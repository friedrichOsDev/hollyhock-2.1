# CPV Player

A Hollyhock 2.1 application for playing CPV v1.1 video files on the Casio fx-CP400. The host tools also read earlier CPV v1.0 files. The `tools/` directory also contains a desktop encoder and a reference preview player.

## Host tool requirements

- Python 3.10 or newer.
- NumPy.
- `lz4` Python package (`python-lz4` on Arch/CachyOS) for the default per-frame compression.
- FFmpeg and FFprobe.
- Tkinter for the desktop preview player.

On Arch Linux / CachyOS, install the dependencies with:

```bash
sudo pacman -S ffmpeg python-numpy python-lz4 tk
```

On other Linux distributions, install NumPy, FFmpeg, FFprobe, and the Python Tkinter package using the distribution's package manager.

Verify the Python modules:

```bash
python3 -c "import numpy, lz4.block; print(numpy.__version__, lz4.__version__)"
python3 -m tkinter
```

The converter uses NumPy to accelerate RGB565 conversion, palette lookup, bit-packing, delta analysis, and packet preparation. The desktop player uses NumPy for frame decoding and RGB preview conversion, and python-lz4 for independently compressed frame payloads.

## Convert a video

Run from this application directory:

```bash
python3 tools/convert.py <video-file> \
    --output <output.cpv> \
    --width <width> \
    --height <height> \
    --fps <fps> \
    --quality <0..100>
```

Example for the calculator's maximum resolution and target frame rate:

```bash
python3 tools/convert.py input.mp4 \
    --output output.cpv \
    --width 528 \
    --height 320 \
    --fps 10 \
    --quality 75
```

The input must be readable by FFmpeg. CPV v1 supports dimensions from 1×1 up to 528×320 pixels and frame rates greater than 0 and no greater than 30 FPS.

By default, the converter preserves the source aspect ratio and fills unused space with black. It builds one global palette for the complete video, maps source RGB565 pixels through a precomputed lookup table, and writes linear I/P-frame packet streams. The converter streams completed frame records to a temporary output file rather than accumulating the complete compressed video in memory.

### Compression settings

CPV v1.1 applies three independent optimisations:

1. **Bit-packed palette indices.** INDEX_LITERAL and DELTA_MASK indices use the minimum width for the active palette: 4 bits for up to 16 colours, 5 bits for up to 32, 6 bits for up to 64, 7 bits for up to 128, and 8 bits for up to 256. INDEX_REPEAT keeps a one-byte index.
2. **DELTA_MASK packets.** Mixed spans of changed and unchanged pixels are represented by a bitmask. The encoder compares the complete byte cost against ordinary SKIP plus data packets and emits the mask only when it is smaller.
3. **Per-frame LZ4.** Each frame's packet stream can be compressed as an independent LZ4 block. It is stored only when the four-byte decoded-size prefix plus compressed data is smaller than the uncompressed packet stream. No LZ4 dictionary is shared between frames, so keyframe seeking remains simple.

The converter enables DELTA_MASK and LZ4 by default. Use `--no-delta-mask` and/or `--no-lz4` to disable them for benchmarks. The resulting statistics let you compare each optimisation independently.

### Quality settings

`--quality` controls the global palette budget:

| Quality | Palette budget | Behaviour |
|---:|---:|---|
| 0–14 | 16 colours | Strong quantisation, smallest palette |
| 15–34 | 32 colours | Aggressive compression |
| 35–54 | 64 colours | Balanced compression |
| 55–74 | 128 colours | Higher colour fidelity |
| 75–99 | 256 colours | Highest palette quality |
| 100 | Up to 256 + RAW565 | Preserve every source RGB565 value |

Quality 100 uses RAW565 packets for pixels that cannot be represented exactly by the global palette. It is intended for lossless-within-RGB565 output and can produce substantially larger files. For normal calculator playback, start with quality 50–75 and compare the result visually.

### Additional options

- `--keyframe-interval SECONDS`: target interval between I-frames; default 2 seconds, maximum 10 seconds.
- `--fit contain`: preserve aspect ratio with letterboxing (default).
- `--fit stretch`: stretch the image to the exact output dimensions.
- `--background RRGGBB`: letterbox background colour (default `000000`).
- `--no-delta-mask`: disable mixed SKIP/data delta-mask packets.
- `--no-lz4`: disable optional independent per-frame LZ4 compression.
- `--ffmpeg PATH`: select an FFmpeg executable.
- `--ffprobe PATH`: select an FFprobe executable.

Run `python3 tools/convert.py --help` for the complete option list.

The converter reports the global palette size, palette-index bit width, input and stored frame counts, keyframe count, duration, file size, packet counts/bytes by packet type, the number of LZ4-compressed frames, and the bytes saved by LZ4.

## Play a CPV file on the host

```bash
python3 tools/play.py output.cpv
```

The desktop player validates the file header, global palette, frame records and flags, packet streams (including bit-packed indices and DELTA_MASK), LZ4 size limits, and keyframe index before or during playback. It supports sequential playback and keyframe-based seeking. Compressed frames require `python-lz4`.

Controls:

- **Space**: pause or resume.
- **Left Arrow** / **−1 s**: seek backward by one second.
- **Right Arrow** / **+1 s**: seek forward by one second.
- **Home**: jump to the beginning.
- **End**: jump to the end.

The preview player requires a working graphical desktop session and Tkinter. It is a host-side reference decoder, not part of the calculator application.

## CPV v1 format

See [cpv_v1.md](cpv_v1.md) for the complete byte-level container, packet, encoder, decoder, validation, and test specification.

CPV v1.1 uses a 64-byte little-endian file header, one global RGB565 palette, 20-byte frame headers, four primary packet types (INDEX_LITERAL, INDEX_REPEAT, SKIP, RAW565), the DELTA_MASK SKIP extension, optional independently compressed LZ4 frame payloads, I/P-frames, and a 12-byte keyframe index entry format. The file header and frame-header sizes remain unchanged from v1.0; the version minor field is 1.

The host player accepts both CPV v1.0 and v1.1. Version 1.0 literals use one byte per index and do not support DELTA_MASK or compressed frame flags. New conversions use v1.1. Neither version is backward-compatible with the legacy `CP` format or the earlier experimental tile-based CPV draft. Those files MUST be converted again with this version of `convert.py`.
