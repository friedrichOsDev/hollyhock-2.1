#!/usr/bin/env python3
"""Decode and play CPV v1.0 files on a desktop using Tkinter."""

from __future__ import annotations

import argparse
import base64
import bisect
import struct
import sys
import time
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Optional

MAGIC = b"CPV\0"
VERSION_MAJOR = 1
VERSION_MINOR = 0
HEADER = struct.Struct("<4sHHIHHHHIIQI")
FRAME_HEADER = struct.Struct("<BBHIII")
INDEX_HEADER = struct.Struct("<4sI")
INDEX_ENTRY = struct.Struct("<IIQ")

MAX_WIDTH = 528
MAX_HEIGHT = 320
ROOT_TILE_SIZE = 32
MIN_TILE_SIZE = 8

MODE_SOLID = 0x00
MODE_PALETTE = 0x01
MODE_RLE565 = 0x02
MODE_RAW565 = 0x03
MODE_SPLIT = 0x04
MODE_SKIP = 0x05

FRAME_I = 0
FRAME_P = 1


class CPVError(Exception):
    """Raised when a CPV file is malformed or unsupported."""


@dataclass(frozen=True)
class FrameRecord:
    number: int
    frame_type: int
    duration_ms: int
    payload_size: int
    file_offset: int
    payload_offset: int
    start_ms: int


class PayloadReader:
    def __init__(self, payload: bytes):
        self.data = payload
        self.position = 0

    @property
    def remaining(self) -> int:
        return len(self.data) - self.position

    def read_u8(self) -> int:
        if self.remaining < 1:
            raise CPVError("truncated tile payload (expected one byte)")
        value = self.data[self.position]
        self.position += 1
        return value

    def read_u16(self) -> int:
        if self.remaining < 2:
            raise CPVError("truncated tile payload (expected RGB565 word)")
        value = self.data[self.position] | (self.data[self.position + 1] << 8)
        self.position += 2
        return value

    def read_bytes(self, size: int) -> bytes:
        if size < 0 or size > self.remaining:
            raise CPVError("tile data extends beyond the frame payload")
        start = self.position
        self.position += size
        return self.data[start:self.position]


