#!/usr/bin/env python3
"""Validate, decode, and preview CPV v1.3 global-palette video files."""

from __future__ import annotations

import argparse
import bisect
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Optional

try:
    import numpy as np
except ImportError:
    np = None

try:
    import lz4.block as lz4_block
except ImportError:
    lz4_block = None


MAGIC = b"CPV\0"
VERSION_MAJOR = 1
VERSION_MINOR = 3

HEADER = struct.Struct("<4sBBHIHHHHIHHIIII20s")
FRAME_HEADER = struct.Struct("<4sBBHIII")
INDEX_HEADER = struct.Struct("<4sI")
INDEX_ENTRY = struct.Struct("<III")

MAX_WIDTH = 528
MAX_HEIGHT = 320
MAX_FPS = 30
MAX_RUN_LENGTH = 8191
FRAME_FLAG_LZ4 = 0x01
FRAME_FLAG_DEFLATE = 0x02
FRAME_FLAG_GLOBAL_MOTION = 0x04
FRAME_COMPRESSION_MASK = FRAME_FLAG_LZ4 | FRAME_FLAG_DEFLATE
FRAME_FLAGS_MASK = FRAME_COMPRESSION_MASK | FRAME_FLAG_GLOBAL_MOTION
MAX_GLOBAL_MOTION = 8
MOTION_HEADER = struct.Struct("<bbH")

FRAME_I = 0
FRAME_P = 1

OP_INDEX_LITERAL = 0
OP_INDEX_REPEAT = 1
OP_SKIP = 2
OP_RAW565 = 3
OP_DELTA_MASK = 4

OP_NAMES = ("INDEX_LITERAL", "INDEX_REPEAT", "SKIP", "RAW565", "DELTA_MASK")


class CPVError(Exception):
    """Raised when a CPV file is malformed or unsupported."""


@dataclass(frozen=True)
class FrameRecord:
    number: int
    frame_type: int
    duration_ms: int
    flags: int
    payload_size: int
    file_offset: int
    payload_offset: int
    start_ms: int

    @property
    def end_ms(self) -> int:
        return self.start_ms + self.duration_ms


