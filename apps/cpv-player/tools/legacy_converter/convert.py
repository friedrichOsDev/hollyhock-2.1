#!/usr/bin/env python3
"""Python 1:1 port of the original cp400-cpv-player/src/pc/main.c converter.

This writes the legacy CPV format (magic "CP"), not the newer CPV v1.x format.
Only the output-writing and error-handling mechanics are modernised; dimensions,
palette generation, RGB565 conversion, colour-distance calculation, FFmpeg
filters, RLE packets, initial reference buffer, and binary byte order follow
the original C implementation.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import BinaryIO

try:
    import numpy as np
except ImportError:  # Report a friendly error from main().
    np = None


LEGACY_HEADER = struct.Struct("<2sBBBB")
PALETTE_COLOURS = 256
PALETTE_BYTES = PALETTE_COLOURS * 2
FILE_DATA_OFFSET = LEGACY_HEADER.size + PALETTE_BYTES
MAX_SHORT_RUN = 63
MAX_LONG_RUN = 0x3FFF
DEFAULT_MAX_SIZE = 80
DEFAULT_FPS = 5

BASE_HUES = (
    (255, 0, 0),       # Red
    (0, 255, 0),       # Green
    (0, 0, 255),       # Blue
    (255, 255, 0),     # Yellow
    (0, 255, 255),     # Cyan
    (255, 0, 255),     # Magenta
    (255, 128, 0),     # Orange
    (255, 0, 128),     # Pink
    (0, 255, 128),     # Spring Green / Turquoise
    (128, 0, 255),     # Violet
    (139, 69, 19),     # Brown
    (128, 128, 128),   # Gray
)


class LegacyConversionError(RuntimeError):
    """Raised for invalid input or a failed legacy conversion."""


def require_numpy() -> None:
    if np is None:
        raise LegacyConversionError(
            "NumPy is required. Install it with: python3 -m pip install numpy"
        )


def scale_to_8bit(width: int, height: int, max_size: int) -> tuple[int, int]:
    """Port of scale_to_8bit() from the original C converter."""
    if width <= 0 or height <= 0:
        raise LegacyConversionError(f"invalid source dimensions: {width}x{height}")
    if width <= max_size and height <= max_size:
        return width, height

    if width > height:
        ratio = max_size / width
    else:
        ratio = max_size / height

    out_width = max(1, int(width * ratio))
    out_height = max(1, int(height * ratio))
    return out_width, out_height


def create_seed_palette() -> bytes:
    """Return the exact original 6x6 RGB888 seed image (108 bytes)."""
    seed = bytearray()
    for red, green, blue in BASE_HUES:
        # Dark (50%), normal (100%), then halfway between colour and white.
        seed.extend((red // 2, green // 2, blue // 2))
        seed.extend((red, green, blue))
        seed.extend((
            red + (255 - red) // 2,
            green + (255 - green) // 2,
            blue + (255 - blue) // 2,
        ))
    if len(seed) != 6 * 6 * 3:
        raise AssertionError("legacy seed palette must be one 6x6 RGB frame")
    return bytes(seed)


def build_legacy_header(width: int, height: int, fps: int) -> bytes:
    """The original packed CPV_Header: CP, width, height, fps/flags, reserved."""
    if not (1 <= width <= 255 and 1 <= height <= 255):
        raise LegacyConversionError("legacy CPV dimensions must fit in uint8")
    if not (1 <= fps <= 15):
        raise LegacyConversionError("legacy CPV FPS must fit in the low four bits")
    fps_flags = fps & 0x0F  # Upper flag nibble was always zero in the old encoder.
    return LEGACY_HEADER.pack(b"CP", width, height, fps_flags, 0)


def run_checked(
    command: list[str],
    description: str,
    *,
    input_data: bytes | None = None,
) -> bytes:
    try:
        result = subprocess.run(
            command,
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
    except FileNotFoundError as exc:
        raise LegacyConversionError(
            f"{description}: executable not found: {command[0]}"
        ) from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip()
        raise LegacyConversionError(
            f"{description} failed" + (f":\n{detail}" if detail else "")
        ) from exc
    return result.stdout


def probe_dimensions(ffprobe: str, source: Path) -> tuple[int, int]:
    command = [
        ffprobe,
        "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height",
        "-of", "csv=p=0",
        str(source),
    ]
    output = run_checked(command, "ffprobe dimension query").decode(
        "ascii", errors="replace"
    ).strip()
    first_line = output.splitlines()[0] if output else ""
    fields = first_line.replace("x", ",").replace(" ", ",").split(",")
    try:
        width, height = int(fields[0]), int(fields[1])
    except (IndexError, ValueError) as exc:
        raise LegacyConversionError(
            f"could not read video dimensions from ffprobe output: {output!r}"
        ) from exc
    if width <= 0 or height <= 0:
        raise LegacyConversionError(f"invalid source dimensions: {width}x{height}")
    return width, height


def build_colour_palette(
    ffmpeg: str,
    source: Path,
    width: int,
    height: int,
    fps: int,
    temp_dir: Path,
):
    """Reproduce run_palette_gen() + build_color_palette() from the old main.c."""
    require_numpy()
    palette_png = temp_dir / "temp_palette.png"
    seed = create_seed_palette()

    # This filter graph deliberately mirrors the original C command:
    #   video: fps -> Lanczos scale
    #   seed: 6x6 raw RGB24 -> nearest-neighbour scale
    #   stack video + seed -> palettegen (no explicit max_colors; FFmpeg default)
    filter_graph = (
        f"[0:v]fps={fps},scale={width}:{height}:flags=lanczos[vid];"
        f"[1:v]scale={width}:-1:flags=neighbor[seed_scaled];"
        "[vid][seed_scaled]vstack=inputs=2[combined];"
        "[combined]palettegen=reserve_transparent=0:stats_mode=full[palette]"
    )
    palette_command = [
        ffmpeg,
        "-hide_banner", "-loglevel", "error",
        "-i", str(source),
        "-f", "rawvideo",
        "-pixel_format", "rgb24",
        "-video_size", "6x6",
        "-i", "pipe:0",
        "-filter_complex", filter_graph,
        "-map", "[palette]",
        "-y", str(palette_png),
    ]
    run_checked(
        palette_command,
        "legacy FFmpeg palette generation",
        input_data=seed,
    )

    # The original converter decoded the palette PNG as RGB24 and read exactly
    # 256 RGB triples. Do not reinterpret its alpha channel or reorder colours.
    extraction_command = [
        ffmpeg,
        "-hide_banner", "-loglevel", "error",
        "-i", str(palette_png),
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "pipe:1",
    ]
    raw_rgb = run_checked(extraction_command, "legacy FFmpeg palette extraction")
    expected_bytes = PALETTE_COLOURS * 3
    if len(raw_rgb) < expected_bytes:
        raise LegacyConversionError(
            f"FFmpeg returned only {len(raw_rgb) // 3} palette colours; "
            f"the legacy format requires {PALETTE_COLOURS}"
        )

    rgb = np.frombuffer(raw_rgb[:expected_bytes], dtype=np.uint8).reshape(
        PALETTE_COLOURS, 3
    ).astype(np.uint16)

    # Exact legacy RGB888 -> RGB565 conversion using bit shifts / truncation.
    r5 = (rgb[:, 0] >> 3) & 0x1F
    g6 = (rgb[:, 1] >> 2) & 0x3F
    b5 = (rgb[:, 2] >> 3) & 0x1F
    palette_rgb565 = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)

    # The C version wrote htons(rgb565) for each entry. On disk this is BE.
    palette_bytes = palette_rgb565.astype(">u2", copy=False).tobytes()
    return palette_rgb565, palette_bytes


def build_palette_rgb888(palette_rgb565):
    """Recreate find_best_index()'s RGB565-to-RGB888 palette expansion."""
    values = np.asarray(palette_rgb565, dtype=np.uint16)
    red = ((values >> 11) & 0x1F) << 3
    green = ((values >> 5) & 0x3F) << 2
    blue = (values & 0x1F) << 3
    return np.stack((red, green, blue), axis=1).astype(np.int16)


