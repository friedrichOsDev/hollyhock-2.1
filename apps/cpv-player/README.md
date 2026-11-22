# CPV Player

A Hollyhock 2.1 application for playing CPV v1.2 video files on the Casio fx-CP400. The host tools also read CPV v1.0 and v1.1 files. The `tools/` directory also contains a desktop encoder and a reference preview player.

## Host tool requirements

- Python 3.10 or newer.
- NumPy.
- Optional `lz4` Python package (`python-lz4` on Arch/CachyOS); DEFLATE is built into Python.
- FFmpeg and FFprobe.
- Tkinter for the desktop preview player.

On Arch Linux / CachyOS, install the dependencies with:

```bash
sudo pacman -S ffmpeg python-numpy python-lz4 tk
```

On other Linux distributions, install NumPy, FFmpeg, FFprobe, and the Python Tkinter package using the distribution's package manager.

Verify the Python modules:

```bash
python3 -c "import numpy; print(numpy.__version__)"
python3 -c "import lz4.block; print('LZ4 available')"  # optional
python3 -m tkinter
```

The converter uses NumPy to accelerate RGB565 conversion, palette lookup, bit-packing, delta analysis, and packet preparation. The palette reserves its first eight entries for red, green, blue, yellow, cyan, magenta, black and white, then FFmpeg's video-adaptive quantiser fills the remaining budget. The converter compares LZ4 high-compression with zlib DEFLATE (which uses Huffman coding) per frame and selects the smaller result. LZ4 is optional; DEFLATE is available from Python's standard library.

## Convert a video

Run from this application directory:

```bash
python3 tools/convert.py <video-file> \
    --output <output.cpv> \
    --width <width> \
    --height <height> \
    --fps <fps> \
    --palette-quality <0..100> \
    --preprocess-quality <0..100>
```

Example for the calculator's maximum resolution and target frame rate:

```bash
python3 tools/convert.py input.mp4 \
    --output output.cpv \
    --width 528 \
    --height 320 \
    --fps 10 \
    --palette-quality 75 \
    --preprocess-quality 35
```

The input must be readable by FFmpeg. CPV v1 supports dimensions from 1×1 up to 528×320 pixels and frame rates greater than 0 and no greater than 30 FPS.

By default, the converter preserves the source aspect ratio and fills unused space with black. It samples the preprocessed video at one frame per second to find frequent colours near useful anchor targets (dark, light, grey, warm skin-like, and primary/secondary colours). These anchors are actual colours found in the video, not synthetic colours. FFmpeg's palette quantiser fills the remaining global palette budget. The converter then maps source RGB565 pixels through a precomputed lookup table and writes linear I/P-frame packet streams. The converter streams completed frame records to a temporary output file rather than accumulating the complete compressed video in memory.

### Compression settings

CPV v1.1/v1.2 applies these optimisations:

1. **Bit-packed palette indices.** INDEX_LITERAL and DELTA_MASK indices use the minimum width for the active palette: 4 bits for up to 16 colours, 5 bits for up to 32, 6 bits for up to 64, 7 bits for up to 128, and 8 bits for up to 256. INDEX_REPEAT keeps a one-byte index.
2. **DELTA_MASK packets.** Mixed spans of changed and unchanged pixels are represented by a bitmask. The encoder compares the complete byte cost against ordinary SKIP plus data packets and emits the mask only when it is smaller.
3. **Per-frame LZ4-HC vs DEFLATE.** The encoder compares LZ4 high-compression and zlib DEFLATE level 9, which uses Huffman coding, and chooses the smaller stream including the four-byte decoded-size prefix. The frame is left uncompressed if compression does not save at least 1% or 16 bytes. Each frame is independent, so keyframe seeking remains simple. CPV v1.2 adds the DEFLATE flag; LZ4 remains supported.

The converter enables DELTA_MASK and frame compression by default. DEFLATE is built into Python; LZ4 is tried if python-lz4 is installed. Use `--no-delta-mask` and/or `--no-frame-compression` (alias `--no-lz4`) to disable them for benchmarks. The resulting statistics let you compare each optimisation independently.

### Independent quality settings

The converter separates colour quantisation from pre-processing. Adjust these independently because they solve different problems.

#### Palette quality

`--palette-quality` controls the global palette budget:

| Palette quality | Palette budget | Behaviour |
|---:|---:|---|
| 0–9 | 8 colours (fixed base palette) | Extreme quantisation |
| 10–24 | 16 colours | Strong colour quantisation |
| 25–39 | 32 colours | Aggressive colour reduction |
| 40–59 | 64 colours | Balanced colour reduction |
| 60–79 | 128 colours | Higher colour fidelity |
| 80–99 | 256 colours | Highest palette budget |
| 100 | Up to 256 + RAW565 | Preserve every source RGB565 value |