class CPVFile:
    """Random-access CPV file reader with strict structural validation."""

    def __init__(self, path: Path):
        if np is None:
            raise CPVError(
                "NumPy is required by the desktop preview player. "
                "On Arch/CachyOS install it with 'sudo pacman -S python-numpy'."
            )
        self.path = path
        self.stream: BinaryIO = path.open("rb")
        self.file_size = path.stat().st_size
        self.width = 0
        self.height = 0
        self.fps_num = 0
        self.fps_den = 0
        self.version_minor = 0
        self.index_bits = 8
        self.frame_count = 0
        self.palette_count = 0
        self.total_duration_ms = 0
        self.frames: list[FrameRecord] = []
        self.keyframes: list[tuple[int, int, int]] = []
        self.keyframe_times: list[int] = []
        self.frame_starts: list[int] = []
        self.palette = np.empty(0, dtype=np.uint16)
        self.last_decoded_frame = -1
        try:
            self._read_header_palette_and_index()
        except Exception:
            self.stream.close()
            raise

    def close(self) -> None:
        self.stream.close()

    def _read_exact_at(self, offset: int, size: int, description: str) -> bytes:
        if offset < 0 or size < 0 or offset > self.file_size or size > self.file_size - offset:
            raise CPVError(f"{description} lies outside the file")
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise CPVError(f"truncated {description}")
        return data

    def _read_header_palette_and_index(self) -> None:
        if self.file_size < HEADER.size + 2 + INDEX_HEADER.size:
            raise CPVError("file is too small to contain a CPV header, palette, and index")
        if self.file_size > 0xFFFFFFFF:
            raise CPVError("file exceeds the CPV v1 4 GiB file-size limit")

        fields = HEADER.unpack(self._read_exact_at(0, HEADER.size, "file header"))
        (
            magic, major, minor, header_size, flags,
            self.width, self.height, self.fps_num, self.fps_den,
            self.frame_count, self.palette_count, reserved0,
            palette_offset, frames_offset, index_offset, self.total_duration_ms,
            reserved1,
        ) = fields

        if magic != MAGIC:
            raise CPVError("invalid magic; expected CPV\\0")
        if major != VERSION_MAJOR or minor not in (0, 1, 2, VERSION_MINOR):
            raise CPVError(
                f"unsupported CPV version {major}.{minor}; this player supports 1.0, 1.1, 1.2 and 1.3"
            )
        self.version_minor = minor
        self.index_bits = 8 if minor == 0 else max(1, (self.palette_count - 1).bit_length())
        if header_size != HEADER.size:
            raise CPVError(f"invalid file header size {header_size}; expected {HEADER.size}")
        if flags != 0 or reserved0 != 0 or reserved1 != bytes(20):
            raise CPVError("reserved file fields and flags must be zero in CPV v1")
        if not (1 <= self.width <= MAX_WIDTH and 1 <= self.height <= MAX_HEIGHT):
            raise CPVError(f"invalid dimensions {self.width}x{self.height}")
        if self.fps_num == 0 or self.fps_den == 0:
            raise CPVError("frame-rate numerator and denominator must be non-zero")
        if self.fps_num > MAX_FPS * self.fps_den:
            raise CPVError(f"frame rate exceeds the CPV v1 maximum of {MAX_FPS} FPS")
        if self.frame_count == 0:
            raise CPVError("frame_count must be greater than zero")
        if not (1 <= self.palette_count <= 256):
            raise CPVError("palette_count must be in the range 1..256")
        if self.total_duration_ms == 0:
            raise CPVError("total duration must be greater than zero")
        if palette_offset != HEADER.size:
            raise CPVError(f"palette_offset must equal {HEADER.size}")
        expected_frames_offset = palette_offset + self.palette_count * 2
        if frames_offset != expected_frames_offset:
            raise CPVError("frames_offset does not immediately follow the global palette")
        if not (frames_offset < index_offset <= self.file_size - INDEX_HEADER.size):
            raise CPVError("index_offset is outside the valid frame/index region")
        if self.frame_count > (index_offset - frames_offset) // FRAME_HEADER.size:
            raise CPVError("frame_count cannot fit in the declared frame-record region")

        raw_palette = self._read_exact_at(
            palette_offset, self.palette_count * 2, "global RGB565 palette"
        )
        self.palette = np.frombuffer(raw_palette, dtype="<u2").astype(np.uint16, copy=True)
        if np.unique(self.palette).size != self.palette_count:
            raise CPVError("global palette contains duplicate RGB565 entries")

        index_magic, index_count = INDEX_HEADER.unpack(
            self._read_exact_at(index_offset, INDEX_HEADER.size, "index header")
        )
        if index_magic != b"IDX1":
            raise CPVError("invalid index magic; expected IDX1")
        if index_count == 0 or index_count > self.frame_count:
            raise CPVError("keyframe index entry count is invalid")
        expected_size = index_offset + INDEX_HEADER.size + index_count * INDEX_ENTRY.size
        if expected_size != self.file_size:
            raise CPVError("keyframe index size does not match end-of-file")

        raw_entries = self._read_exact_at(
            index_offset + INDEX_HEADER.size,
            index_count * INDEX_ENTRY.size,
            "keyframe index entries",
        )
        stored_entries = [
            INDEX_ENTRY.unpack_from(raw_entries, offset)
            for offset in range(0, len(raw_entries), INDEX_ENTRY.size)
        ]

        # Parse the frame sequence and independently reconstruct the expected index.
        position = frames_offset
        elapsed_ms = 0
        last_keyframe_elapsed = 0
        expected_entries: list[tuple[int, int, int]] = []
        for number in range(self.frame_count):
            if position > index_offset or FRAME_HEADER.size > index_offset - position:
                raise CPVError(f"frame {number} header overlaps the keyframe index")

            (
                frame_magic, frame_type, frame_flags, frame_header_size,
                frame_number, duration_ms, payload_size,
            ) = FRAME_HEADER.unpack(
                self._read_exact_at(position, FRAME_HEADER.size, f"frame {number} header")
            )

            if frame_magic != b"FRM1":
                raise CPVError(f"frame {number} has invalid magic; expected FRM1")
            if frame_type not in (FRAME_I, FRAME_P):
                raise CPVError(f"frame {number} has unknown type {frame_type}")
            compression_flags = frame_flags & FRAME_COMPRESSION_MASK
            if frame_flags & ~FRAME_FLAGS_MASK or compression_flags == FRAME_COMPRESSION_MASK:
                raise CPVError(f"frame {number} has invalid flag bits 0x{frame_flags:02x}")
            if minor < 2 and frame_flags & FRAME_FLAG_DEFLATE:
                raise CPVError(f"frame {number}: DEFLATE flag is only supported in CPV 1.2 or newer")
            if minor < 3 and frame_flags & FRAME_FLAG_GLOBAL_MOTION:
                raise CPVError(f"frame {number}: global motion requires CPV 1.3")
            if frame_flags & FRAME_FLAG_GLOBAL_MOTION and frame_type != FRAME_P:
                raise CPVError(f"frame {number}: global motion is valid only in P-frames")
            if minor == 0 and frame_flags != 0:
                raise CPVError(f"CPV 1.0 frame {number} must have zero flags")
            if frame_header_size != FRAME_HEADER.size:
                raise CPVError(f"frame {number} has invalid header size")
            if frame_number != number:
                raise CPVError(f"frame numbering error: expected {number}, found {frame_number}")
            if duration_ms == 0:
                raise CPVError(f"frame {number} has zero duration")
            if number == 0 and frame_type != FRAME_I:
                raise CPVError("the first frame must be an I-frame")
            if frame_type == FRAME_I:
                last_keyframe_elapsed = elapsed_ms
            elif elapsed_ms - last_keyframe_elapsed > 10000:
                raise CPVError(
                    f"frame {number} exceeds the maximum 10-second keyframe dependency interval"
                )

            payload_offset = position + FRAME_HEADER.size
            if payload_offset > index_offset or payload_size > index_offset - payload_offset:
                raise CPVError(f"frame {number} payload overlaps or exceeds the index")

            self.frames.append(FrameRecord(
                number=number,
                frame_type=frame_type,
                duration_ms=duration_ms,
                flags=frame_flags,
                payload_size=payload_size,
                file_offset=position,
                payload_offset=payload_offset,
                start_ms=elapsed_ms,
            ))

            if frame_type == FRAME_I:
                expected_entries.append((number, elapsed_ms, position))

            elapsed_ms += duration_ms
            if elapsed_ms > 0xFFFFFFFF:
                raise CPVError("sum of frame durations exceeds the CPV v1 duration limit")
            position = payload_offset + payload_size

        if position != index_offset:
            raise CPVError("frame records do not end exactly at index_offset")
        if elapsed_ms != self.total_duration_ms:
            raise CPVError(
                f"duration mismatch: header says {self.total_duration_ms} ms, "
                f"frame records sum to {elapsed_ms} ms"
            )
        if len(expected_entries) != index_count:
            raise CPVError("index entry count does not match the number of I-frames")

        previous_number = previous_time = previous_offset = -1
        for index, entry in enumerate(stored_entries):
            number, elapsed, offset = entry
            if number <= previous_number or elapsed <= previous_time or offset <= previous_offset:
                raise CPVError("keyframe index entries are not strictly increasing")
            if entry != expected_entries[index]:
                raise CPVError(f"keyframe index entry {index} does not match its I-frame")
            previous_number, previous_time, previous_offset = number, elapsed, offset

        if stored_entries[0] != (0, 0, self.frames[0].file_offset):
            raise CPVError("the first keyframe index entry must reference frame 0 at time 0")

        self.keyframes = stored_entries
        self.keyframe_times = [entry[1] for entry in stored_entries]
        self.frame_starts = [record.start_ms for record in self.frames]

    def _read_payload(self, record: FrameRecord) -> bytes:
        pixel_count = self.width * self.height
        motion_prefix_size = MOTION_HEADER.size if record.flags & FRAME_FLAG_GLOBAL_MOTION else 0
        max_decoded_size = pixel_count * 3 + motion_prefix_size
        compression_flags = record.flags & FRAME_COMPRESSION_MASK
        if compression_flags:
            if self.version_minor < 1:
                raise CPVError(f"frame {record.number}: compression flags are invalid in CPV 1.0")
            if compression_flags == FRAME_FLAG_DEFLATE and self.version_minor < 2:
                raise CPVError(f"frame {record.number}: DEFLATE is invalid in CPV 1.1")
            if record.payload_size < 5:
                raise CPVError(f"frame {record.number}: truncated compressed payload")
            stored = self._read_exact_at(
                record.payload_offset, record.payload_size, f"frame {record.number} payload"
            )
            decoded_size = struct.unpack_from("<I", stored, 0)[0]
            if not (1 <= decoded_size <= max_decoded_size):
                raise CPVError(
                    f"frame {record.number}: invalid decoded size {decoded_size}"
                )
            if record.payload_size - 4 >= decoded_size:
                raise CPVError(f"frame {record.number}: compressed payload is not smaller than raw payload")
            try:
                if compression_flags == FRAME_FLAG_LZ4:
                    if lz4_block is None:
                        raise CPVError(
                            "this video uses LZ4; install python-lz4 "
                            "(Arch/CachyOS: sudo pacman -S python-lz4)"
                        )
                    decoded = lz4_block.decompress(stored[4:], uncompressed_size=decoded_size)
                elif compression_flags == FRAME_FLAG_DEFLATE:
                    import zlib
                    decoder = zlib.decompressobj()
                    decoded = decoder.decompress(stored[4:], decoded_size + 1)
                    if len(decoded) > decoded_size or decoder.unconsumed_tail:
                        raise CPVError(f"frame {record.number}: DEFLATE output exceeds declared size")
                    if not decoder.eof or decoder.unused_data:
                        raise CPVError(f"frame {record.number}: invalid DEFLATE stream or trailing data")
                    decoded += decoder.flush()
                else:
                    raise CPVError(f"frame {record.number}: invalid compression flags")
            except CPVError:
                raise
            except Exception as exc:
                raise CPVError(f"frame {record.number}: decompression failed: {exc}") from exc
            if len(decoded) != decoded_size:
                raise CPVError(
                    f"frame {record.number}: decoded {len(decoded)} bytes, expected {decoded_size}"
                )
            return decoded

        if record.payload_size > max_decoded_size:
            raise CPVError(
                f"frame {record.number}: raw packet payload exceeds the {max_decoded_size}-byte safety limit"
            )
        return self._read_exact_at(
            record.payload_offset, record.payload_size, f"frame {record.number} payload"
        )

    @staticmethod
    def _read_run_length(payload: memoryview, position: int, frame_number: int,
                         version_minor: int):
        if position >= len(payload):
            raise CPVError(f"frame {frame_number}: truncated packet header")
        first = payload[position]
        position += 1
        opcode = first >> 6

        # 0xA0 0x00 is the v1.1 extended SKIP/DELTA_MASK sentinel.
        if opcode == OP_SKIP and first == 0xA0 and position < len(payload) and payload[position] == 0:
            if version_minor < 1:
                raise CPVError(f"frame {frame_number}: DELTA_MASK is not valid in CPV 1.0")
            position += 1
            if position + 2 > len(payload):
                raise CPVError(f"frame {frame_number}: truncated DELTA_MASK span length")
            run_length = struct.unpack_from("<H", payload, position)[0]
            position += 2
            if not (1 <= run_length <= 65535):
                raise CPVError(f"frame {frame_number}: invalid DELTA_MASK span length {run_length}")
            return OP_DELTA_MASK, run_length, position

        if (first & 0x20) == 0:
            run_length = (first & 0x1F) + 1
        else:
            if position >= len(payload):
                raise CPVError(f"frame {frame_number}: truncated long packet header")
            second = payload[position]
            position += 1
            run_length = ((first & 0x1F) << 8) | second
            if not (33 <= run_length <= MAX_RUN_LENGTH):
                raise CPVError(f"frame {frame_number}: invalid long run length {run_length}")
        return opcode, run_length, position

    def _read_palette_indices(self, payload: memoryview, position: int,
                              count: int, frame_number: int):
        bit_width = self.index_bits
        byte_count = (count * bit_width + 7) // 8
        end = position + byte_count
        if end > len(payload):
            raise CPVError(f"frame {frame_number}: truncated packed palette indices")
        packed = np.frombuffer(payload, dtype=np.uint8, count=byte_count, offset=position)
        if bit_width == 8:
            indices = packed.copy()
        elif count == 0:
            indices = np.empty(0, dtype=np.uint8)
        else:
            unpacked = np.unpackbits(packed, bitorder="little")
            useful_bits = count * bit_width
            if np.any(unpacked[useful_bits:]):
                raise CPVError(f"frame {frame_number}: non-zero INDEX_LITERAL padding bits")
            weights = (1 << np.arange(bit_width, dtype=np.uint16))
            indices = (
                unpacked[:useful_bits].reshape(count, bit_width).astype(np.uint16) * weights
            ).sum(axis=1).astype(np.uint8)
        if indices.size and int(indices.max()) >= self.palette_count:
            raise CPVError(f"frame {frame_number}: invalid palette index")
        return indices, end

    def decode_frame(self, frame_number: int, framebuffer):
        if not (0 <= frame_number < len(self.frames)):
            raise CPVError("requested frame is outside the video")
        record = self.frames[frame_number]
        if record.frame_type == FRAME_P and self.last_decoded_frame != frame_number - 1:
            raise CPVError(
                f"P-frame {frame_number} requires frame {frame_number - 1} "
                "to be decoded immediately before it"
            )

        payload_bytes = self._read_payload(record)
        payload = memoryview(payload_bytes)
        payload_position = 0
        if record.flags & FRAME_FLAG_GLOBAL_MOTION:
            if record.frame_type != FRAME_P or self.version_minor < 3:
                raise CPVError(f"frame {frame_number}: invalid global-motion flag")
            if len(payload) < MOTION_HEADER.size:
                raise CPVError(f"frame {frame_number}: truncated global-motion header")
            dx, dy, fill_colour = MOTION_HEADER.unpack_from(payload, 0)
            if (
                (dx == 0 and dy == 0)
                or abs(dx) > MAX_GLOBAL_MOTION
                or abs(dy) > MAX_GLOBAL_MOTION
            ):
                raise CPVError(f"frame {frame_number}: invalid global-motion vector ({dx}, {dy})")
            source = framebuffer.reshape(self.height, self.width)
            shifted = np.full(
                (self.height, self.width), fill_colour, dtype=np.uint16
            )
            source_x0 = max(0, -dx)
            source_y0 = max(0, -dy)
            source_x1 = min(self.width, self.width - dx)
            source_y1 = min(self.height, self.height - dy)
            shifted[
                source_y0 + dy:source_y1 + dy,
                source_x0 + dx:source_x1 + dx,
            ] = source[source_y0:source_y1, source_x0:source_x1]
            framebuffer[:] = shifted.reshape(-1)
            payload_position = MOTION_HEADER.size
        pixel_position = 0
        pixel_count = self.width * self.height

        while payload_position < len(payload):
            opcode, run_length, payload_position = self._read_run_length(
                payload, payload_position, frame_number, self.version_minor
            )
            if run_length > pixel_count - pixel_position:
                raise CPVError(f"frame {frame_number}: packet exceeds frame pixel count")
            end = pixel_position + run_length

            if opcode == OP_INDEX_LITERAL:
                indices, payload_position = self._read_palette_indices(
                    payload, payload_position, run_length, frame_number
                )
                framebuffer[pixel_position:end] = self.palette[indices]

            elif opcode == OP_INDEX_REPEAT:
                if payload_position >= len(payload):
                    raise CPVError(f"frame {frame_number}: truncated INDEX_REPEAT data")
                palette_index = int(payload[payload_position])
                payload_position += 1
                if palette_index >= self.palette_count:
                    raise CPVError(f"frame {frame_number}: invalid INDEX_REPEAT palette index")
                framebuffer[pixel_position:end] = self.palette[palette_index]

            elif opcode == OP_SKIP:
                if record.frame_type != FRAME_P:
                    raise CPVError(f"frame {frame_number}: SKIP is invalid in an I-frame")

            elif opcode == OP_DELTA_MASK:
                if record.frame_type != FRAME_P:
                    raise CPVError(f"frame {frame_number}: DELTA_MASK is invalid in an I-frame")
                mask_size = (run_length + 7) // 8
                mask_end = payload_position + mask_size
                if mask_end > len(payload):
                    raise CPVError(f"frame {frame_number}: truncated DELTA_MASK bitmap")
                mask_bytes = np.frombuffer(
                    payload, dtype=np.uint8, count=mask_size, offset=payload_position
                )
                mask_bits = np.unpackbits(mask_bytes, bitorder="little")[:run_length]
                if run_length & 7:
                    used_bits = run_length & 7
                    final_byte = int(mask_bytes[-1])
                    if final_byte >> used_bits:
                        raise CPVError(f"frame {frame_number}: non-zero DELTA_MASK padding bits")
                changed_positions = np.flatnonzero(mask_bits)
                changed_count = int(changed_positions.size)
                if changed_count == 0 or changed_count == run_length:
                    raise CPVError(
                        f"frame {frame_number}: DELTA_MASK must contain both changed and unchanged pixels"
                    )
                indices, payload_position = self._read_palette_indices(
                    payload, mask_end, changed_count, frame_number
                )
                framebuffer[pixel_position + changed_positions] = self.palette[indices]
                # _read_palette_indices starts at mask_end and returns the byte after the index stream.

            elif opcode == OP_RAW565:
                byte_count = run_length * 2
                data_end = payload_position + byte_count
                if data_end > len(payload):
                    raise CPVError(f"frame {frame_number}: truncated RAW565 data")
                raw_values = np.frombuffer(
                    payload, dtype="<u2", count=run_length, offset=payload_position
                )
                framebuffer[pixel_position:end] = raw_values
                payload_position = data_end

            else:
                raise CPVError(f"frame {frame_number}: unknown packet opcode {opcode}")

            pixel_position = end

        if pixel_position != pixel_count:
            raise CPVError(
                f"frame {frame_number}: packet stream covers {pixel_position} "
                f"of {pixel_count} pixels"
            )
        if payload_position != len(payload):
            raise CPVError(f"frame {frame_number}: unconsumed payload bytes")

        self.last_decoded_frame = frame_number
        return record

    def seek_time(self, target_ms: int, framebuffer):
        """Decode from the nearest keyframe to the frame containing target_ms."""
        target_ms = max(0, min(self.total_duration_ms - 1, int(target_ms)))
        keyframe_index = bisect.bisect_right(self.keyframe_times, target_ms) - 1
        if keyframe_index < 0:
            raise CPVError("no keyframe exists before the requested time")

        first_frame = self.keyframes[keyframe_index][0]
        target_frame = bisect.bisect_right(self.frame_starts, target_ms) - 1
        target_frame = max(first_frame, target_frame)

        result: Optional[FrameRecord] = None
        for number in range(first_frame, target_frame + 1):
            result = self.decode_frame(number, framebuffer)
        if result is None:
            raise CPVError("seeking did not decode a frame")
        return result


