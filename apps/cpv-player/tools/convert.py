#!/usr/bin/env python3
"""Encode FFmpeg-readable videos into the CPV v1.1 global-palette format."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import BinaryIO, Optional

try:
    import numpy as np
except ImportError:  # Keep --help usable when NumPy is not installed.
    np = None

try:
    import lz4.block as lz4_block
except ImportError:
    lz4_block = None


MAGIC = b"CPV\0"
VERSION_MAJOR = 1
VERSION_MINOR = 3

# CPV v1.1: 64-byte file header, 20-byte frame header, 12-byte index entry.
HEADER = struct.Struct("<4sBBHIHHHHIHHIIII20s")
FRAME_HEADER = struct.Struct("<4sBBHIII")
INDEX_HEADER = struct.Struct("<4sI")
INDEX_ENTRY = struct.Struct("<III")

MAX_WIDTH = 528
MAX_HEIGHT = 320
MAX_FPS = 30
MAX_RUN_LENGTH = 8191
MAX_DELTA_MASK_LENGTH = 65535
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
OP_DELTA_MASK = 4  # Accounting category; on disk this is an extended SKIP packet.

OP_NAMES = ("INDEX_LITERAL", "INDEX_REPEAT", "SKIP", "RAW565", "DELTA_MASK")


class ConversionError(RuntimeError):
    """Raised when input processing or CPV encoding fails."""


@dataclass
class PacketStats:
    counts: list[int]
    sizes: list[int]

    @classmethod
    def create(cls) -> "PacketStats":
        return cls([0] * len(OP_NAMES), [0] * len(OP_NAMES))

    def add(self, opcode: int, byte_count: int) -> None:
        self.counts[opcode] += 1
        self.sizes[opcode] += byte_count

    def merge(self, other: "PacketStats") -> None:
        for index in range(len(OP_NAMES)):
            self.counts[index] += other.counts[index]
            self.sizes[index] += other.sizes[index]


def require_numpy() -> None:
    if np is None:
        raise ConversionError(
            "NumPy is required for the fast encoder. On Arch/CachyOS install it with "
            "'sudo pacman -S python-numpy', or use 'python -m pip install numpy'."
        )


def require_lz4() -> None:
    if lz4_block is None:
        raise ConversionError(
            "LZ4 compression is enabled but python-lz4 is not installed. "
            "On Arch/CachyOS install it with 'sudo pacman -S python-lz4', "
            "or use 'python -m pip install lz4'. Pass --no-lz4 to disable it."
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
        [ffprobe, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(source)],
        [ffprobe, "-v", "error", "-show_entries", "format=duration",
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


def hqdn3d_filter(preprocess_quality: int) -> Optional[str]:
    """Map pre-processing quality to a mild-to-strong spatial/temporal denoiser."""
    if preprocess_quality == 0:
        return None

    strength = preprocess_quality / 100.0
    luma_spatial = 0.5 + 3.5 * strength
    chroma_spatial = 0.75 + 5.25 * strength
    luma_temporal = 1.0 + 5.0 * strength
    chroma_temporal = 1.5 + 7.5 * strength
    values = (
        luma_spatial,
        chroma_spatial,
        luma_temporal,
        chroma_temporal,
    )
    return "hqdn3d=" + ":".join(f"{value:.2f}" for value in values)


def make_filter(
    width: int,
    height: int,
    fps: Fraction,
    fit: str,
    background: str,
    preprocess_quality: int = 0,
) -> str:
    # Scale before denoising to keep the filter inexpensive. Denoising happens
    # before frame-rate reduction so temporal filtering sees the source cadence.
    filters = []
    if fit == "stretch":
        filters.append(f"scale={width}:{height}:flags=lanczos")
    else:
        filters.append(
            f"scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos"
        )
        filters.append(f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color={background}")

    denoise_filter = hqdn3d_filter(preprocess_quality)
    if denoise_filter is not None:
        filters.append(denoise_filter)

    filters.append(f"fps={fps.numerator}/{fps.denominator}")
    filters.append("setsar=1")
    return ",".join(filters)


def palette_limit_for_palette_quality(palette_quality: int) -> int:
    """Map quality to the palette budgets used by the legacy seed-palette method."""
    if palette_quality < 40:
        return 36
    if palette_quality < 60:
        return 64
    if palette_quality < 80:
        return 128
    return 256


# Exact twelve hues used by the original cp400-cpv-player palette generator.
LEGACY_BASE_HUES = (
    (255, 0, 0),       # red
    (0, 255, 0),       # green
    (0, 0, 255),       # blue
    (255, 255, 0),     # yellow
    (0, 255, 255),     # cyan
    (255, 0, 255),     # magenta
    (255, 128, 0),     # orange
    (255, 0, 128),     # pink
    (0, 255, 128),     # spring green / turquoise
    (128, 0, 255),     # violet
    (139, 69, 19),     # brown
    (128, 128, 128),   # grey
)


def build_legacy_seed_rgb24() -> bytes:
    """Create the exact 6x6 RGB888 seed image used by the legacy converter."""
    require_numpy()
    pixels: list[int] = []
    for red, green, blue in LEGACY_BASE_HUES:
        variants = (
            (red // 2, green // 2, blue // 2),
            (red, green, blue),
            (
                red + (255 - red) // 2,
                green + (255 - green) // 2,
                blue + (255 - blue) // 2,
            ),
        )
        for r, g, b in variants:
            pixels.extend((r, g, b))
    return bytes(pixels)


def build_legacy_seed_palette() -> np.ndarray:
    """Create the old 36-colour palette: 12 hues, each dark / normal / light."""
    require_numpy()
    rgb = np.frombuffer(build_legacy_seed_rgb24(), dtype=np.uint8).reshape(-1, 3).astype(np.uint16)
    # Match the original encoder's exact RGB888-to-RGB565 truncation.
    r5 = rgb[:, 0] >> 3
    g6 = rgb[:, 1] >> 2
    b5 = rgb[:, 2] >> 3
    colours = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)
    return colours


def merge_legacy_seed_palette(generated, palette_budget: int) -> np.ndarray:
    """Keep legacy seed colours first and fill remaining slots with video colours."""
    require_numpy()
    seed = build_legacy_seed_palette()
    if palette_budget < seed.size or palette_budget > 256:
        raise ValueError("palette budget must be in the range 36..256")
    generated = np.asarray(generated, dtype=np.uint16).reshape(-1)
    merged = np.concatenate((seed, generated))
    _, first_indices = np.unique(merged, return_index=True)
    ordered_unique = merged[np.sort(first_indices)]
    return np.ascontiguousarray(ordered_unique[:palette_budget], dtype=np.uint16)


def shift_frame(
    previous, width: int, height: int, dx: int, dy: int, fill_colour: int
):
    """Predict the current frame by translating the previous reconstructed frame."""
    source = np.asarray(previous, dtype=np.uint16).reshape(height, width)
    shifted = np.full((height, width), fill_colour, dtype=np.uint16)
    source_x0 = max(0, -dx)
    source_y0 = max(0, -dy)
    source_x1 = min(width, width - dx)
    source_y1 = min(height, height - dy)
    if source_x0 < source_x1 and source_y0 < source_y1:
        dest_x0 = source_x0 + dx
        dest_y0 = source_y0 + dy
        shifted[dest_y0:dest_y0 + (source_y1-source_y0),
                dest_x0:dest_x0 + (source_x1-source_x0)] = source[
            source_y0:source_y1, source_x0:source_x1
        ]
    return shifted.reshape(-1)


def _motion_match_count(
    current_2d, previous_2d, dx: int, dy: int, sample_step: int
) -> int:
    """Count exact RGB565 matches on the overlapping, regularly sampled area."""
    height, width = current_2d.shape
    sx0 = max(0, -dx)
    sy0 = max(0, -dy)
    sx1 = min(width, width - dx)
    sy1 = min(height, height - dy)
    if sx0 >= sx1 or sy0 >= sy1:
        return 0
    source = previous_2d[sy0:sy1:sample_step, sx0:sx1:sample_step]
    target = current_2d[sy0 + dy:sy1 + dy:sample_step,
                        sx0 + dx:sx1 + dx:sample_step]
    return int(np.count_nonzero(source == target))


def estimate_global_motion(
    current, previous, width: int, height: int, max_shift: int = MAX_GLOBAL_MOTION
) -> tuple[int, int]:
    """Estimate a small global translation using coarse-to-fine sampled matching."""
    current_2d = np.asarray(current, dtype=np.uint16).reshape(height, width)
    previous_2d = np.asarray(previous, dtype=np.uint16).reshape(height, width)

    # Coarse grid: at most roughly 17x10 samples at the calculator's target size.
    coarse_step = max(4, max(width, height) // 32)
    coarse_candidates = range(-max_shift, max_shift + 1, 2)
    coarse_dx = coarse_dy = 0
    coarse_score = _motion_match_count(
        current_2d, previous_2d, 0, 0, coarse_step
    )
    for dy in coarse_candidates:
        for dx in coarse_candidates:
            if dx == 0 and dy == 0:
                continue
            score = _motion_match_count(
                current_2d, previous_2d, dx, dy, coarse_step
            )
            if score > coarse_score:
                coarse_score, coarse_dx, coarse_dy = score, dx, dy

    # Refine the best coarse vector and compare against a zero-motion score
    # measured on exactly the same sample grid.
    fine_step = max(2, coarse_step // 2)
    best_dx = best_dy = 0
    zero_score = _motion_match_count(
        current_2d, previous_2d, 0, 0, fine_step
    )
    best_score = zero_score
    for dy in range(max(-max_shift, coarse_dy - 1),
                    min(max_shift, coarse_dy + 1) + 1):
        for dx in range(max(-max_shift, coarse_dx - 1),
                        min(max_shift, coarse_dx + 1) + 1):
            score = _motion_match_count(
                current_2d, previous_2d, dx, dy, fine_step
            )
            if score > best_score or (
                score == best_score
                and abs(dx) + abs(dy) < abs(best_dx) + abs(best_dy)
            ):
                best_score, best_dx, best_dy = score, dx, dy

    sample_count = ((height + fine_step - 1) // fine_step) * (
        (width + fine_step - 1) // fine_step
    )
    if (best_dx == 0 and best_dy == 0) or (
        best_score - zero_score < max(3, sample_count // 100)
    ):
        return 0, 0
    return best_dx, best_dy


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
    palette_quality: int,
    preprocess_quality: int,
    fps: Fraction,
    temp_dir: Path,
):
    """Use the original 6x6 seed image as a real FFmpeg palette-generation input."""
    require_numpy()
    palette_budget = palette_limit_for_palette_quality(palette_quality)
    seed_palette = build_legacy_seed_palette()
    seed_rgb24 = build_legacy_seed_rgb24()
    palette_path = temp_dir / "palette.png"

    video_filter = make_filter(
        width, height, fps, fit, background, preprocess_quality
    )
    filter_graph = (
        f"[0:v]{video_filter}[video];"
        f"[1:v]scale={width}:-1:flags=neighbor[seed];"
        "[video][seed]vstack=inputs=2:shortest=0[combined];"
        f"[combined]palettegen=max_colors={palette_budget}:"
        "stats_mode=full:reserve_transparent=0[palette]"
    )
    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(source),
        "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "6x6",
        "-framerate", "1", "-i", "pipe:0",
        "-filter_complex", filter_graph,
        "-map", "[palette]", "-frames:v", "1", str(palette_path),
    ]
    try:
        result = subprocess.run(
            command, input=seed_rgb24, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=True,
        )
    except FileNotFoundError as exc:
        raise ConversionError(f"palette generation: executable not found: {ffmpeg}") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode("utf-8", errors="replace").strip()
        raise ConversionError("seeded FFmpeg palette generation failed" + (f": {detail}" if detail else "")) from exc

    extract = [
        ffmpeg, "-hide_banner", "-loglevel", "error",
        "-i", str(palette_path), "-frames:v", "1",
        "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1",
    ]
    raw = _run_checked(extract, "palette extraction").stdout
    if not raw or len(raw) % 4:
        raise ConversionError("FFmpeg returned an invalid RGBA palette image")

    rgba = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 4)
    # palettegen emits transparent padding up to the 16x16 palette canvas.
    # Ignore it rather than accidentally adding transparent black as a colour.
    rgb = rgba[rgba[:, 3] != 0, :3].astype(np.uint16)
    if rgb.size == 0:
        raise ConversionError("FFmpeg generated an empty palette")
    red = (rgb[:, 0] * 31 + 127) // 255
    green = (rgb[:, 1] * 63 + 127) // 255
    blue = (rgb[:, 2] * 31 + 127) // 255
    generated = ((red << 11) | (green << 5) | blue).astype(np.uint16)

    # Exact legacy colours always occupy the first 36 entries. FFmpeg's
    # seed-weighted palette fills the remaining portion of the configured budget.
    palette = merge_legacy_seed_palette(generated, palette_budget)
    if palette.size < seed_palette.size or palette.size > palette_budget:
        raise ConversionError(f"invalid legacy-seed global palette size: {palette.size}")
    return palette, palette_budget


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


def map_frame(source565, palette, lookup, palette_quality: int):
    """Return palette indices, exact reconstructed RGB565 pixels, and RAW mask."""
    indices = lookup[source565]
    palette_pixels = palette[indices]
    if palette_quality == 100:
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


def bits_per_index(palette_count: int) -> int:
    if not (1 <= palette_count <= 256):
        raise ValueError("palette_count must be in the range 1..256")
    return max(1, (palette_count - 1).bit_length())


def pack_palette_indices(values, bit_width: int) -> bytes:
    """Pack indices LSB-first, with each packet independently byte-aligned."""
    values = np.asarray(values, dtype=np.uint8)
    if values.size == 0:
        return b""
    if not (1 <= bit_width <= 8):
        raise ValueError("palette index bit width must be in the range 1..8")
    if bit_width == 8:
        return values.tobytes()
    bit_shifts = np.arange(bit_width, dtype=np.uint8)
    bits = ((values[:, None] >> bit_shifts) & 1).astype(np.uint8, copy=False)
    return np.packbits(bits.reshape(-1), bitorder="little").tobytes()


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


def literal_cost(length: int, bit_width: int) -> int:
    total = 0
    while length:
        part = min(length, MAX_RUN_LENGTH)
        total += packet_header_size(part) + (part * bit_width + 7) // 8
        length -= part
    return total


def repeat_cost(length: int) -> int:
    full, remainder = divmod(length, MAX_RUN_LENGTH)
    total = full * 3  # 2-byte long header + one palette index.
    if remainder:
        total += packet_header_size(remainder) + 1
    return total


def emit_literal(out: bytearray, values, stats: PacketStats, bit_width: int) -> None:
    start = 0
    length = int(values.size)
    while start < length:
        count = min(MAX_RUN_LENGTH, length - start)
        packet_start = len(out)
        out.extend(packet_header(OP_INDEX_LITERAL, count))
        out.extend(pack_palette_indices(values[start:start + count], bit_width))
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


def emit_repeat_batch(out: bytearray, values, run_starts, run_lengths,
                      stats: PacketStats) -> None:
    """Emit repeat packets in one vectorised operation, preserving run order."""
    lengths = np.asarray(run_lengths, dtype=np.int64)
    starts = np.asarray(run_starts, dtype=np.int64)
    repeated_values = values[starts]

    if np.any(lengths > MAX_RUN_LENGTH):
        for value, length in zip(repeated_values, lengths):
            emit_repeat(out, int(value), int(length), stats)
        return

    short = lengths <= 32
    packets = np.empty((lengths.size, 3), dtype=np.uint8)
    packets[:, 0] = np.where(
        short,
        0x40 | (lengths - 1),
        0x60 | ((lengths >> 8) & 0x1F),
    )
    packets[:, 1] = np.where(short, repeated_values, lengths & 0xFF)
    packets[:, 2] = np.where(short, 0, repeated_values)
    keep = np.ones((lengths.size, 3), dtype=np.bool_)
    keep[short, 2] = False

    encoded = packets[keep].tobytes()
    out.extend(encoded)
    stats.counts[OP_INDEX_REPEAT] += int(lengths.size)
    stats.sizes[OP_INDEX_REPEAT] += len(encoded)


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


def encode_palette_segment(values, out: bytearray, stats: PacketStats,
                           bit_width: int) -> None:
    """Choose a compact mix of bit-packed literal and byte-index repeat packets."""
    length = int(values.size)
    if length == 0:
        return

    changes = np.flatnonzero(values[1:] != values[:-1]) + 1
    boundaries = np.empty(changes.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = changes
    boundaries[-1] = length

    run_lengths = np.diff(boundaries)
    repeated = run_lengths >= 4
    if not np.any(repeated):
        emit_literal(out, values, stats, bit_width)
        return

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
            run_plan_cost += literal_cost(pixel_length, bit_width)

    all_literal_cost = literal_cost(length, bit_width)
    group_count = group_boundaries.size - 1
    if group_count > 256:
        emit_literal(out, values, stats, bit_width)
        return

    saving_threshold = max(3, all_literal_cost // 100)
    if run_plan_cost + saving_threshold >= all_literal_cost:
        emit_literal(out, values, stats, bit_width)
        return

    for group in range(group_count):
        first_run = int(group_boundaries[group])
        after_run = int(group_boundaries[group + 1])
        pixel_start = int(boundaries[first_run])
        pixel_end = int(boundaries[after_run])
        if repeated[first_run]:
            emit_repeat_batch(
                out,
                values,
                boundaries[first_run:after_run],
                run_lengths[first_run:after_run],
                stats,
            )
        else:
            emit_literal(out, values[pixel_start:pixel_end], stats, bit_width)


def encode_normal_delta_chunk(indices, reconstructed, previous, out: bytearray,
                              stats: PacketStats, bit_width: int) -> None:
    changed = reconstructed != previous
    transitions = np.flatnonzero(changed[1:] != changed[:-1]) + 1
    boundaries = np.empty(transitions.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = transitions
    boundaries[-1] = int(changed.size)

    for segment in range(boundaries.size - 1):
        start = int(boundaries[segment])
        end = int(boundaries[segment + 1])
        if bool(changed[start]):
            encode_palette_segment(indices[start:end], out, stats, bit_width)
        else:
            emit_skip(out, end - start, stats)


def encode_palette_delta_segment(indices, reconstructed, previous,
                                 out: bytearray, stats: PacketStats,
                                 bit_width: int, use_delta_mask: bool) -> None:
    """Use a mixed delta mask only when it beats ordinary SKIP + data packets."""
    length = int(indices.size)
    chunk_start = 0
    while chunk_start < length:
        chunk_length = min(MAX_DELTA_MASK_LENGTH, length - chunk_start)
        chunk_end = chunk_start + chunk_length
        chunk_indices = indices[chunk_start:chunk_end]
        chunk_reconstructed = reconstructed[chunk_start:chunk_end]
        chunk_previous = previous[chunk_start:chunk_end]
        changed = chunk_reconstructed != chunk_previous

        normal = bytearray()
        normal_stats = PacketStats.create()
        encode_normal_delta_chunk(
            chunk_indices, chunk_reconstructed, chunk_previous,
            normal, normal_stats, bit_width,
        )

        changed_count = int(np.count_nonzero(changed))
        if use_delta_mask and 0 < changed_count < chunk_length:
            mask_bytes = np.packbits(changed, bitorder="little").tobytes()
            packed_indices = pack_palette_indices(chunk_indices[changed], bit_width)
            candidate = (
                bytes((0xA0, 0x00))
                + struct.pack("<H", chunk_length)
                + mask_bytes
                + packed_indices
            )
            if len(candidate) < len(normal):
                out.extend(candidate)
                stats.add(OP_DELTA_MASK, len(candidate))
            else:
                out.extend(normal)
                stats.merge(normal_stats)
        else:
            out.extend(normal)
            stats.merge(normal_stats)
        chunk_start = chunk_end


def encode_frame_payload(indices, reconstructed, raw_mask, previous,
                         frame_type: int, bit_width: int, use_delta_mask: bool):
    """Encode I/P frames; raw quality escapes are isolated from delta-mask spans."""
    pixel_count = int(reconstructed.size)
    if frame_type == FRAME_I:
        raw_positions = raw_mask
    elif frame_type == FRAME_P:
        changed = reconstructed != previous
        raw_positions = raw_mask & changed
    else:
        raise ValueError("invalid CPV frame type")

    transitions = np.flatnonzero(raw_positions[1:] != raw_positions[:-1]) + 1
    boundaries = np.empty(transitions.size + 2, dtype=np.int64)
    boundaries[0] = 0
    boundaries[1:-1] = transitions
    boundaries[-1] = pixel_count

    payload = bytearray()
    stats = PacketStats.create()
    for segment in range(boundaries.size - 1):
        start = int(boundaries[segment])
        end = int(boundaries[segment + 1])
        if bool(raw_positions[start]):
            emit_raw(payload, reconstructed[start:end], stats)
        elif frame_type == FRAME_P:
            encode_palette_delta_segment(
                indices[start:end],
                reconstructed[start:end],
                previous[start:end],
                payload,
                stats,
                bit_width,
                use_delta_mask,
            )
        else:
            encode_palette_segment(indices[start:end], payload, stats, bit_width)

    if frame_type == FRAME_I and (stats.counts[OP_SKIP] or stats.counts[OP_DELTA_MASK]):
        raise AssertionError("encoder generated a delta packet in an I-frame")
    return bytes(payload), stats


def compress_frame_payload(payload: bytes, enabled: bool) -> tuple[bytes, int, int]:
    """Choose the smaller independent LZ4-HC or DEFLATE frame stream."""
    if not enabled:
        return payload, 0, 0

    prefix = struct.pack("<I", len(payload))
    deflate_data = zlib.compress(payload, level=9)
    candidates: list[tuple[int, bytes, int]] = [
        (len(prefix) + len(deflate_data), prefix + deflate_data, FRAME_FLAG_DEFLATE)
    ]
    if lz4_block is not None:
        lz4_data = lz4_block.compress(
            payload, mode="high_compression", compression=9, store_size=False
        )
        lz4_stored = prefix + lz4_data
        candidates.append((len(lz4_stored), lz4_stored, FRAME_FLAG_LZ4))

    _, stored, flag = min(candidates, key=lambda item: item[0])
    saving = len(payload) - len(stored)
    if saving < max(16, len(payload) // 100):
        return payload, 0, 0
    return stored, flag, saving


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
    flags: int,
) -> int:
    if not (1 <= duration_ms <= 0xFFFFFFFF):
        raise ConversionError("frame duration must fit in a non-zero uint32")
    compression_flags = flags & FRAME_COMPRESSION_MASK
    if flags & ~FRAME_FLAGS_MASK or compression_flags == FRAME_COMPRESSION_MASK:
        raise ConversionError("attempted to write invalid frame flags")
    if len(payload) > 0xFFFFFFFF:
        raise ConversionError("frame payload exceeds the CPV uint32 limit")
    offset = output.tell()
    if offset > 0xFFFFFFFF or len(payload) + FRAME_HEADER.size > 0xFFFFFFFF - offset:
        raise ConversionError("output exceeds the CPV v1 4 GiB file-offset limit")
    output.write(FRAME_HEADER.pack(
        b"FRM1", frame_type, flags, FRAME_HEADER.size,
        frame_number, duration_ms, len(payload),
    ))
    output.write(payload)
    return offset

def convert(args: argparse.Namespace) -> None:
    require_numpy()
    if not args.no_frame_compression and lz4_block is None:
        print("Notice: python-lz4 is not installed; DEFLATE will be used where beneficial.")
    source = Path(args.input).expanduser().resolve()
    output_path = Path(args.output).expanduser().resolve()
    if not source.is_file():
        raise ConversionError(f"input file does not exist: {source}")
    if source == output_path:
        raise ConversionError("input and output paths must be different")
    if not (1 <= args.width <= MAX_WIDTH and 1 <= args.height <= MAX_HEIGHT):
        raise ConversionError(f"width and height must be within 1..{MAX_WIDTH} and 1..{MAX_HEIGHT}")
    if not (0 <= args.palette_quality <= 100):
        raise ConversionError("palette quality must be between 0 and 100")
    if not (0 <= args.preprocess_quality <= 100):
        raise ConversionError("pre-processing quality must be between 0 and 100")
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
    video_filter = make_filter(
        args.width, args.height, fps, args.fit, args.background,
        args.preprocess_quality,
    )
    frame_bytes = args.width * args.height * 3
    pixel_count = args.width * args.height
    keyframe_interval_ms = max(1, round(args.keyframe_interval * 1000))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    conversion_started = time.monotonic()
    total_packet_stats = PacketStats.create()
    total_lz4_frames = 0
    total_deflate_frames = 0
    total_compression_savings = 0
    total_motion_frames = 0
    total_motion_bytes_saved = 0
    index_entries: list[tuple[int, int, int]] = []
    process: Optional[subprocess.Popen[bytes]] = None
    stderr_file = tempfile.TemporaryFile(mode="w+b")
    temp_output_name: Optional[str] = None

    try:
        with tempfile.TemporaryDirectory(prefix="cpv_palette_") as palette_dir:
            palette, palette_limit = build_global_palette(
                ffmpeg, source, args.width, args.height, args.fit,
                args.background, args.palette_quality, args.preprocess_quality,
                fps, Path(palette_dir),
            )
            lookup = build_rgb565_lookup(palette)
            print(
                f"Global palette: {palette.size}/{palette_limit} colours "
                f"(palette quality {args.palette_quality}/100; "
                f"pre-process quality {args.preprocess_quality}/100)"
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
                        source565, palette, lookup, args.palette_quality
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
                        bit_width = bits_per_index(int(palette.size))
                        payload, frame_stats = encode_frame_payload(
                            indices,
                            reconstructed,
                            raw_mask,
                            previous_reconstruction,
                            frame_type,
                            bit_width,
                            not args.no_delta_mask,
                        )
                        stored_payload, compression_flag, frame_saving = compress_frame_payload(
                            payload, not args.no_frame_compression
                        )
                        frame_flags = compression_flag

                        # For P-frames, try a small global translation of the previous
                        # reconstructed image. Keep it only when the fully compressed
                        # motion candidate beats the ordinary P-frame byte-for-byte.
                        if (
                            frame_type == FRAME_P
                            and not args.no_global_motion
                            and previous_reconstruction is not None
                        ):
                            dx, dy = estimate_global_motion(
                                reconstructed, previous_reconstruction,
                                args.width, args.height,
                            )
                            if dx != 0 or dy != 0:
                                background_rgb = int(args.background, 16)
                                fill_red = (background_rgb >> 16) & 0xFF
                                fill_green = (background_rgb >> 8) & 0xFF
                                fill_blue = background_rgb & 0xFF
                                fill_colour = (
                                    ((fill_red * 31 + 127) // 255) << 11
                                    | ((fill_green * 63 + 127) // 255) << 5
                                    | ((fill_blue * 31 + 127) // 255)
                                )
                                predicted = shift_frame(
                                    previous_reconstruction, args.width, args.height,
                                    dx, dy, fill_colour,
                                )
                                motion_packets, motion_stats = encode_frame_payload(
                                    indices, reconstructed, raw_mask, predicted,
                                    FRAME_P, bit_width, not args.no_delta_mask,
                                )
                                motion_stream = (
                                    MOTION_HEADER.pack(dx, dy, fill_colour)
                                    + motion_packets
                                )
                                motion_stored, motion_compression_flag, motion_saving = (
                                    compress_frame_payload(
                                        motion_stream, not args.no_frame_compression
                                    )
                                )
                                if len(motion_stored) < len(stored_payload):
                                    total_motion_frames += 1
                                    total_motion_bytes_saved += (
                                        len(stored_payload) - len(motion_stored)
                                    )
                                    stored_payload = motion_stored
                                    frame_flags = (
                                        motion_compression_flag | FRAME_FLAG_GLOBAL_MOTION
                                    )
                                    frame_stats = motion_stats
                                    frame_saving = motion_saving
                                    compression_flag = motion_compression_flag

                        if compression_flag == FRAME_FLAG_LZ4:
                            total_lz4_frames += 1
                            total_compression_savings += frame_saving
                        elif compression_flag == FRAME_FLAG_DEFLATE:
                            total_deflate_frames += 1
                            total_compression_savings += frame_saving
                        frame_offset = write_frame_record(
                            output,
                            stored_payload,
                            frame_type,
                            stored_frame_count,
                            duration_ms,
                            frame_flags,
                        )
                        if frame_type == FRAME_I:
                            index_entries.append((stored_frame_count, start_ms, frame_offset))
                            last_keyframe_start_ms = start_ms
                        total_packet_stats.merge(frame_stats)
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
        elapsed_wall = max(0.001, time.monotonic() - conversion_started)
        size = output_path.stat().st_size
        print(
            f"Created {output_path}\n"
            f"  Resolution: {args.width}x{args.height}\n"
            f"  Frame rate: {fps.numerator}/{fps.denominator} FPS\n"
            f"  Input frames: {input_frame_number}\n"
            f"  Stored frames: {stored_frame_count}\n"
            f"  Keyframes: {len(index_entries)}\n"
            f"  Duration: {seconds:.3f} s\n"
            f"  Palette: {palette.size} colours (36 legacy seed colours + adaptive video colours)\n"
            f"  Palette quality: {args.palette_quality}/100\n"
            f"  Pre-process quality: {args.preprocess_quality}/100\n"
            f"  Palette index width: {bits_per_index(int(palette.size))} bits\n"
            f"  LZ4-HC frames: {total_lz4_frames}/{stored_frame_count}\n"
            f"  DEFLATE/Huffman frames: {total_deflate_frames}/{stored_frame_count}\n"
            f"  Compression bytes saved: {total_compression_savings:,}\n"
            f"  Global-motion frames: {total_motion_frames}/{stored_frame_count}\n"
            f"  Bytes saved by global motion: {total_motion_bytes_saved:,}\n"
            f"  File size: {size:,} bytes ({size / (1024 * 1024):.2f} MiB)\n"
            f"  Conversion time: {elapsed_wall:.2f} s\n"
            f"  Conversion speed: {seconds / elapsed_wall:.2f}× realtime\n"
            f"  Packets: "
            + ", ".join(
                f"{OP_NAMES[i]}={total_packet_stats.counts[i]} "
                f"({total_packet_stats.sizes[i]:,} bytes)"
                for i in range(len(OP_NAMES))
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
        description="Convert an FFmpeg-readable video into the CPV v1.3 format."
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
        "--palette-quality", type=int, choices=range(0, 101), default=75,
        help="palette budget: 0 (36 legacy seed colours) to 100 (256 colours + RAW565 fallback; default: 75)",
    )
    parser.add_argument(
        "--preprocess-quality", type=int, choices=range(0, 101), default=0,
        help="spatial/temporal denoising strength: 0 disables it, 100 is strongest (default: 0)",
    )
    parser.add_argument(
        "--no-global-motion", action="store_true",
        help="disable optional global-motion prediction on P-frames (benchmarking only)",
    )
    parser.add_argument(
        "--no-delta-mask", action="store_true",
        help="disable mixed SKIP/data delta-mask packets (useful for size comparisons)",
    )
    parser.add_argument(
        "--no-frame-compression", "--no-lz4", dest="no_frame_compression",
        action="store_true",
        help="disable per-frame LZ4-HC and DEFLATE/Huffman compression (benchmarking only)",
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