Palette quality 100 uses RAW565 packets for pixels that cannot be represented exactly by the global palette. It is intended for lossless-within-RGB565 colour output and can produce substantially larger files.

#### Pre-processing quality

`--preprocess-quality` controls FFmpeg's `hqdn3d` spatial and temporal denoising filter. It runs after scaling and before output frame-rate reduction. The same filter is applied when generating the global palette and when reading frames for encoding.

| Pre-process quality | Behaviour |
|---:|---|
| 0 | Disabled; no denoising (default) |
| 1–25 | Very mild denoising |
| 26–50 | Mild to moderate denoising |
| 51–75 | Stronger spatial and temporal denoising |
| 76–100 | Strong denoising; fine details may be lost |

Higher pre-process values mean stronger, more lossy filtering, not higher visual fidelity. Denoising can reduce sensor noise and small frame-to-frame changes, which may improve delta and LZ4 compression. It can also remove texture and fine detail, so compare the output visually. Start around 20–40 for noisy footage; use 0 for clean animation, UI recordings, or other sharp material.

Example for a moderately denoised source with a high colour budget:

```bash
python3 tools/convert.py input.mp4 \
    --output output.cpv \
    --width 132 \
    --height 80 \
    --fps 5 \
    --palette-quality 75 \
    --preprocess-quality 35
```

Use `--palette-quality` to trade colour accuracy for a smaller palette, and `--preprocess-quality` to trade fine image detail for smoother frames and potentially better temporal compression. The eight base colours are guaranteed exactly; remaining entries are video-adaptive.

### Additional options

- `--keyframe-interval SECONDS`: target interval between I-frames; default 2 seconds, maximum 10 seconds. Use `10` for smaller files; seeking then needs to decode more dependent frames.
- `--fit contain`: preserve aspect ratio with letterboxing (default).
- `--fit stretch`: stretch the image to the exact output dimensions.
- `--background RRGGBB`: letterbox background colour (default `000000`).
- `--palette-quality 0..100`: global colour palette budget and RGB565-preserving RAW fallback (default `75`).
- `--preprocess-quality 0..100`: spatial/temporal `hqdn3d` denoising strength; `0` disables pre-processing (default `0`).
- `--no-delta-mask`: disable mixed SKIP/data delta-mask packets.
- `--no-frame-compression` (alias `--no-lz4`): disable LZ4-HC and DEFLATE compression.
- `--ffmpeg PATH`: select an FFmpeg executable.
- `--ffprobe PATH`: select an FFprobe executable.

Run `python3 tools/convert.py --help` for the complete option list.

The converter reports the global palette size, palette-index bit width, input and stored frame counts, keyframe count, duration, file size, packet counts/bytes by packet type, LZ4-HC and DEFLATE frame counts, and total compression savings.

## Play a CPV file on the host

```bash
python3 tools/play.py output.cpv
```

The desktop player validates the file header, global palette, frame records and flags, packet streams (including bit-packed indices and DELTA_MASK), compressed payload size limits, and keyframe index before or during playback. It supports sequential playback and keyframe-based seeking. LZ4-compressed frames require `python-lz4`; DEFLATE uses the Python standard library.

Controls:

- **Space**: pause or resume.
- **Left Arrow** / **−1 s**: seek backward by one second.
- **Right Arrow** / **+1 s**: seek forward by one second.
- **Home**: jump to the beginning.
- **End**: jump to the end.

The preview player requires a working graphical desktop session and Tkinter. It is a host-side reference decoder, not part of the calculator application.

## CPV v1 format

See [cpv_v1.md](cpv_v1.md) for the complete byte-level container, packet, encoder, decoder, validation, and test specification.

CPV v1.2 uses a 64-byte little-endian file header, one global RGB565 palette, 20-byte frame headers, four primary packet types (INDEX_LITERAL, INDEX_REPEAT, SKIP, RAW565), the DELTA_MASK SKIP extension, optional independent LZ4 or DEFLATE frame payloads, I/P-frames, and a 12-byte keyframe index entry format. The file header and frame-header sizes remain unchanged from v1.0; the version minor field is 2.

The host player accepts CPV v1.0, v1.1 and v1.2. Version 1.0 literals use one byte per index and do not support DELTA_MASK or compressed frame flags. Version 1.1 supports LZ4 only. New conversions use v1.2. Neither version is backward-compatible with the legacy `CP` format or the earlier experimental tile-based CPV draft. Those files MUST be converted again with this version of `convert.py`.
