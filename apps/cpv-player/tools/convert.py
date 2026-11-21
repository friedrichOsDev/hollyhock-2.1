#!/usr/bin/env python3
"""Convert an FFmpeg-readable video into the CPV v1.0 format."""

from __future__ import annotations

import argparse
import array
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction
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


def frame_timestamp_ms(frame_number: int, fps: Fraction) -> int:
    """Return a rounded, drift-free presentation timestamp."""
    numerator = frame_number * 1000 * fps.denominator
    return (numerator + fps.numerator // 2) // fps.numerator


def rgb24_to_rgb565(raw: bytes) -> array.array:
    pixels = array.array("H")
    append = pixels.append
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        r5 = (r * 31 + 127) // 255
        g6 = (g * 63 + 127) // 255
        b5 = (b * 31 + 127) // 255
        append((r5 << 11) | (g6 << 5) | b5)
    return pixels


def extract_tile(frame: array.array, width: int, height: int,
                 x: int, y: int, size: int) -> tuple[array.array, int, int]:
    visible_width = min(size, width - x)
    visible_height = min(size, height - y)
    values = array.array("H")
    for row in range(visible_height):
        start = (y + row) * width + x
        values.extend(frame[start:start + visible_width])
    return values, visible_width, visible_height


def write_tile(destination: array.array, width: int, x: int, y: int,
               visible_width: int, visible_height: int,
               values: array.array) -> None:
    for row in range(visible_height):
        src = row * visible_width
        dst = (y + row) * width + x
        destination[dst:dst + visible_width] = values[src:src + visible_width]


def pack_rgb565(values: array.array) -> bytes:
    # struct guarantees the file's little-endian byte order on every host.
    return struct.pack("<" + "H" * len(values), *values)


def colour_components(value: int) -> tuple[int, int, int]:
    return (value >> 11) & 31, (value >> 5) & 63, value & 31


def colour_distance(a: int, b: int) -> int:
    ar, ag, ab = colour_components(a)
    br, bg, bb = colour_components(b)
    dr, dg, db = ar - br, ag - bg, ab - bb
    return 3 * dr * dr + 4 * dg * dg + 2 * db * db


def tile_distortion(source: array.array, decoded: array.array) -> int:
    return sum(colour_distance(a, b) for a, b in zip(source, decoded))


def quality_palette_limit(quality: int) -> int:
    """Map the user quality control to an encoder-side palette budget."""
    if quality >= 95:
        return 256
    if quality >= 80:
        return 128
    if quality >= 65:
        return 64
    if quality >= 50:
        return 32
    if quality >= 35:
        return 16
    if quality >= 20:
        return 8
    return 4


def median_cut_palette(values: array.array, maximum_colours: int) -> list[int]:
    """Build a deterministic weighted median-cut palette in RGB565 space."""
    histogram: dict[int, int] = {}
    for value in values:
        histogram[value] = histogram.get(value, 0) + 1

    points = [(value, count, colour_components(value))
              for value, count in histogram.items()]
    boxes: list[list[tuple[int, int, tuple[int, int, int]]]] = [points]

    while len(boxes) < maximum_colours:
        best_index = -1
        best_score = -1
        best_channel = 0
        for box_index, box in enumerate(boxes):
            if len(box) < 2:
                continue
            ranges = [max(p[2][c] for p in box) - min(p[2][c] for p in box)
                      for c in range(3)]
            channel = max(range(3), key=lambda c: (ranges[c] * (4 if c == 1 else 3 if c == 0 else 2), -c))
            score = ranges[channel] * sum(p[1] for p in box)
            if score > best_score:
                best_score = score
                best_index = box_index
                best_channel = channel
        if best_index < 0:
            break

        box = boxes.pop(best_index)
        box.sort(key=lambda p: (p[2][best_channel], p[0]))
        total_weight = sum(p[1] for p in box)
        half_weight = (total_weight + 1) // 2
        accumulated = 0
        split_at = 1
        for i, point in enumerate(box[:-1], start=1):
            accumulated += point[1]
            if accumulated >= half_weight:
                split_at = i
                break
        boxes.insert(best_index, box[split_at:])
        boxes.insert(best_index, box[:split_at])

    palette: list[int] = []
    for box in boxes:
        weight = sum(point[1] for point in box)
        r = (sum(point[2][0] * point[1] for point in box) + weight // 2) // weight
        g = (sum(point[2][1] * point[1] for point in box) + weight // 2) // weight
        b = (sum(point[2][2] * point[1] for point in box) + weight // 2) // weight
        palette.append((r << 11) | (g << 5) | b)

    # Median-cut boxes can occasionally produce the same rounded centroid.
    return sorted(set(palette))


def encode_palette(values: array.array, quality: int) -> Optional[tuple[bytes, array.array]]:
    unique = sorted(set(values))
    if len(unique) < 2:
        return None  # SOLID is smaller.

    maximum = quality_palette_limit(quality)
    if len(unique) > maximum:
        if quality == 100:
            return None  # Quality 100 never adds quantisation beyond RGB565.
        palette = median_cut_palette(values, maximum)
    else:
        palette = unique

    if len(palette) < 2 or len(palette) > 256 or len(palette) > len(values):
        return None

    palette_lookup = {colour: index for index, colour in enumerate(palette)}
    if len(palette_lookup) == len(unique) and all(c in palette_lookup for c in unique):
        indices = [palette_lookup[value] for value in values]
    else:
        # Cache nearest-palette searches by source colour.
        nearest_cache: dict[int, int] = {}
        for source_colour in unique:
            nearest_cache[source_colour] = min(
                range(len(palette)),
                key=lambda i: (colour_distance(source_colour, palette[i]), i),
            )
        indices = [nearest_cache[value] for value in values]

    bits_per_index = (len(palette) - 1).bit_length()
    packed = bytearray()
    accumulator = 0
    bits_in_accumulator = 0
    for index in indices:
        accumulator |= index << bits_in_accumulator
        bits_in_accumulator += bits_per_index
        while bits_in_accumulator >= 8:
            packed.append(accumulator & 0xFF)
            accumulator >>= 8
            bits_in_accumulator -= 8
    if bits_in_accumulator:
        packed.append(accumulator & 0xFF)

    encoded = bytearray((MODE_PALETTE, len(palette) - 1))
    for colour in palette:
        encoded.extend(struct.pack("<H", colour))
    encoded.extend(packed)
    decoded = array.array("H", (palette[index] for index in indices))
    return bytes(encoded), decoded


def encode_rle565(values: array.array) -> bytes:
    output = bytearray((MODE_RLE565,))
    length = len(values)
    i = 0

    def run_length(position: int) -> int:
        value = values[position]
        end = position + 1
        limit = min(length, position + 128)
        while end < limit and values[end] == value:
            end += 1
        return end - position

    while i < length:
        run = run_length(i)
        if run >= 2:
            output.append(0x80 | (run - 1))
            output.extend(struct.pack("<H", values[i]))
            i += run
            continue

        literal_start = i
        i += 1
        while i < length and i - literal_start < 128:
            if run_length(i) >= 2:
                break
            i += 1
        literal_count = i - literal_start
        output.append(literal_count - 1)
        output.extend(pack_rgb565(values[literal_start:i]))

    return bytes(output)


def encode_node(current: array.array, previous: Optional[array.array],
                width: int, height: int, x: int, y: int, size: int,
                frame_type: int, quality: int) -> tuple[bytes, array.array]:
    values, visible_width, visible_height = extract_tile(current, width, height, x, y, size)

    if frame_type == FRAME_P and previous is not None:
        old_values, _, _ = extract_tile(previous, width, height, x, y, size)
        if values == old_values:
            return bytes((MODE_SKIP,)), old_values

    candidates: list[tuple[bytes, array.array, int]] = []

    if all(value == values[0] for value in values):
        solid = bytes((MODE_SOLID,)) + struct.pack("<H", values[0])
        candidates.append((solid, values, 0))

    palette_candidate = encode_palette(values, quality)
    if palette_candidate is not None:
        encoded, decoded = palette_candidate
        candidates.append((encoded, decoded, tile_distortion(values, decoded)))

    rle = encode_rle565(values)
    candidates.append((rle, values, 0))

    raw = bytes((MODE_RAW565,)) + pack_rgb565(values)
    candidates.append((raw, values, 0))

    if size > MIN_TILE_SIZE:
        half = size // 2
        split_data = bytearray((MODE_SPLIT,))
        split_pixels = array.array("H", [0]) * (visible_width * visible_height)
        children = (
            (x, y),
            (x + half, y),
            (x, y + half),
            (x + half, y + half),
        )
        for child_x, child_y in children:
            if child_x >= width or child_y >= height:
                continue
            child_data, child_pixels = encode_node(
                current, previous, width, height,
                child_x, child_y, half, frame_type, quality,
            )
            split_data.extend(child_data)
            child_width = min(half, width - child_x)
            child_height = min(half, height - child_y)
            local_x = child_x - x
            local_y = child_y - y
            for row in range(child_height):
                src = row * child_width
                dst = (local_y + row) * visible_width + local_x
                split_pixels[dst:dst + child_width] = child_pixels[src:src + child_width]

        split_distortion = tile_distortion(values, split_pixels)
        candidates.append((bytes(split_data), split_pixels, split_distortion))

    # Prefer the smallest representation; on size ties prefer lower distortion.
    best_data, best_pixels, _ = min(candidates, key=lambda item: (len(item[0]), item[2]))
    return best_data, best_pixels


def parse_background(value: str) -> str:
    text = value.strip().removeprefix("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError("background must be a six-digit RGB colour, e.g. 000000")
    try:
        int(text, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("background must contain hexadecimal digits only") from exc
    return "0x" + text.upper()


def parse_fps(value: str) -> Fraction:
    try:
        fps = Fraction(value).limit_denominator(10000)
    except (ValueError, ZeroDivisionError) as exc:
        raise argparse.ArgumentTypeError("fps must be a positive number, e.g. 10 or 29.97") from exc
    if fps <= 0 or fps > 60:
        raise argparse.ArgumentTypeError("fps must be greater than 0 and no greater than 60")
    if fps.numerator > 65535 or fps.denominator > 65535:
        raise argparse.ArgumentTypeError("fps cannot be represented in the CPV header")
    return fps


def probe_duration_ms(ffprobe: str, source: Path) -> Optional[int]:
    # Prefer the video stream duration so a longer audio track cannot extend
    # the final CPV frame. Fall back to container duration when unavailable.
    commands = (
        [ffprobe, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(source)],
        [ffprobe, "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(source)],
    )
    for command in commands:
        try:
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            duration = float(result.stdout.strip())
            if duration > 0:
                return max(1, round(duration * 1000))
        except (OSError, subprocess.CalledProcessError, ValueError):
            continue
    return None


def make_filter(width: int, height: int, fps: Fraction,
                fit: str, background: str) -> str:
    filters = [f"fps={fps.numerator}/{fps.denominator}"]
    if fit == "stretch":
        filters.append(f"scale={width}:{height}:flags=lanczos")
    else:
        filters.append(
            f"scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos"
        )
        filters.append(
            f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color={background}"
        )
    filters.append("setsar=1")
    return ",".join(filters)


def read_raw_frame(stream: BinaryIO, frame_size: int) -> Optional[bytes]:
    data = bytearray()
    while len(data) < frame_size:
        chunk = stream.read(min(1024 * 1024, frame_size - len(data)))
        if not chunk:
            break
        data.extend(chunk)
    if not data:
        return None
    if len(data) != frame_size:
        raise RuntimeError(
            f"FFmpeg returned a truncated raw frame ({len(data)} of {frame_size} bytes)"
        )
    return bytes(data)


def write_encoded_frame(output: BinaryIO, frame: array.array,
                        previous_reconstruction: Optional[array.array],
                        width: int, height: int, frame_number: int,
                        start_ms: int, duration_ms: int, is_keyframe: bool,
                        quality: int) -> tuple[array.array, Optional[tuple[int, int, int]]]:
    frame_type = FRAME_I if is_keyframe else FRAME_P
    reconstructed = array.array("H", [0]) * (width * height)
    payload = bytearray()

    for y in range(0, height, ROOT_TILE_SIZE):
        for x in range(0, width, ROOT_TILE_SIZE):
            node_data, node_pixels = encode_node(
                frame, previous_reconstruction, width, height,
                x, y, ROOT_TILE_SIZE, frame_type, quality,
            )
            payload.extend(node_data)
            tile_width = min(ROOT_TILE_SIZE, width - x)
            tile_height = min(ROOT_TILE_SIZE, height - y)
            write_tile(reconstructed, width, x, y, tile_width, tile_height, node_pixels)

    if len(payload) > 0xFFFFFFFF:
        raise RuntimeError("encoded frame payload exceeds the CPV uint32 limit")

    frame_offset = output.tell()
    output.write(FRAME_HEADER.pack(
        frame_type, 0, FRAME_HEADER.size, duration_ms,
        len(payload), frame_number,
    ))
    output.write(payload)

    index_entry = (frame_number, start_ms, frame_offset) if is_keyframe else None
    return reconstructed, index_entry


def convert(args: argparse.Namespace) -> None:
    source = Path(args.input).expanduser().resolve()
    output_path = Path(args.output).expanduser().resolve()
    if not source.is_file():
        raise RuntimeError(f"input file does not exist: {source}")
    if source == output_path:
        raise RuntimeError("input and output paths must be different")
    if not (1 <= args.width <= MAX_WIDTH and 1 <= args.height <= MAX_HEIGHT):
        raise RuntimeError(f"width and height must be within 1..{MAX_WIDTH} and 1..{MAX_HEIGHT}")

    ffmpeg = shutil.which(args.ffmpeg) if os.path.sep not in args.ffmpeg else args.ffmpeg
    ffprobe = shutil.which(args.ffprobe) if os.path.sep not in args.ffprobe else args.ffprobe
    if not ffmpeg or not Path(ffmpeg).exists():
        raise RuntimeError(f"FFmpeg executable not found: {args.ffmpeg}")

    duration_ms = probe_duration_ms(ffprobe or args.ffprobe, source) if ffprobe else None
    if duration_ms is None:
        print("Warning: ffprobe duration unavailable; output duration will use the nominal frame rate.", file=sys.stderr)

    fps: Fraction = args.fps
    frame_size = args.width * args.height * 3
    video_filter = make_filter(args.width, args.height, fps, args.fit, args.background)
    command = [
        ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
        "-i", str(source), "-map", "0:v:0", "-vf", video_filter,
        "-an", "-sn", "-dn", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    keyframe_interval_ms = max(1, round(args.keyframe_interval * 1000))
    index_entries: list[tuple[int, int, int]] = []
    previous_reconstruction: Optional[array.array] = None
    last_keyframe_ms = 0
    total_duration = 0
    frame_count = 0

    temp_name: Optional[str] = None
    process: Optional[subprocess.Popen] = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w+b", prefix=output_path.name + ".", suffix=".tmp",
            dir=str(output_path.parent), delete=False,
        ) as output:
            temp_name = output.name
            output.write(b"\0" * HEADER.size)
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            assert process.stdout is not None

            pending_frame: Optional[array.array] = None
            pending_number = 0
            pending_start = 0

            while True:
                raw = read_raw_frame(process.stdout, frame_size)
                if raw is None:
                    break
                decoded = rgb24_to_rgb565(raw)

                if pending_frame is not None:
                    next_start = frame_timestamp_ms(frame_count, fps)
                    frame_duration = max(1, next_start - pending_start)
                    is_keyframe = (
                        previous_reconstruction is None
                        or pending_start - last_keyframe_ms >= keyframe_interval_ms
                    )
                    reconstruction, entry = write_encoded_frame(
                        output, pending_frame, previous_reconstruction,
                        args.width, args.height, pending_number,
                        pending_start, frame_duration, is_keyframe, args.quality,
                    )
                    if entry is not None:
                        index_entries.append(entry)
                        last_keyframe_ms = pending_start
                    previous_reconstruction = reconstruction
                    total_duration += frame_duration
                    if total_duration > 0xFFFFFFFF:
                        raise RuntimeError("video duration exceeds the CPV uint32 limit")

                pending_frame = decoded
                pending_number = frame_count
                pending_start = frame_timestamp_ms(frame_count, fps)
                frame_count += 1
                if frame_count % 10 == 0:
                    print(f"\rRead {frame_count} frames...", end="", file=sys.stderr, flush=True)

            process.stdout.close()
            stderr_output = process.stderr.read() if process.stderr else b""
            return_code = process.wait()
            if return_code != 0:
                detail = stderr_output.decode("utf-8", errors="replace").strip()
                raise RuntimeError("FFmpeg failed" + (f": {detail}" if detail else ""))
            if frame_count == 0 or pending_frame is None:
                raise RuntimeError("FFmpeg produced no video frames")

            nominal_end = frame_timestamp_ms(frame_count, fps)
            last_duration = nominal_end - pending_start
            if duration_ms is not None and duration_ms > pending_start:
                last_duration = duration_ms - pending_start
            last_duration = max(1, last_duration)
            is_keyframe = (
                previous_reconstruction is None
                or pending_start - last_keyframe_ms >= keyframe_interval_ms
            )
            reconstruction, entry = write_encoded_frame(
                output, pending_frame, previous_reconstruction,
                args.width, args.height, pending_number,
                pending_start, last_duration, is_keyframe, args.quality,
            )
            if entry is not None:
                index_entries.append(entry)
            total_duration += last_duration
            if total_duration > 0xFFFFFFFF:
                raise RuntimeError("video duration exceeds the CPV uint32 limit")

            index_offset = output.tell()
            output.write(INDEX_HEADER.pack(b"IDX1", len(index_entries)))
            for frame_number, elapsed_ms, file_offset in index_entries:
                output.write(INDEX_ENTRY.pack(frame_number, elapsed_ms, file_offset))

            output.seek(0)
            output.write(HEADER.pack(
                MAGIC, VERSION_MAJOR, VERSION_MINOR, HEADER.size,
                args.width, args.height, fps.numerator, fps.denominator,
                frame_count, 0, index_offset, total_duration,
            ))
            output.flush()
            os.fsync(output.fileno())

        os.replace(temp_name, output_path)
        temp_name = None
        if frame_count % 10:
            print(file=sys.stderr)
        print(
            f"Created {output_path}\n"
            f"  Resolution: {args.width}x{args.height}\n"
            f"  Frame rate: {fps.numerator}/{fps.denominator} FPS\n"
            f"  Frames: {frame_count}\n"
            f"  Duration: {total_duration / 1000:.3f} s\n"
            f"  Keyframes: {len(index_entries)}\n"
            f"  Quality: {args.quality}/100\n"
            f"  File size: {output_path.stat().st_size:,} bytes"
        )
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except OSError:
                pass


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert an FFmpeg-compatible video into a CPV v1.0 file."
    )
    parser.add_argument("input", help="input video file supported by FFmpeg")
    parser.add_argument("--output", required=True, help="output .cpv file")
    parser.add_argument("--width", required=True, type=int, help="output width (1..528)")
    parser.add_argument("--height", required=True, type=int, help="output height (1..320)")
    parser.add_argument("--fps", required=True, type=parse_fps, help="output frame rate (0..60; normally 10)")
    parser.add_argument(
        "--quality", type=int, choices=range(0, 101), default=75,
        help="lossy palette budget: 0 (smallest palettes) to 100 (no extra quantisation; default: 75)",
    )
    parser.add_argument(
        "--keyframe-interval", type=float, default=1.0,
        help="target maximum interval between keyframes in seconds (default: 1.0)",
    )
    parser.add_argument(
        "--fit", choices=("contain", "stretch"), default="contain",
        help="preserve aspect ratio with letterboxing, or stretch to the exact output size (default: contain)",
    )
    parser.add_argument(
        "--background", type=parse_background, default="000000",
        help="letterbox background as RRGGBB (default: 000000)",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg", help="FFmpeg executable (default: ffmpeg)")
    parser.add_argument("--ffprobe", default="ffprobe", help="FFprobe executable (default: ffprobe)")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.keyframe_interval <= 0:
        parser.error("--keyframe-interval must be greater than zero")
    try:
        convert(args)
    except (OSError, RuntimeError, ValueError, struct.error) as exc:
        print(f"convert.py: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
