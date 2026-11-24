#!/usr/bin/env python3
"""Reproducible CPV quality/size/speed benchmark with full reference decoding."""
from __future__ import annotations
import argparse, csv, hashlib, importlib.util, json, math, re, subprocess, sys, time
from datetime import datetime
from pathlib import Path
from typing import Any
import numpy as np

TOOLS_DIR = Path(__file__).resolve().parent
CONVERTER = TOOLS_DIR / "cpv_convert"
PLAYER_PATH = TOOLS_DIR / "play.py"

# Orthogonal sweeps: palette budget at fixed denoising, then denoising at fixed palette.
QUALITY_CASES = [
    ("palette_025_preprocess_100", 25, 100),
    ("palette_050_preprocess_100", 50, 100),
    ("palette_070_preprocess_100", 70, 100),
    ("palette_080_preprocess_100", 80, 100),
    ("palette_100_preprocess_100", 100, 100),
    ("palette_080_preprocess_000", 80, 0),
    ("palette_080_preprocess_025", 80, 25),
    ("palette_080_preprocess_050", 80, 50),
    ("palette_080_preprocess_075", 80, 75),
]
CSV_FIELDS = [
    "case", "palette_quality", "preprocess_quality", "width", "height", "fps",
    "keyframe_interval", "palette_colours", "input_frames", "reference_frames",
    "stored_frames", "file_bytes", "file_mib", "conversion_seconds",
    "wall_seconds", "decoder_seconds", "psnr_db", "deflate_frames", "lz4_frames",
    "raw_frames", "libdeflate_l6", "libdeflate_l9", "libdeflate_l10",
    "libdeflate_l12", "mask_disabled_frames", "mask_heuristic_frames",
    "mask_compressed_probe_frames", "global_motion_frames", "global_motion_bytes_saved",
]

def load_player():
    spec = importlib.util.spec_from_file_location("cpv_benchmark_player", PLAYER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load reference decoder: {PLAYER_PATH}")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves annotations through sys.modules during class creation.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def probe_source(path: Path, ffprobe: str) -> dict[str, Any]:
    cmd = [
        ffprobe, "-v", "error", "-select_streams", "v:0",
        "-show_entries",
        "stream=codec_name,width,height,pix_fmt,r_frame_rate,avg_frame_rate,duration,nb_frames:"
        "format=duration,size", "-of", "json", str(path),
    ]
    data = json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)
    if not data.get("streams"):
        raise RuntimeError("ffprobe found no video stream")
    return {
        "path": str(path.resolve()), "file_bytes": path.stat().st_size,
        "sha256": sha256_file(path), "ffprobe": data,
    }

def make_reference_filter(width: int, height: int, fps: int) -> str:
    # Same FPS and geometry as the converter, deliberately without hqdn3d.
    return (
        f"fps={fps}/1,scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=0x000000,setsar=1"
    )

def read_exact_frame(stream, frame_bytes: int) -> bytes | None:
    chunks = []
    remaining = frame_bytes
    while remaining:
        block = stream.read(remaining)
        if not block:
            if not chunks:
                return None
            raise RuntimeError("FFmpeg returned a truncated reference frame")
        chunks.append(block)
        remaining -= len(block)
    return b"".join(chunks)

