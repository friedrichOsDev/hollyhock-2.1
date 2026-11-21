#!/usr/bin/env python3
"""Encode FFmpeg-readable videos into the CPV v1.0 global-palette format."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import BinaryIO, Optional

try:
    import numpy as np
except ImportError:  # Keep --help usable when NumPy is not installed.
    np = None


MAGIC = b"CPV\0"
VERSION_MAJOR = 1
VERSION_MINOR = 0

# CPV v1: 64-byte file header, 20-byte frame header, 12-byte index entry.
HEADER = struct.Struct("<4sBBHIHHHHIHHIIII20s")
FRAME_HEADER = struct.Struct("<4sBBHIII")
INDEX_HEADER = struct.Struct("<4sI")
INDEX_ENTRY = struct.Struct("<III")

MAX_WIDTH = 528
MAX_HEIGHT = 320
MAX_FPS = 30
MAX_RUN_LENGTH = 8191

FRAME_I = 0
FRAME_P = 1

OP_INDEX_LITERAL = 0
OP_INDEX_REPEAT = 1
OP_SKIP = 2
OP_RAW565 = 3

OP_NAMES = ("INDEX_LITERAL", "INDEX_REPEAT", "SKIP", "RAW565")


class ConversionError(RuntimeError):
    """Raised when input processing or CPV encoding fails."""


@dataclass
class PacketStats:
    counts: list[int]
    sizes: list[int]

    @classmethod
    def create(cls) -> "PacketStats":
        return cls([0, 0, 0, 0], [0, 0, 0, 0])

    def add(self, opcode: int, byte_count: int) -> None:
        self.counts[opcode] += 1
        self.sizes[opcode] += byte_count


def require_numpy() -> None:
    if np is None:
        raise ConversionError(
            "NumPy is required for the fast encoder. On Arch/CachyOS install it with "
            "'sudo pacman -S python-numpy', or use 'python -m pip install numpy'."
        )


def parse_fps(value: str) -> Fraction:
    try:
        fps = Fraction(value).limit_denominator(1000)
    except (ValueError, ZeroDivisionError) as exc:
        raise argparse.ArgumentTypeError(
            "fps must be a positive number, for example 10 or 29.97"
        ) from exc
    if fps <= 0 or fps > MAX_FPS:
        raise argparse.ArgumentTypeError(f"fps must be greater than 0 and no greater than {MAX_FPS}")
    if fps.numerator > 65535 or fps.denominator > 65535:
        raise argparse.ArgumentTypeError("fps cannot be represented in the CPV v1 header")
    return fps


def parse_background(value: str) -> str:
    text = value.strip().removeprefix("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError("background must be a six-digit RGB colour such as 000000")
    try:
        int(text, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("background must contain hexadecimal digits only") from exc
    return "0x" + text.upper()


def resolve_program(name: str) -> str:
    resolved = shutil.which(name) if os.path.sep not in name else name
    if not resolved or not Path(resolved).is_file():
        raise ConversionError(f"executable not found: {name}")
    return str(Path(resolved).resolve())


def frame_timestamp_ms(frame_number: int, fps: Fraction) -> int:
    numerator = frame_number * 1000 * fps.denominator
    return (numerator + fps.numerator // 2) // fps.numerator


def probe_duration_ms(ffprobe: str, source: Path) -> Optional[int]:
    commands = (
        [ffprobe, "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(source)],
        [ffprobe, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(source)],
    )
    for command in commands:
        try:
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            value = float(result.stdout.strip().splitlines()[0])
            if value > 0:
                return max(1, round(value * 1000))
        except (OSError, subprocess.CalledProcessError, ValueError, IndexError):
            continue
    return None


def make_filter(width: int, height: int, fps: Fraction, fit: str, background: str) -> str:
    filters = [f"fps={fps.numerator}/{fps.denominator}"]
    if fit == "stretch":
        filters.append(f"scale={width}:{height}:flags=lanczos")
    else:
        filters.append(
            f"scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos"
        )
        filters.append(f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color={background}")
    filters.append("setsar=1")
    return ",".join(filters)


def palette_limit_for_quality(quality: int) -> int:
    """Map quality to FFmpeg's single global palette budget."""
    if quality < 15:
        return 16
    if quality < 35:
        return 32
    if quality < 55:
        return 64
    if quality < 75:
        return 128
    return 256