def map_frame_to_indices(frame_rgb24: bytes, palette_rgb565):
    """Vectorised, tie-stable equivalent of find_best_index() for each pixel.

    The old C code compares full input RGB888 values with palette channels
    expanded by left-shifting the RGB565 fields. NumPy argmin returns the first
    minimum, matching the C loop's strict '<' comparison and first-index tie.
    """
    require_numpy()
    pixels = np.frombuffer(frame_rgb24, dtype=np.uint8)
    if pixels.size % 3:
        raise LegacyConversionError("FFmpeg returned a partial RGB24 frame")
    pixels = pixels.reshape(-1, 3)
    palette_rgb = build_palette_rgb888(palette_rgb565)

    unique_colours, inverse = np.unique(pixels, axis=0, return_inverse=True)
    mapped_unique = np.empty(unique_colours.shape[0], dtype=np.uint8)

    # Bound temporary memory while retaining exactly the same RGB distance:
    # dr*dr + dg*dg + db*db, over all 256 palette entries.
    chunk_size = 512
    for start in range(0, unique_colours.shape[0], chunk_size):
        end = min(start + chunk_size, unique_colours.shape[0])
        chunk = unique_colours[start:end].astype(np.int16)
        differences = chunk[:, None, :] - palette_rgb[None, :, :]
        distances = np.sum(
            differences.astype(np.int32) * differences.astype(np.int32),
            axis=2,
        )
        mapped_unique[start:end] = np.argmin(distances, axis=1).astype(np.uint8)

    return mapped_unique[inverse]