def decode_and_measure(cpv_path: Path, input_path: Path, ffmpeg: str, player,
                       width: int, height: int, fps: int) -> tuple[int, float, float]:
    cpv = player.CPVFile(cpv_path)
    process = None
    decode_seconds = 0.0
    squared_error = 0
    sample_count = 0
    reference_frames = 0
    next_record = 0
    pixels = width * height
    framebuffer = np.zeros(pixels, dtype=np.uint16)
    rendered = np.empty((pixels, 3), dtype=np.uint8)
    try:
        actual = (cpv.width, cpv.height, cpv.fps_num, cpv.fps_den, cpv.version_minor)
        expected = (width, height, fps, 1, 3)
        if actual != expected:
            raise RuntimeError(f"CPV header profile mismatch: {actual}, expected {expected}")
        cmd = [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(input_path),
            "-map", "0:v:0", "-vf", make_reference_filter(width, height, fps),
            "-an", "-sn", "-dn", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
        ]
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        assert process.stdout is not None
        while True:
            raw = read_exact_frame(process.stdout, pixels * 3)
            if raw is None:
                break
            timestamp_ms = (reference_frames * 1000 + fps // 2) // fps
            # Decode records sequentially; coalesced frames remain displayed.
            while next_record < cpv.frame_count and cpv.frames[next_record].start_ms <= timestamp_ms:
                started = time.perf_counter()
                cpv.decode_frame(next_record, framebuffer)
                decode_seconds += time.perf_counter() - started
                next_record += 1
            if next_record == 0:
                raise RuntimeError("no CPV frame covers the first reference frame")
            r = (framebuffer >> 11) & 31
            g = (framebuffer >> 5) & 63
            b = framebuffer & 31
            rendered[:, 0] = ((r.astype(np.uint32) * 255 + 15) // 31).astype(np.uint8)
            rendered[:, 1] = ((g.astype(np.uint32) * 255 + 31) // 63).astype(np.uint8)
            rendered[:, 2] = ((b.astype(np.uint32) * 255 + 15) // 31).astype(np.uint8)
            reference = np.frombuffer(raw, dtype=np.uint8).reshape(pixels, 3)
            diff = rendered.astype(np.int16) - reference.astype(np.int16)
            squared_error += int(np.sum(diff.astype(np.int64) ** 2))
            sample_count += diff.size
            reference_frames += 1
        process.stdout.close()
        code = process.wait()
        if code != 0:
            raise RuntimeError(f"FFmpeg reference decode failed with exit code {code}")
        if not reference_frames or next_record != cpv.frame_count:
            raise RuntimeError(
                f"reference alignment failed: {reference_frames} source frames, "
                f"{next_record}/{cpv.frame_count} CPV records decoded"
            )
        mse = squared_error / sample_count
        psnr = math.inf if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
        return reference_frames, decode_seconds, psnr
    finally:
        if process is not None and process.poll() is None:
            if process.stdout is not None:
                process.stdout.close()
            process.terminate()
            process.wait()
        cpv.close()

def parse_log(log: str) -> dict[str, int | float]:
    def get(pattern: str, label: str) -> re.Match[str]:
        match = re.search(pattern, log)
        if not match:
            raise RuntimeError(f"converter output missing {label}")
        return match
    winners = {}
    for item in get(r"Compression candidate winners:\s+([^\n]+)", "compression winners").group(1).split(","):
        key, value = item.strip().rsplit("=", 1)
        winners[key] = int(value)
    masks = {}
    for item in get(r"DELTA_MASK packetisation:\s+([^\n]+)", "DELTA_MASK statistics").group(1).split(","):
        key, value = item.strip().rsplit("=", 1)
        masks[key] = int(value)
    return {
        "palette_colours": int(get(r"Palette:\s+(\d+)\s+colours", "palette count").group(1)),
        "input_frames": int(get(r"Input frames:\s+(\d+)", "input frame count").group(1)),
        "stored_frames": int(get(r"Stored frames:\s+(\d+)", "stored frame count").group(1)),
        "deflate_frames": int(get(r"DEFLATE/Huffman frames:\s+(\d+)/", "DEFLATE count").group(1)),
        "lz4_frames": int(get(r"LZ4-HC frames:\s+(\d+)/", "LZ4 count").group(1)),
        "conversion_seconds": float(get(r"Conversion time:\s+([0-9.]+)\s+s", "conversion time").group(1)),
        "global_motion_frames": int(get(r"Global-motion frames:\s+(\d+)/", "motion count").group(1)),
        "global_motion_bytes_saved": int(get(r"Bytes saved by global motion:\s+(\d+)", "motion savings").group(1)),
        "raw_frames": winners.get("raw", 0),
        "libdeflate_l6": winners.get("libdeflate L6", 0),
        "libdeflate_l9": winners.get("libdeflate L9", 0),
        "libdeflate_l10": winners.get("libdeflate L10", 0),
        "libdeflate_l12": winners.get("libdeflate L12", 0),
        "mask_disabled_frames": masks.get("disabled", 0),
        "mask_heuristic_frames": masks.get("local-size heuristic", 0),
        "mask_compressed_probe_frames": masks.get("compressed-size probe", 0),
    }

def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path,
                        default=TOOLS_DIR / "2026-11-21_200236.mp4",
                        help="input video (default: tools/2026-11-21_200236.mp4)")
    parser.add_argument("--output-dir", type=Path, help="new output directory; must not already exist")
    parser.add_argument("--width", type=int, default=132)
    parser.add_argument("--height", type=int, default=74)
    parser.add_argument("--fps", type=int, default=5)
    parser.add_argument("--keyframe-interval", type=float, default=10.0)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    args = parser.parse_args()
    source = args.input.resolve()
    if not source.is_file():
        parser.error(f"input video does not exist: {source}")
    if not (1 <= args.width <= 528 and 1 <= args.height <= 320):
        parser.error("width/height must be within CPV v1 limits (1..528, 1..320)")
    if not (1 <= args.fps <= 30):
        parser.error("fps must be in the range 1..30")
    if not (0 < args.keyframe_interval <= 10):
        parser.error("keyframe interval must be greater than 0 and at most 10 seconds")
    if not CONVERTER.is_file():
        parser.error(f"converter not found: {CONVERTER}; build it with make -C {TOOLS_DIR}")
    try:
        player = load_player()
        source_metadata = probe_source(source, args.ffprobe)
    except Exception as exc:
        parser.error(str(exc))

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = (args.output_dir or TOOLS_DIR / "benchmarks" / f"quality-{stamp}").resolve()
    try:
        output_dir.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        parser.error(f"output directory already exists; refusing to overwrite: {output_dir}")

    manifest = {
        "created_local": datetime.now().astimezone().isoformat(),
        "source": source_metadata,
        "fixed_output_profile": {
            "width": args.width, "height": args.height, "fps": args.fps,
            "fit": "contain", "background": "000000",
            "keyframe_interval_seconds": args.keyframe_interval,
            "global_motion": True, "delta_mask": True, "compression_portfolio": True,
        },
        "quality_matrix": [
            {"case": name, "palette_quality": palette, "preprocess_quality": preprocess}
            for name, palette, preprocess in QUALITY_CASES
        ],
        "psnr_reference": (
            "Common undenoised FFmpeg reference at the same output dimensions/FPS; "
            "includes palette quantisation, RGB565 rounding, and denoising distortion."
        ),
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    rows: list[dict[str, Any]] = []
    print(f"Source: {source}")
    print(f"Input bytes: {source_metadata['file_bytes']}")
    print(f"Input SHA-256: {source_metadata['sha256']}")
    print(f"Fixed output: {args.width}x{args.height} @ {args.fps} FPS")
    print(f"Output directory: {output_dir}")
    print(f"Quality cases: {len(QUALITY_CASES)}\n")

    for index, (case, palette_quality, preprocess_quality) in enumerate(QUALITY_CASES, 1):
        output = output_dir / f"{case}.cpv"
        log_path = output_dir / f"{case}.log"
        command = [
            str(CONVERTER), str(source), "--output", str(output),
            "--width", str(args.width), "--height", str(args.height), "--fps", str(args.fps),
            "--palette-quality", str(palette_quality),
            "--preprocess-quality", str(preprocess_quality),
            "--keyframe-interval", str(args.keyframe_interval),
            "--background", "000000", "--ffmpeg", args.ffmpeg, "--ffprobe", args.ffprobe,
        ]
        print(f"[{index}/{len(QUALITY_CASES)}] {case}")
        started = time.perf_counter()
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        wall_seconds = time.perf_counter() - started
        log_path.write_text(result.stdout, encoding="utf-8")
        if result.returncode != 0:
            print(result.stdout)
            raise RuntimeError(f"converter failed for {case}; see {log_path}")

        stats = parse_log(result.stdout)
        reference_frames, decoder_seconds, psnr = decode_and_measure(
            output, source, args.ffmpeg, player, args.width, args.height, args.fps
        )
        if reference_frames != stats["input_frames"]:
            raise RuntimeError(
                f"{case}: converter read {stats['input_frames']} frames, "
                f"reference produced {reference_frames}"
            )
        size = output.stat().st_size
        row: dict[str, Any] = {
            "case": case, "palette_quality": palette_quality,
            "preprocess_quality": preprocess_quality,
            "width": args.width, "height": args.height, "fps": args.fps,
            "keyframe_interval": args.keyframe_interval,
            **stats, "reference_frames": reference_frames,
            "file_bytes": size, "file_mib": round(size / (1024.0 * 1024.0), 4),
            "wall_seconds": round(wall_seconds, 4),
            "decoder_seconds": round(decoder_seconds, 4), "psnr_db": round(psnr, 4),
        }
        rows.append(row)
        write_csv(output_dir / "summary.csv", rows)
        print(
            f"  {size:,} bytes | {psnr:.3f} dB PSNR | "
            f"encode {stats['conversion_seconds']:.2f}s | decode {decoder_seconds:.2f}s | "
            f"palette {stats['palette_colours']} colours"
        )

    (output_dir / "results.json").write_text(
        json.dumps(rows, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"\nCompleted: {len(rows)} cases")
    print(f"CSV: {output_dir / 'summary.csv'}")
    print(f"Manifest: {output_dir / 'manifest.json'}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nBenchmark interrupted; completed cases remain in the output directory.", file=sys.stderr)
        raise SystemExit(130)