def _run_checked(command: list[str], description: str) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError as exc:
        raise ConversionError(f"{description}: executable not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip()
        raise ConversionError(f"{description} failed" + (f": {detail}" if detail else "")) from exc


def build_global_palette(
    ffmpeg: str,
    source: Path,
    width: int,
    height: int,
    fit: str,
    background: str,
    quality: int,
    temp_dir: Path,
):
    """Use FFmpeg's native palettegen filter, then convert the palette to RGB565."""
    require_numpy()
    max_colours = palette_limit_for_quality(quality)
    palette_path = temp_dir / "palette.png"
    sample_filter = make_filter(width, height, Fraction(1, 1), fit, background)
    video_filter = (
        sample_filter
        + f",palettegen=max_colors={max_colours}:stats_mode=full:reserve_transparent=0"
    )
    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(source), "-map", "0:v:0", "-an", "-sn", "-dn",
        "-vf", video_filter, "-frames:v", "1", str(palette_path),
    ]
    _run_checked(command, "global palette generation")

    extract = [
        ffmpeg, "-hide_banner", "-loglevel", "error",
        "-i", str(palette_path), "-frames:v", "1",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
    ]
    raw = _run_checked(extract, "palette extraction").stdout
    if not raw or len(raw) % 3:
        raise ConversionError("FFmpeg returned an invalid palette image")

    rgb = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.uint16)
    red = (rgb[:, 0] * 31 + 127) // 255
    green = (rgb[:, 1] * 63 + 127) // 255
    blue = (rgb[:, 2] * 31 + 127) // 255
    colours = ((red << 11) | (green << 5) | blue).astype(np.uint16)

    # Keep palette order, remove duplicate entries, and ignore transparent/padded
    # palette slots which collapse to the same RGB565 value.
    _, first_indices = np.unique(colours, return_index=True)
    palette = np.ascontiguousarray(colours[np.sort(first_indices)], dtype=np.uint16)
    if palette.size == 0 or palette.size > 256:
        raise ConversionError(f"invalid global palette size: {palette.size}")
    return palette, max_colours


def build_rgb565_lookup(palette):
    """Build one 65,536-entry lookup table, avoiding a 256-colour search per pixel."""
    require_numpy()
    colours = np.arange(65536, dtype=np.uint16)
    red = ((colours >> 11) & 31).astype(np.int16)
    green = ((colours >> 5) & 63).astype(np.int16)
    blue = (colours & 31).astype(np.int16)

    best_distance = np.full(65536, np.iinfo(np.int32).max, dtype=np.int32)
    best_index = np.zeros(65536, dtype=np.uint8)

    for index, colour in enumerate(palette):
        value = int(colour)
        pr = (value >> 11) & 31
        pg = (value >> 5) & 63
        pb = value & 31
        dr = red - pr
        dg = green - pg
        db = blue - pb
        distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db
        better = distance < best_distance
        best_distance[better] = distance[better]
        best_index[better] = index

    return best_index


def rgb24_to_rgb565(raw: bytes):
    """Vectorised RGB888-to-RGB565 conversion."""
    pixels = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.uint16)
    red = (pixels[:, 0] * 31 + 127) // 255
    green = (pixels[:, 1] * 63 + 127) // 255
    blue = (pixels[:, 2] * 31 + 127) // 255
    return ((red << 11) | (green << 5) | blue).astype(np.uint16)


def map_frame(source565, palette, lookup, quality: int):
    """Return palette indices, exact reconstructed RGB565 pixels, and RAW mask."""
    indices = lookup[source565]
    palette_pixels = palette[indices]
    if quality == 100:
        # Preserve every source RGB565 value. Palette hits remain palette-coded;
        # all other pixels are stored by RAW565 packets.
        raw_mask = source565 != palette_pixels
        reconstructed = source565
    else:
        raw_mask = np.zeros(source565.size, dtype=np.bool_)
        reconstructed = palette_pixels
    return indices, reconstructed, raw_mask