def _emit_skip(out: bytearray, length: int) -> None:
    """Exact old 10xx short / 11xxxxxx long skip packet encoding."""
    remaining = length
    while remaining > 0:
        if remaining <= MAX_SHORT_RUN:
            out.append(0x80 | (remaining & 0x3F))
            remaining = 0
        else:
            chunk = min(remaining, MAX_LONG_RUN)
            out.extend(struct.pack(">H", 0xC000 | (chunk & 0x3FFF)))
            remaining -= chunk


def _emit_data(out: bytearray, length: int, colour_index: int) -> None:
    """Exact old 00xx short / 01xxxxxx long single-colour data packet."""
    remaining = length
    while remaining > 0:
        if remaining <= MAX_SHORT_RUN:
            out.append(0x00 | (remaining & 0x3F))
            out.append(colour_index & 0xFF)
            remaining = 0
        else:
            chunk = min(remaining, MAX_LONG_RUN)
            out.extend(struct.pack(">H", 0x4000 | (chunk & 0x3FFF)))
            out.append(colour_index & 0xFF)
            remaining -= chunk


def encode_frame_packets(current_indices, previous_indices) -> bytes:
    """Port of the old delta + run-length loop. Returns packets without size."""
    require_numpy()
    current = np.asarray(current_indices, dtype=np.uint8).reshape(-1)
    previous = np.asarray(previous_indices, dtype=np.uint8).reshape(-1)
    if current.size != previous.size:
        raise ValueError("current and previous index buffers must have equal sizes")

    out = bytearray()
    if current.size == 0:
        return bytes(out)

    # First identify continuous SKIP and changed regions. Inside each changed
    # region, split only when the current palette index changes. This is exactly
    # the old C loop's two nested run conditions.
    unchanged = current == previous
    transitions = np.flatnonzero(unchanged[1:] != unchanged[:-1]) + 1
    boundaries = np.empty(transitions.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = transitions
    boundaries[-1] = current.size

    for segment in range(boundaries.size - 1):
        start = int(boundaries[segment])
        end = int(boundaries[segment + 1])
        segment_length = end - start
        if unchanged[start]:
            _emit_skip(out, segment_length)
            continue

        changed_indices = current[start:end]
        colour_transitions = (
            np.flatnonzero(changed_indices[1:] != changed_indices[:-1]) + 1
        )
        colour_bounds = np.empty(colour_transitions.size + 2, dtype=np.int64)
        colour_bounds[0] = 0
        colour_bounds[1:-1] = colour_transitions
        colour_bounds[-1] = segment_length

        for colour_segment in range(colour_bounds.size - 1):
            colour_start = int(colour_bounds[colour_segment])
            colour_end = int(colour_bounds[colour_segment + 1])
            colour = int(changed_indices[colour_start])
            _emit_data(out, colour_end - colour_start, colour)

    return bytes(out)


def encode_legacy_frame(current_indices, previous_indices) -> bytes:
    """Return BE uint16 packet-size prefix + the original RLE packet stream."""
    packets = encode_frame_packets(current_indices, previous_indices)
    if len(packets) > 0xFFFF:
        raise LegacyConversionError(
            f"legacy frame packet stream is {len(packets)} bytes; "
            "the original uint16 frame-size field cannot represent it"
        )
    return struct.pack(">H", len(packets)) + packets


def read_exact_frame(stream: BinaryIO, frame_size: int) -> bytes | None:
    data = bytearray()
    while len(data) < frame_size:
        chunk = stream.read(frame_size - len(data))
        if not chunk:
            if not data:
                return None
            raise LegacyConversionError(
                f"FFmpeg returned a partial frame ({len(data)} of {frame_size} bytes)"
            )
        data.extend(chunk)
    return bytes(data)


def convert_video(
    source: Path,
    output: Path,
    *,
    max_size: int = DEFAULT_MAX_SIZE,
    fps: int = DEFAULT_FPS,
    ffmpeg: str = "ffmpeg",
    ffprobe: str = "ffprobe",
) -> dict[str, int | float | str]:
    require_numpy()
    source = source.expanduser().resolve()
    output = output.expanduser().resolve()
    if not source.is_file():
        raise LegacyConversionError(f"input video does not exist: {source}")
    if source == output:
        raise LegacyConversionError("input and output paths must be different")

    # These are the original C program's clamp rules.
    max_size = min(255, max(1, int(max_size)))
    fps = min(15, max(1, int(fps)))

    source_width, source_height = probe_dimensions(ffprobe, source)
    width, height = scale_to_8bit(source_width, source_height, max_size)
    header = build_legacy_header(width, height, fps)
    frame_bytes = width * height * 3

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output: str | None = None
    started = time.monotonic()
    frame_count = 0
    packet_bytes = 0
    previous_indices = np.zeros(width * height, dtype=np.uint8)

    try:
        with tempfile.TemporaryDirectory(prefix="cpv_legacy_palette_") as palette_dir:
            palette, palette_bytes = build_colour_palette(
                ffmpeg, source, width, height, fps, Path(palette_dir)
            )

            ffmpeg_command = [
                ffmpeg,
                "-hide_banner", "-loglevel", "error",
                "-i", str(source),
                "-map", "0:v:0",
                "-vf", f"fps={fps},scale={width}:{height}:flags=lanczos,hqdn3d",
                "-an",
                "-f", "rawvideo",
                "-pix_fmt", "rgb24",
                "pipe:1",
            ]

            with tempfile.NamedTemporaryFile(
                mode="w+b",
                prefix=output.name + ".",
                suffix=".tmp",
                dir=str(output.parent),
                delete=False,
            ) as out:
                temporary_output = out.name
                out.write(header)
                out.write(palette_bytes)

                with tempfile.TemporaryFile(mode="w+b") as ffmpeg_stderr:
                    try:
                        process = subprocess.Popen(
                            ffmpeg_command,
                            stdout=subprocess.PIPE,
                            stderr=ffmpeg_stderr,
                        )
                    except FileNotFoundError as exc:
                        raise LegacyConversionError(
                            f"video decoding: executable not found: {ffmpeg}"
                        ) from exc
                    assert process.stdout is not None

                    try:
                        while True:
                            raw_frame = read_exact_frame(process.stdout, frame_bytes)
                            if raw_frame is None:
                                break
                            current_indices = map_frame_to_indices(raw_frame, palette)
                            encoded_frame = encode_legacy_frame(
                                current_indices, previous_indices
                            )
                            out.write(encoded_frame)
                            packet_bytes += len(encoded_frame)
                            previous_indices[:] = current_indices
                            frame_count += 1

                            if frame_count % 100 == 0:
                                print(
                                    f"\rEncoded {frame_count} frames...",
                                    end="",
                                    file=sys.stderr,
                                    flush=True,
                                )
                    finally:
                        process.stdout.close()

                    return_code = process.wait()
                    ffmpeg_stderr.seek(0)
                    error_text = ffmpeg_stderr.read().decode(
                        "utf-8", errors="replace"
                    ).strip()
                    if return_code != 0:
                        raise LegacyConversionError(
                            "FFmpeg video conversion failed"
                            + (f":\n{error_text}" if error_text else "")
                        )

                if frame_count == 0:
                    raise LegacyConversionError("FFmpeg produced no video frames")
                out.flush()
                os.fsync(out.fileno())

        os.replace(temporary_output, output)
        temporary_output = None

        elapsed = max(0.001, time.monotonic() - started)
        size = output.stat().st_size
        print(
            f"Created legacy CPV: {output}\n"
            f"  Source: {source_width}x{source_height}\n"
            f"  Resolution: {width}x{height}\n"
            f"  FPS: {fps}\n"
            f"  Frames: {frame_count}\n"
            f"  Palette: {PALETTE_COLOURS} colours (legacy seeded FFmpeg palette)\n"
            f"  Frame payload bytes: {packet_bytes:,}\n"
            f"  File size: {size:,} bytes ({size / (1024 * 1024):.2f} MiB)\n"
            f"  Conversion time: {elapsed:.2f} s\n"
            f"  Conversion speed: {(frame_count / fps) / elapsed:.2f}x realtime"
        )
        return {
            "width": width,
            "height": height,
            "fps": fps,
            "frames": frame_count,
            "palette_bytes": len(palette_bytes),
            "packet_bytes": packet_bytes,
            "file_size": size,
            "elapsed_seconds": elapsed,
        }
    finally:
        if temporary_output is not None:
            try:
                os.unlink(temporary_output)
            except OSError:
                pass


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Python port of the original CPV converter. "
            "Writes the legacy CP (not CPV v1.x) file format."
        )
    )
    parser.add_argument("video", help="input video readable by FFmpeg")
    parser.add_argument(
        "-max_size", "--max-size", type=int, default=DEFAULT_MAX_SIZE,
        help="maximum width or height, preserving aspect ratio (old default: 80; range 1..255)",
    )
    parser.add_argument(
        "-fps", "--fps", type=int, default=DEFAULT_FPS,
        help="legacy frame rate (old default: 5; range 1..15)",
    )
    parser.add_argument(
        "-o", "--output", type=Path,
        help="output path (default: append .cpv to the complete input filename)",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg", help="FFmpeg executable")
    parser.add_argument("--ffprobe", default="ffprobe", help="FFprobe executable")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    source = Path(args.video).expanduser()
    output = args.output or Path(str(source) + ".cpv")
    try:
        convert_video(
            source,
            output,
            max_size=args.max_size,
            fps=args.fps,
            ffmpeg=args.ffmpeg,
            ffprobe=args.ffprobe,
        )
    except (LegacyConversionError, OSError, ValueError) as exc:
        print(f"Legacy CPV conversion error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