class CPVFile:
    def __init__(self, path: Path):
        self.path = path
        self.stream: BinaryIO = path.open("rb")
        self.file_size = path.stat().st_size
        self.width = 0
        self.height = 0
        self.fps_num = 0
        self.fps_den = 0
        self.total_duration_ms = 0
        self.frames: list[FrameRecord] = []
        self.keyframes: list[tuple[int, int, int]] = []
        self.last_decoded_frame = -1
        try:
            self._read_header_and_index()
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

    def _read_header_and_index(self) -> None:
        if self.file_size < HEADER.size + INDEX_HEADER.size:
            raise CPVError("file is too small to contain a CPV header and index")

        fields = HEADER.unpack(self._read_exact_at(0, HEADER.size, "file header"))
        (magic, major, minor, header_size, self.width, self.height,
         self.fps_num, self.fps_den, frame_count, flags,
         index_offset, self.total_duration_ms) = fields

        if magic != MAGIC:
            raise CPVError("invalid CPV magic; expected CPV\\0")
        if (major, minor) != (VERSION_MAJOR, VERSION_MINOR):
            raise CPVError(f"unsupported CPV version {major}.{minor}; this player supports 1.0")
        if header_size != HEADER.size:
            raise CPVError(f"invalid file header size {header_size}; expected {HEADER.size}")
        if not (1 <= self.width <= MAX_WIDTH and 1 <= self.height <= MAX_HEIGHT):
            raise CPVError(f"invalid video dimensions {self.width}x{self.height}")
        if self.fps_num == 0 or self.fps_den == 0:
            raise CPVError("frame-rate numerator and denominator must be non-zero")
        if self.fps_num > 60 * self.fps_den:
            raise CPVError("frame rate exceeds the CPV v1 maximum of 60 FPS")
        if frame_count == 0:
            raise CPVError("frame_count must be greater than zero")
        if flags != 0:
            raise CPVError("reserved file flags must be zero in CPV v1.0")
        if self.total_duration_ms == 0:
            raise CPVError("total_duration_ms must be greater than zero")
        if index_offset < header_size or index_offset > self.file_size - INDEX_HEADER.size:
            raise CPVError("index_offset is outside the valid frame/index region")

        index_magic, index_count = INDEX_HEADER.unpack(
            self._read_exact_at(index_offset, INDEX_HEADER.size, "frame index header")
        )
        if index_magic != b"IDX1":
            raise CPVError("invalid frame index magic; expected IDX1")
        if index_count == 0:
            raise CPVError("frame index must contain at least one keyframe")
        expected_file_size = index_offset + INDEX_HEADER.size + index_count * INDEX_ENTRY.size
        if expected_file_size != self.file_size:
            raise CPVError("frame index size does not match the end of the file")

        raw_index = self._read_exact_at(
            index_offset + INDEX_HEADER.size,
            index_count * INDEX_ENTRY.size,
            "frame index entries",
        )
        stored_index = [
            INDEX_ENTRY.unpack_from(raw_index, i * INDEX_ENTRY.size)
            for i in range(index_count)
        ]

        # Parse every frame header and verify the index against the actual stream.
        position = header_size
        elapsed_ms = 0
        expected_keyframes: list[tuple[int, int, int]] = []
        for number in range(frame_count):
            if position > index_offset or FRAME_HEADER.size > index_offset - position:
                raise CPVError(f"frame {number} header overlaps the frame index")
            raw_header = self._read_exact_at(position, FRAME_HEADER.size, f"frame {number} header")
            frame_type, frame_flags, header_size_value, duration_ms, payload_size, frame_number = FRAME_HEADER.unpack(raw_header)

            if frame_type not in (FRAME_I, FRAME_P):
                raise CPVError(f"frame {number} has unknown frame type {frame_type}")
            if frame_flags != 0:
                raise CPVError(f"frame {number} has non-zero reserved flags")
            if header_size_value != FRAME_HEADER.size:
                raise CPVError(f"frame {number} has invalid header size {header_size_value}")
            if duration_ms == 0:
                raise CPVError(f"frame {number} has zero duration")
            if frame_number != number:
                raise CPVError(f"frame numbering error: expected {number}, found {frame_number}")
            if number == 0 and frame_type != FRAME_I:
                raise CPVError("the first frame must be an I-frame")

            payload_offset = position + FRAME_HEADER.size
            if payload_offset > index_offset or payload_size > index_offset - payload_offset:
                raise CPVError(f"frame {number} payload overlaps or exceeds the frame index")

            record = FrameRecord(
                number=number,
                frame_type=frame_type,
                duration_ms=duration_ms,
                payload_size=payload_size,
                file_offset=position,
                payload_offset=payload_offset,
                start_ms=elapsed_ms,
            )
            self.frames.append(record)
            if frame_type == FRAME_I:
                expected_keyframes.append((number, elapsed_ms, position))

            elapsed_ms += duration_ms
            if elapsed_ms > 0xFFFFFFFF:
                raise CPVError("sum of frame durations exceeds the CPV uint32 limit")
            position = payload_offset + payload_size

        if position != index_offset:
            raise CPVError("frame records do not end exactly at index_offset")
        if elapsed_ms != self.total_duration_ms:
            raise CPVError(
                f"total duration mismatch: header says {self.total_duration_ms} ms, "
                f"frames sum to {elapsed_ms} ms"
            )
        if len(expected_keyframes) != index_count:
            raise CPVError("frame index entry count does not match the number of I-frames")

        previous_number = previous_time = previous_offset = -1
        for i, entry in enumerate(stored_index):
            number, start_ms, file_offset = entry
            if number <= previous_number or start_ms <= previous_time or file_offset <= previous_offset:
                raise CPVError("frame index entries are not strictly increasing")
            if entry != expected_keyframes[i]:
                raise CPVError(f"frame index entry {i} does not match its I-frame")
            previous_number, previous_time, previous_offset = number, start_ms, file_offset
        if stored_index[0][0] != 0 or stored_index[0][1] != 0:
            raise CPVError("the first frame-index entry must point to frame 0 at time 0")

        self.keyframes = stored_index

    def _decode_node(self, reader: PayloadReader, framebuffer: array,
                     x: int, y: int, size: int, frame_type: int) -> None:
        if x >= self.width or y >= self.height:
            raise CPVError("internal tile traversal produced an out-of-image node")
        visible_width = min(size, self.width - x)
        visible_height = min(size, self.height - y)
        pixel_count = visible_width * visible_height
        mode = reader.read_u8()

        if mode == MODE_SOLID:
            colour = reader.read_u16()
            for row in range(visible_height):
                start = (y + row) * self.width + x
                framebuffer[start:start + visible_width] = array("H", [colour]) * visible_width
            return

        if mode == MODE_PALETTE:
            palette_count = reader.read_u8() + 1
            if not (2 <= palette_count <= 256):
                raise CPVError("PALETTE colour count must be between 2 and 256")
            if palette_count > pixel_count:
                raise CPVError("PALETTE colour count exceeds the visible pixel count")
            palette = [reader.read_u16() for _ in range(palette_count)]
            bits_per_index = (palette_count - 1).bit_length()
            packed_size = (pixel_count * bits_per_index + 7) // 8
            packed = reader.read_bytes(packed_size)
            unused_bits = packed_size * 8 - pixel_count * bits_per_index
            if unused_bits and (packed[-1] >> (8 - unused_bits)) != 0:
                raise CPVError("PALETTE unused high bits in the final byte must be zero")

            mask = (1 << bits_per_index) - 1
            for pixel_index in range(pixel_count):
                bit_offset = pixel_index * bits_per_index
                byte_index, shift = divmod(bit_offset, 8)
                word = packed[byte_index]
                if byte_index + 1 < len(packed):
                    word |= packed[byte_index + 1] << 8
                palette_index = (word >> shift) & mask
                if palette_index >= palette_count:
                    raise CPVError("PALETTE index refers to an unavailable colour")
                row, column = divmod(pixel_index, visible_width)
                framebuffer[(y + row) * self.width + x + column] = palette[palette_index]
            return

        if mode == MODE_RLE565:
            produced = 0
            while produced < pixel_count:
                control = reader.read_u8()
                count = (control & 0x7F) + 1
                if count > pixel_count - produced:
                    raise CPVError("RLE565 token expands beyond the tile pixel count")
                if control & 0x80:
                    colour = reader.read_u16()
                    for offset in range(produced, produced + count):
                        row, column = divmod(offset, visible_width)
                        framebuffer[(y + row) * self.width + x + column] = colour
                else:
                    byte_count = count * 2
                    literal = reader.read_bytes(byte_count)
                    for i in range(count):
                        colour = literal[i * 2] | (literal[i * 2 + 1] << 8)
                        offset = produced + i
                        row, column = divmod(offset, visible_width)
                        framebuffer[(y + row) * self.width + x + column] = colour
                produced += count
            return

        if mode == MODE_RAW565:
            raw = reader.read_bytes(pixel_count * 2)
            decoded = array("H")
            decoded.frombytes(raw)
            if sys.byteorder != "little":
                decoded.byteswap()
            for row in range(visible_height):
                src = row * visible_width
                dst = (y + row) * self.width + x
                framebuffer[dst:dst + visible_width] = decoded[src:src + visible_width]
            return

        if mode == MODE_SPLIT:
            if size <= MIN_TILE_SIZE:
                raise CPVError("SPLIT is not valid for an 8x8 tile")
            half = size // 2
            children = (
                (x, y),
                (x + half, y),
                (x, y + half),
                (x + half, y + half),
            )
            for child_x, child_y in children:
                if child_x < self.width and child_y < self.height:
                    self._decode_node(reader, framebuffer, child_x, child_y, half, frame_type)
            return

        if mode == MODE_SKIP:
            if frame_type != FRAME_P:
                raise CPVError("SKIP is only valid in a P-frame")
            return

        raise CPVError(f"unknown tile mode 0x{mode:02X}")

    def decode_frame(self, frame_number: int, framebuffer: array) -> FrameRecord:
        if not (0 <= frame_number < len(self.frames)):
            raise CPVError("requested frame is outside the video")
        record = self.frames[frame_number]
        if record.frame_type == FRAME_P and self.last_decoded_frame != frame_number - 1:
            raise CPVError("P-frame requires its immediately preceding frame to be decoded first")
        payload = self._read_exact_at(record.payload_offset, record.payload_size,
                                      f"frame {frame_number} payload")
        reader = PayloadReader(payload)

        for y in range(0, self.height, ROOT_TILE_SIZE):
            for x in range(0, self.width, ROOT_TILE_SIZE):
                self._decode_node(reader, framebuffer, x, y, ROOT_TILE_SIZE, record.frame_type)

        if reader.position != len(payload):
            raise CPVError(
                f"frame {frame_number} payload has {reader.remaining} unconsumed trailing bytes"
            )
        self.last_decoded_frame = frame_number
        return record

    def seek_frame(self, target_frame: int, framebuffer: array) -> FrameRecord:
        if not (0 <= target_frame < len(self.frames)):
            raise CPVError("requested frame is outside the video")
        keyframe_numbers = [entry[0] for entry in self.keyframes]
        keyframe_index = bisect.bisect_right(keyframe_numbers, target_frame) - 1
        if keyframe_index < 0:
            raise CPVError("no keyframe is available before the requested frame")
        start_frame = keyframe_numbers[keyframe_index]
        result: Optional[FrameRecord] = None
        for number in range(start_frame, target_frame + 1):
            result = self.decode_frame(number, framebuffer)
        assert result is not None
        return result

    def to_rgb_bytes(self, framebuffer: array) -> bytes:
        rgb = bytearray(len(framebuffer) * 3)
        out = 0
        for value in framebuffer:
            r5 = (value >> 11) & 31
            g6 = (value >> 5) & 63
            b5 = value & 31
            rgb[out] = (r5 << 3) | (r5 >> 2)
            rgb[out + 1] = (g6 << 2) | (g6 >> 4)
            rgb[out + 2] = (b5 << 3) | (b5 >> 2)
            out += 3
        return bytes(rgb)