def packet_header(opcode: int, run_length: int) -> bytes:
    if opcode not in (OP_INDEX_LITERAL, OP_INDEX_REPEAT, OP_SKIP, OP_RAW565):
        raise ValueError("invalid CPV packet opcode")
    if not (1 <= run_length <= MAX_RUN_LENGTH):
        raise ValueError("CPV packet run length is outside 1..8191")
    if run_length <= 32:
        return bytes(((opcode << 6) | (run_length - 1),))
    first = (opcode << 6) | 0x20 | ((run_length >> 8) & 0x1F)
    return bytes((first, run_length & 0xFF))


def packet_header_size(run_length: int) -> int:
    return 1 if run_length <= 32 else 2


def literal_cost(length: int) -> int:
    total = 0
    while length:
        part = min(length, MAX_RUN_LENGTH)
        total += packet_header_size(part) + part
        length -= part
    return total


def repeat_cost(length: int) -> int:
    full, remainder = divmod(length, MAX_RUN_LENGTH)
    total = full * 3  # 2-byte long header + one palette index.
    if remainder:
        total += packet_header_size(remainder) + 1
    return total


def emit_literal(out: bytearray, values, stats: PacketStats) -> None:
    start = 0
    length = int(values.size)
    while start < length:
        count = min(MAX_RUN_LENGTH, length - start)
        packet_start = len(out)
        out.extend(packet_header(OP_INDEX_LITERAL, count))
        out.extend(values[start:start + count].tobytes())
        stats.add(OP_INDEX_LITERAL, len(out) - packet_start)
        start += count


def emit_repeat(out: bytearray, value: int, length: int, stats: PacketStats) -> None:
    remaining = length
    while remaining:
        count = min(MAX_RUN_LENGTH, remaining)
        packet_start = len(out)
        out.extend(packet_header(OP_INDEX_REPEAT, count))
        out.append(value)
        stats.add(OP_INDEX_REPEAT, len(out) - packet_start)
        remaining -= count


def emit_skip(out: bytearray, length: int, stats: PacketStats) -> None:
    remaining = length
    while remaining:
        count = min(MAX_RUN_LENGTH, remaining)
        packet_start = len(out)
        out.extend(packet_header(OP_SKIP, count))
        stats.add(OP_SKIP, len(out) - packet_start)
        remaining -= count


def emit_raw(out: bytearray, values, stats: PacketStats) -> None:
    start = 0
    length = int(values.size)
    while start < length:
        count = min(MAX_RUN_LENGTH, length - start)
        packet_start = len(out)
        out.extend(packet_header(OP_RAW565, count))
        out.extend(np.asarray(values[start:start + count], dtype="<u2").tobytes())
        stats.add(OP_RAW565, len(out) - packet_start)
        start += count