class CPVPlayer:
    def __init__(self, cpv: CPVFile):
        try:
            import tkinter as tk
            from tkinter import messagebox
        except ImportError as exc:
            raise RuntimeError(
                "Tkinter is not available. On Arch/CachyOS install it with "
                "'sudo pacman -S tk'."
            ) from exc

        self.tk = tk
        self.messagebox = messagebox
        self.cpv = cpv
        self.framebuffer = np.zeros(cpv.width * cpv.height, dtype=np.uint16)
        self.current_frame = -1
        self.current_time_ms = 0
        self.playing = False
        self.after_id: Optional[str] = None
        self.deadline = 0.0
        self.remaining_ms = 0
        self.photo = None
        self.closed = False

        try:
            self.root = tk.Tk()
        except tk.TclError as exc:
            raise RuntimeError(
                f"cannot open the preview window (is a graphical session available?): {exc}"
            ) from exc

        self.root.title(f"CPV Player — {cpv.path.name}")
        self.root.resizable(False, False)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.bind("<space>", lambda _event: self.toggle_playback())
        self.root.bind("<Left>", lambda _event: self.seek_relative(-1000))
        self.root.bind("<Right>", lambda _event: self.seek_relative(1000))
        self.root.bind("<Home>", lambda _event: self.seek_absolute(0))
        self.root.bind("<End>", lambda _event: self.seek_absolute(cpv.total_duration_ms - 1))

        self.image_label = tk.Label(self.root, borderwidth=0)
        self.image_label.pack(padx=8, pady=(8, 4))

        controls = tk.Frame(self.root)
        controls.pack(fill="x", padx=8, pady=(0, 4))

        self.back_button = tk.Button(
            controls, text="−1 s", width=7, command=lambda: self.seek_relative(-1000)
        )
        self.back_button.pack(side="left")

        self.play_button = tk.Button(
            controls, text="Pause", width=9, command=self.toggle_playback
        )
        self.play_button.pack(side="left", padx=6)

        self.forward_button = tk.Button(
            controls, text="+1 s", width=7, command=lambda: self.seek_relative(1000)
        )
        self.forward_button.pack(side="left")

        self.status = tk.Label(self.root, anchor="w")
        self.status.pack(fill="x", padx=10, pady=(0, 8))

        self.show_time(0)
        self.start_playback()

    @staticmethod
    def _format_time(milliseconds: int) -> str:
        seconds = max(0, milliseconds) // 1000
        tenths = (max(0, milliseconds) % 1000) // 100
        return f"{seconds // 60:02d}:{seconds % 60:02d}.{tenths}"

    def _render(self) -> None:
        pixels = self.framebuffer
        rgb = np.empty((self.cpv.height, self.cpv.width, 3), dtype=np.uint8)
        values = pixels.reshape(self.cpv.height, self.cpv.width)

        red = ((values >> 11) & 31).astype(np.uint8)
        green = ((values >> 5) & 63).astype(np.uint8)
        blue = (values & 31).astype(np.uint8)

        rgb[:, :, 0] = (red << 3) | (red >> 2)
        rgb[:, :, 1] = (green << 2) | (green >> 4)
        rgb[:, :, 2] = (blue << 3) | (blue >> 2)

        ppm = (
            f"P6\n{self.cpv.width} {self.cpv.height}\n255\n".encode("ascii")
            + rgb.tobytes()
        )
        # Tk accepts binary PPM bytes. Do not base64-wrap this payload:
        # Tk's PPM loader expects the P6 header followed by raw pixel bytes.
        self.photo = self.tk.PhotoImage(data=ppm, format="PPM")
        self.image_label.configure(image=self.photo)

        record = self.cpv.frames[self.current_frame]
        self.status.configure(
            text=(
                f"Frame {self.current_frame + 1}/{len(self.cpv.frames)}   "
                f"{self.cpv.width}×{self.cpv.height}   "
                f"{self._format_time(record.start_ms)} / "
                f"{self._format_time(self.cpv.total_duration_ms)}"
            )
        )

    def show_time(self, target_ms: int) -> None:
        record = self.cpv.seek_time(target_ms, self.framebuffer)
        self.current_frame = record.number
        self.current_time_ms = record.start_ms
        self.remaining_ms = max(1, record.end_ms - max(record.start_ms, target_ms))
        self._render()

    def show_next_frame(self) -> None:
        next_number = self.current_frame + 1
        if next_number >= len(self.cpv.frames):
            self.playing = False
            self.play_button.configure(text="Play")
            self.remaining_ms = 0
            return
        record = self.cpv.decode_frame(next_number, self.framebuffer)
        self.current_frame = record.number
        self.current_time_ms = record.start_ms
        self.remaining_ms = record.duration_ms
        self._render()

    def _schedule_next(self, delay_ms: int, base_deadline: Optional[float] = None) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        delay_ms = max(1, int(delay_ms))
        base = time.monotonic() if base_deadline is None else base_deadline
        self.deadline = base + delay_ms / 1000.0
        wait_ms = max(1, round((self.deadline - time.monotonic()) * 1000))
        self.remaining_ms = wait_ms
        self.after_id = self.root.after(wait_ms, self._advance)

    def _advance(self) -> None:
        self.after_id = None
        if not self.playing or self.closed:
            return
        previous_deadline = self.deadline
        try:
            self.show_next_frame()
            if self.playing:
                self._schedule_next(self.remaining_ms, base_deadline=previous_deadline)
        except (CPVError, OSError) as exc:
            self.pause_playback()
            self.messagebox.showerror("CPV decoding error", str(exc), parent=self.root)

    def start_playback(self) -> None:
        if self.current_frame >= len(self.cpv.frames) - 1:
            self.show_time(0)
        self.playing = True
        self.play_button.configure(text="Pause")
        self._schedule_next(self.remaining_ms or self.cpv.frames[self.current_frame].duration_ms)

    def pause_playback(self) -> None:
        if self.after_id is not None:
            self.remaining_ms = max(1, round((self.deadline - time.monotonic()) * 1000))
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.playing = False
        self.play_button.configure(text="Play")

    def toggle_playback(self) -> None:
        if self.playing:
            self.pause_playback()
        else:
            self.start_playback()

    def seek_absolute(self, target_ms: int) -> None:
        was_playing = self.playing
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.playing = False
        try:
            self.show_time(target_ms)
        except (CPVError, OSError) as exc:
            self.messagebox.showerror("CPV seeking error", str(exc), parent=self.root)
        self.playing = was_playing
        if was_playing:
            self.play_button.configure(text="Pause")
            self._schedule_next(self.remaining_ms)
        else:
            self.play_button.configure(text="Play")

    def seek_relative(self, delta_ms: int) -> None:
        if self.current_frame < 0:
            return
        # Use the current displayed frame's start. This makes seeks deterministic
        # and avoids depending on Tk event-loop scheduling jitter.
        self.seek_absolute(self.cpv.frames[self.current_frame].start_ms + delta_ms)

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Play and inspect CPV v1.0/v1.1 video files.")
    parser.add_argument("cpv_file", help="input .cpv file")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    path = Path(args.cpv_file).expanduser().resolve()
    if not path.is_file():
        print(f"play.py: error: file not found: {path}", file=sys.stderr)
        return 1

    cpv: Optional[CPVFile] = None
    try:
        cpv = CPVFile(path)
        print(
            f"CPV v1.{cpv.version_minor}: {cpv.width}x{cpv.height}, "
            f"{cpv.fps_num}/{cpv.fps_den} FPS, "
            f"{len(cpv.frames)} stored frames, "
            f"{cpv.total_duration_ms / 1000:.3f} s, "
            f"palette {cpv.palette_count}"
        )
        player = CPVPlayer(cpv)
        player.run()
    except (OSError, CPVError, RuntimeError, struct.error, OverflowError) as exc:
        print(f"play.py: error: {exc}", file=sys.stderr)
        return 1
    finally:
        if cpv is not None:
            cpv.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