class CPVPlayer:
    def __init__(self, cpv: CPVFile):
        try:
            import tkinter as tk
        except ImportError as exc:
            raise RuntimeError("Tkinter is not available; install your system's python-tk package") from exc

        self.tk = tk
        self.cpv = cpv
        self.framebuffer = array("H", [0]) * (cpv.width * cpv.height)
        self.current_frame = -1
        self.playing = False
        self.after_id: Optional[str] = None
        self.deadline = 0.0
        self.remaining_ms = 0
        self.photo = None

        try:
            self.root = tk.Tk()
        except tk.TclError as exc:
            raise RuntimeError(f"cannot open the desktop window (is a graphical session available?): {exc}") from exc
        self.root.title(f"CPV Player — {cpv.path.name}")
        self.root.resizable(False, False)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.bind("<space>", lambda _event: self.toggle_playback())
        self.root.bind("<Left>", lambda _event: self.seek_relative(-1000))
        self.root.bind("<Right>", lambda _event: self.seek_relative(1000))

        self.image_label = tk.Label(self.root, borderwidth=0)
        self.image_label.pack(padx=8, pady=(8, 4))

        controls = tk.Frame(self.root)
        controls.pack(fill="x", padx=8, pady=(0, 4))
        self.back_button = tk.Button(controls, text="−1 s", width=7,
                                     command=lambda: self.seek_relative(-1000))
        self.back_button.pack(side="left")
        self.play_button = tk.Button(controls, text="Pause", width=9,
                                     command=self.toggle_playback)
        self.play_button.pack(side="left", padx=6)
        self.forward_button = tk.Button(controls, text="+1 s", width=7,
                                        command=lambda: self.seek_relative(1000))
        self.forward_button.pack(side="left")
        self.status = tk.Label(self.root, anchor="w")
        self.status.pack(fill="x", padx=10, pady=(0, 8))

        self.show_frame(0)
        self.start_playback()

    def _format_time(self, milliseconds: int) -> str:
        seconds = max(0, milliseconds) // 1000
        tenths = (max(0, milliseconds) % 1000) // 100
        return f"{seconds // 60:02d}:{seconds % 60:02d}.{tenths}"

    def _render(self) -> None:
        ppm = (
            f"P6\n{self.cpv.width} {self.cpv.height}\n255\n".encode("ascii")
            + self.cpv.to_rgb_bytes(self.framebuffer)
        )
        encoded = base64.b64encode(ppm).decode("ascii")
        self.photo = self.tk.PhotoImage(data=encoded, format="PPM")
        self.image_label.configure(image=self.photo)
        record = self.cpv.frames[self.current_frame]
        time_text = self._format_time(record.start_ms)
        total_text = self._format_time(self.cpv.total_duration_ms)
        self.status.configure(
            text=f"Frame {self.current_frame + 1}/{len(self.cpv.frames)}   "
                 f"{self.cpv.width}×{self.cpv.height}   {time_text} / {total_text}"
        )

    def show_frame(self, frame_number: int, seek: bool = True) -> None:
        if seek:
            self.cpv.seek_frame(frame_number, self.framebuffer)
        else:
            self.cpv.decode_frame(frame_number, self.framebuffer)
        self.current_frame = frame_number
        self.remaining_ms = self.cpv.frames[frame_number].duration_ms
        self._render()
        if self.playing:
            self._schedule_next(self.remaining_ms)

    def _schedule_next(self, delay_ms: int) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.remaining_ms = max(1, delay_ms)
        self.deadline = time.monotonic() + self.remaining_ms / 1000.0
        self.after_id = self.root.after(self.remaining_ms, self._advance)

    def _advance(self) -> None:
        self.after_id = None
        if not self.playing:
            return
        next_frame = self.current_frame + 1
        if next_frame >= len(self.cpv.frames):
            self.playing = False
            self.play_button.configure(text="Play")
            self.remaining_ms = 0
            return
        try:
            # Normal playback is sequential: decode only the next record.
            self.show_frame(next_frame, seek=False)
        except CPVError as exc:
            self.pause_playback()
            from tkinter import messagebox
            messagebox.showerror("CPV decoding error", str(exc), parent=self.root)

    def start_playback(self) -> None:
        if self.current_frame >= len(self.cpv.frames) - 1:
            self.show_frame(0)
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

    def seek_relative(self, delta_ms: int) -> None:
        current = self.cpv.frames[self.current_frame]
        target_ms = max(0, min(self.cpv.total_duration_ms - 1, current.start_ms + delta_ms))
        frame_starts = [record.start_ms for record in self.cpv.frames]
        target = max(0, bisect.bisect_right(frame_starts, target_ms) - 1)
        was_playing = self.playing
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.playing = False
        try:
            self.show_frame(target)
        except CPVError as exc:
            self.playing = False
            self.play_button.configure(text="Play")
            from tkinter import messagebox
            messagebox.showerror("CPV decoding error", str(exc), parent=self.root)
            return
        self.playing = was_playing
        if was_playing:
            self._schedule_next(self.remaining_ms)
        else:
            self.play_button.configure(text="Play")

    def close(self) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Play a CPV v1.0 video on the desktop.")
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
        player = CPVPlayer(cpv)
        player.run()
    except (OSError, CPVError, RuntimeError, struct.error) as exc:
        print(f"play.py: error: {exc}", file=sys.stderr)
        return 1
    finally:
        if cpv is not None:
            cpv.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