def encode_palette_segment(values, out: bytearray, stats: PacketStats) -> None:
    """Choose between one/more literal packets and repeat packets plus literals."""
    length = int(values.size)
    if length == 0:
        return

    changes = np.flatnonzero(values[1:] != values[:-1]) + 1
    boundaries = np.empty(changes.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = changes
    boundaries[-1] = length

    run_lengths = np.diff(boundaries)
    repeated = run_lengths >= 2

    if not np.any(repeated):
        emit_literal(out, values, stats)
        return

    # Cost of a plan that uses INDEX_REPEAT for repeated runs and groups
    # adjacent singleton runs into INDEX_LITERAL packets.
    group_changes = np.flatnonzero(repeated[1:] != repeated[:-1]) + 1
    group_boundaries = np.empty(group_changes.size + 2, dtype=np.int64)
    group_boundaries[0] = 0
    group_boundaries[1:-1] = group_changes
    group_boundaries[-1] = run_lengths.size

    repeated_lengths = run_lengths[repeated].astype(np.int64)
    full_chunks = repeated_lengths // MAX_RUN_LENGTH
    remainders = repeated_lengths % MAX_RUN_LENGTH
    repeated_costs = full_chunks * 3
    has_remainder = remainders > 0
    if np.any(has_remainder):
        rem = remainders[has_remainder]
        repeated_costs[has_remainder] += np.where(rem <= 32, 2, 3)
    run_plan_cost = int(repeated_costs.sum())

    for group in range(group_boundaries.size - 1):
        first_run = int(group_boundaries[group])
        after_run = int(group_boundaries[group + 1])
        if not repeated[first_run]:
            pixel_length = int(boundaries[after_run] - boundaries[first_run])
            run_plan_cost += literal_cost(pixel_length)

    all_literal_cost = literal_cost(length)
    # Avoid generating thousands of tiny repeat packets for a negligible gain.
    saving_threshold = max(4, all_literal_cost // 100)
    if run_plan_cost + saving_threshold >= all_literal_cost:
        emit_literal(out, values, stats)
        return

    for group in range(group_boundaries.size - 1):
        first_run = int(group_boundaries[group])
        after_run = int(group_boundaries[group + 1])
        pixel_start = int(boundaries[first_run])
        pixel_end = int(boundaries[after_run])
        if repeated[first_run]:
            for run in range(first_run, after_run):
                run_start = int(boundaries[run])
                run_length = int(run_lengths[run])
                emit_repeat(out, int(values[run_start]), run_length, stats)
        else:
            emit_literal(out, values[pixel_start:pixel_end], stats)


def encode_frame_payload(indices, reconstructed, raw_mask, previous, frame_type: int):
    """Encode a complete I/P frame using linear status runs and packet payloads."""
    pixel_count = int(reconstructed.size)
    status = np.zeros(pixel_count, dtype=np.uint8)  # 0 palette, 1 raw, 2 skip
    if np.any(raw_mask):
        status[raw_mask] = 1
    if frame_type == FRAME_P:
        unchanged = reconstructed == previous
        status[unchanged] = 2
        status[(~unchanged) & raw_mask] = 1
        status[(~unchanged) & (~raw_mask)] = 0
    elif frame_type != FRAME_I:
        raise ValueError("invalid CPV frame type")

    transitions = np.flatnonzero(status[1:] != status[:-1]) + 1
    boundaries = np.empty(transitions.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = transitions
    boundaries[-1] = pixel_count

    payload = bytearray()
    stats = PacketStats.create()
    for segment in range(boundaries.size - 1):
        start = int(boundaries[segment])
        end = int(boundaries[segment + 1])
        kind = int(status[start])
        if kind == 2:
            emit_skip(payload, end - start, stats)
        elif kind == 1:
            emit_raw(payload, reconstructed[start:end], stats)
        else:
            encode_palette_segment(indices[start:end], payload, stats)

    if frame_type == FRAME_I and stats.counts[OP_SKIP]:
        raise AssertionError("encoder generated SKIP in an I-frame")
    return bytes(payload), stats


def read_exact(stream: BinaryIO, size: int) -> Optional[bytes]:
    chunks = bytearray()
    while len(chunks) < size:
        data = stream.read(size - len(chunks))
        if not data:
            if not chunks:
                return None
            raise ConversionError(f"FFmpeg returned a truncated frame ({len(chunks)} of {size} bytes)")
        chunks.extend(data)
    return bytes(chunks)


def write_u32_at(stream: BinaryIO, offset: int, value: int) -> None:
    if not (0 <= value <= 0xFFFFFFFF):
        raise ConversionError("duration exceeds the CPV v1 uint32 limit")
    end = stream.tell()
    stream.seek(offset)
    stream.write(struct.pack("<I", value))
    stream.seek(end)


def write_frame_record(
    output: BinaryIO,
    payload: bytes,
    frame_type: int,
    frame_number: int,
    duration_ms: int,
) -> int:
    if not (1 <= duration_ms <= 0xFFFFFFFF):
        raise ConversionError("frame duration must fit in a non-zero uint32")
    if len(payload) > 0xFFFFFFFF:
        raise ConversionError("frame payload exceeds the CPV uint32 limit")
    offset = output.tell()
    if offset > 0xFFFFFFFF or len(payload) + FRAME_HEADER.size > 0xFFFFFFFF - offset:
        raise ConversionError("output exceeds the CPV v1 4 GiB file-offset limit")
    output.write(FRAME_HEADER.pack(
        b"FRM1", frame_type, 0, FRAME_HEADER.size,
        frame_number, duration_ms, len(payload),
    ))
    output.write(payload)
    return offset


def convert(args: argparse.Namespace) -> None:
    require_numpy()
    source = Path(args.input).expanduser().resolve()
    output_path = Path(args.output).expanduser().resolve()
    if not source.is_file():
        raise ConversionError(f"input file does not exist: {source}")
    if source == output_path:
        raise ConversionError("input and output paths must be different")
    if not (1 <= args.width <= MAX_WIDTH and 1 <= args.height <= MAX_HEIGHT):
        raise ConversionError(f"width and height must be within 1..{MAX_WIDTH} and 1..{MAX_HEIGHT}")
    if not (0 <= args.quality <= 100):
        raise ConversionError("quality must be between 0 and 100")
    if not (0 < args.keyframe_interval <= 10):
        raise ConversionError("keyframe interval must be greater than 0 and no greater than 10 seconds")

    ffmpeg = resolve_program(args.ffmpeg)
    ffprobe = resolve_program(args.ffprobe)
    source_duration_ms = probe_duration_ms(ffprobe, source)
    if source_duration_ms is None:
        print(
            "Warning: ffprobe could not determine the source duration; "
            "the final frame will use its nominal duration.",
            file=sys.stderr,
        )

    fps: Fraction = args.fps
    video_filter = make_filter(args.width, args.height, fps, args.fit, args.background)
    frame_bytes = args.width * args.height * 3
    pixel_count = args.width * args.height
    keyframe_interval_ms = max(1, round(args.keyframe_interval * 1000))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    total_packet_stats = PacketStats.create()
    index_entries: list[tuple[int, int, int]] = []
    process: Optional[subprocess.Popen[bytes]] = None
    stderr_file = tempfile.TemporaryFile(mode="w+b")
    temp_output_name: Optional[str] = None

    try:
        with tempfile.TemporaryDirectory(prefix="cpv_palette_") as palette_dir:
            palette, palette_limit = build_global_palette(
                ffmpeg, source, args.width, args.height, args.fit,
                args.background, args.quality, Path(palette_dir),
            )
            lookup = build_rgb565_lookup(palette)
            print(
                f"Global palette: {palette.size}/{palette_limit} colours "
                f"(quality {args.quality}/100)"
            )

            ffmpeg_command = [
                ffmpeg, "-hide_banner", "-loglevel", "error",
                "-i", str(source), "-map", "0:v:0", "-vf", video_filter,
                "-an", "-sn", "-dn", "-f", "rawvideo",
                "-pix_fmt", "rgb24", "pipe:1",
            ]

            with tempfile.NamedTemporaryFile(
                mode="w+b",
                prefix=output_path.name + ".",
                suffix=".tmp",
                dir=str(output_path.parent),
                delete=False,
            ) as output:
                temp_output_name = output.name
                output.write(b"\0" * HEADER.size)
                output.write(np.asarray(palette, dtype="<u2").tobytes())
                frames_offset = output.tell()

                process = subprocess.Popen(
                    ffmpeg_command,
                    stdout=subprocess.PIPE,
                    stderr=stderr_file,
                )
                assert process.stdout is not None

                input_frame_number = 0
                stored_frame_count = 0
                previous_reconstruction = None
                last_keyframe_start_ms: Optional[int] = None
                last_frame_offset: Optional[int] = None
                last_frame_duration = 0
                last_input_start_ms = 0
                last_input_duration = 0
                total_duration_ms = 0

                while True:
                    raw = read_exact(process.stdout, frame_bytes)
                    if raw is None:
                        break

                    start_ms = frame_timestamp_ms(input_frame_number, fps)
                    next_ms = frame_timestamp_ms(input_frame_number + 1, fps)
                    duration_ms = max(1, next_ms - start_ms)
                    last_input_start_ms = start_ms
                    last_input_duration = duration_ms

                    source565 = rgb24_to_rgb565(raw)
                    indices, reconstructed, raw_mask = map_frame(
                        source565, palette, lookup, args.quality
                    )

                    identical = (
                        previous_reconstruction is not None
                        and np.array_equal(reconstructed, previous_reconstruction)
                    )

                    if identical:
                        if last_frame_offset is None:
                            raise AssertionError("missing previous stored frame")
                        last_frame_duration += duration_ms
                        if last_frame_duration > 0xFFFFFFFF:
                            raise ConversionError("coalesced frame duration exceeds uint32")
                        write_u32_at(output, last_frame_offset + 12, last_frame_duration)
                    else:
                        is_keyframe = (
                            previous_reconstruction is None
                            or last_keyframe_start_ms is None
                            or start_ms - last_keyframe_start_ms >= keyframe_interval_ms
                        )
                        frame_type = FRAME_I if is_keyframe else FRAME_P
                        payload, frame_stats = encode_frame_payload(
                            indices,
                            reconstructed,
                            raw_mask,
                            previous_reconstruction,
                            frame_type,
                        )
                        frame_offset = write_frame_record(
                            output, payload, frame_type, stored_frame_count, duration_ms
                        )
                        if frame_type == FRAME_I:
                            index_entries.append((stored_frame_count, start_ms, frame_offset))
                            last_keyframe_start_ms = start_ms
                        for opcode in range(4):
                            total_packet_stats.counts[opcode] += frame_stats.counts[opcode]
                            total_packet_stats.sizes[opcode] += frame_stats.sizes[opcode]
                        stored_frame_count += 1
                        last_frame_offset = frame_offset
                        last_frame_duration = duration_ms
                        previous_reconstruction = reconstructed

                    total_duration_ms += duration_ms
                    if total_duration_ms > 0xFFFFFFFF:
                        raise ConversionError("video duration exceeds the CPV uint32 limit")

                    input_frame_number += 1
                    if input_frame_number % 50 == 0:
                        print(
                            f"\rRead {input_frame_number} frames; "
                            f"stored {stored_frame_count}",
                            end="", file=sys.stderr, flush=True,
                        )

                process.stdout.close()
                return_code = process.wait()
                stderr_file.seek(0)
                ffmpeg_error = stderr_file.read().decode("utf-8", errors="replace").strip()
                if return_code != 0:
                    raise ConversionError(
                        "FFmpeg video decoding failed" +
                        (f": {ffmpeg_error}" if ffmpeg_error else "")
                    )
                process = None

                if input_frame_number == 0 or stored_frame_count == 0 or last_frame_offset is None:
                    raise ConversionError("FFmpeg produced no video frames")

                # Use the source duration for the final displayed frame when available.
                nominal_end = frame_timestamp_ms(input_frame_number, fps)
                desired_end = (
                    source_duration_ms
                    if source_duration_ms is not None and source_duration_ms > last_input_start_ms
                    else nominal_end
                )
                desired_last_duration = max(1, desired_end - last_input_start_ms)
                duration_adjustment = desired_last_duration - last_input_duration
                if duration_adjustment:
                    last_frame_duration += duration_adjustment
                    if last_frame_duration <= 0 or last_frame_duration > 0xFFFFFFFF:
                        raise ConversionError("final frame duration is outside the CPV uint32 range")
                    write_u32_at(output, last_frame_offset + 12, last_frame_duration)
                    total_duration_ms += duration_adjustment

                if total_duration_ms <= 0 or total_duration_ms > 0xFFFFFFFF:
                    raise ConversionError("total duration is outside the CPV v1 uint32 range")

                index_offset = output.tell()
                if index_offset > 0xFFFFFFFF:
                    raise ConversionError("output exceeds the CPV v1 4 GiB file-offset limit")
                output.write(INDEX_HEADER.pack(b"IDX1", len(index_entries)))
                for frame_number, elapsed_ms, file_offset in index_entries:
                    output.write(INDEX_ENTRY.pack(frame_number, elapsed_ms, file_offset))

                if output.tell() > 0xFFFFFFFF:
                    raise ConversionError("output exceeds the CPV v1 4 GiB file-offset limit")

                output.seek(0)
                output.write(HEADER.pack(
                    MAGIC,
                    VERSION_MAJOR,
                    VERSION_MINOR,
                    HEADER.size,
                    0,
                    args.width,
                    args.height,
                    fps.numerator,
                    fps.denominator,
                    stored_frame_count,
                    int(palette.size),
                    0,
                    64,
                    frames_offset,
                    index_offset,
                    total_duration_ms,
                    bytes(20),
                ))
                output.flush()
                os.fsync(output.fileno())

        os.replace(temp_output_name, output_path)
        temp_output_name = None

        if input_frame_number % 50:
            print(file=sys.stderr)
        seconds = total_duration_ms / 1000.0
        size = output_path.stat().st_size
        print(
            f"Created {output_path}\n"
            f"  Resolution: {args.width}x{args.height}\n"
            f"  Frame rate: {fps.numerator}/{fps.denominator} FPS\n"
            f"  Input frames: {input_frame_number}\n"
            f"  Stored frames: {stored_frame_count}\n"
            f"  Keyframes: {len(index_entries)}\n"
            f"  Duration: {seconds:.3f} s\n"
            f"  Palette: {palette.size} colours\n"
            f"  Quality: {args.quality}/100\n"
            f"  File size: {size:,} bytes ({size / (1024 * 1024):.2f} MiB)\n"
            f"  Conversion speed: {seconds:.2f} source seconds processed\n"
            f"  Packets: "
            + ", ".join(
                f"{OP_NAMES[i]}={total_packet_stats.counts[i]} "
                f"({total_packet_stats.sizes[i]:,} bytes)"
                for i in range(4)
            )
        )
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        stderr_file.close()
        if temp_output_name is not None:
            try:
                os.unlink(temp_output_name)
            except OSError:
                pass


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert an FFmpeg-readable video into the CPV v1.0 format."
    )
    parser.add_argument("input", help="input video file supported by FFmpeg")
    parser.add_argument("--output", required=True, help="output .cpv file")
    parser.add_argument("--width", required=True, type=int, help="output width (1..528)")
    parser.add_argument("--height", required=True, type=int, help="output height (1..320)")
    parser.add_argument(
        "--fps", required=True, type=parse_fps,
        help="output frame rate (greater than 0 and no greater than 30; normally 10)",
    )
    parser.add_argument(
        "--quality", type=int, choices=range(0, 101), default=75,
        help="global palette quality: 0 (16 colours) to 100 (RGB565-preserving RAW fallback; default: 75)",
    )
    parser.add_argument(
        "--keyframe-interval", type=float, default=2.0,
        help="target keyframe interval in seconds (default: 2.0; maximum: 10.0)",
    )
    parser.add_argument(
        "--fit", choices=("contain", "stretch"), default="contain",
        help="preserve aspect ratio with letterboxing, or stretch to exact dimensions (default: contain)",
    )
    parser.add_argument(
        "--background", type=parse_background, default="0x000000",
        help="letterbox background as RRGGBB (default: 000000)",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg", help="FFmpeg executable (default: ffmpeg)")
    parser.add_argument("--ffprobe", default="ffprobe", help="FFprobe executable (default: ffprobe)")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.keyframe_interval <= 0 or args.keyframe_interval > 10:
        parser.error("--keyframe-interval must be greater than 0 and no greater than 10")
    try:
        convert(args)
    except (OSError, ConversionError, ValueError, struct.error, subprocess.SubprocessError) as exc:
        print(f"convert.py: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
