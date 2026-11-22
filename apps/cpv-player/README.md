# CPV Player

A Hollyhock 2.1 application for playing CPV video files on the Casio fx-CP400. The host-side CPV v1.3 encoder is implemented in native C++; tools/play.py remains the Python reference decoder and desktop preview player.

## Host encoder requirements

- C++17 compiler (GCC or Clang).
- FFmpeg and FFprobe executables.
- zlib and LZ4 development libraries.
- Python 3, NumPy and Tkinter only for the reference preview player and integration tests.

On Arch Linux / CachyOS:

~~~bash
sudo pacman -S base-devel ffmpeg zlib lz4 python-numpy tk
~~~

On Debian / Ubuntu:

~~~bash
sudo apt install build-essential ffmpeg zlib1g-dev liblz4-dev python3-numpy python3-tk
~~~

## Build the native converter

From the application directory:

~~~bash
make -C tools
~~~

This creates tools/cpv_convert. The host build uses C++17, -O3, link-time optimisation and native CPU tuning by default. Override CXX, CXXFLAGS, CPPFLAGS, LDFLAGS or LDLIBS when needed.

## Convert a video

~~~bash
tools/cpv_convert input.mp4 \
    --output output.cpv \
    --width 528 \
    --height 320 \
    --fps 10 \
    --palette-quality 75 \
    --preprocess-quality 35
~~~

Example matching the compact fx-CP400 profile:

~~~bash
tools/cpv_convert input.mp4 \
    --output output.cpv \
    --width 132 \
    --height 74 \
    --fps 5 \
    --palette-quality 80 \
    --preprocess-quality 100 \
    --keyframe-interval 10 \
    --background 000000
~~~

The input must be readable by FFmpeg. CPV v1 supports dimensions from 1×1 up to 528×320 pixels and frame rates greater than 0 and no greater than 30 FPS. By default, the converter preserves the source aspect ratio and fills unused space with black. Use --fit stretch to stretch to the exact target dimensions.

FFmpeg performs video decoding, scaling, frame-rate conversion, hqdn3d filtering and palette generation. The computationally intensive encoder path is native C++: RGB565 conversion, 65,536-entry nearest-palette lookup, frame comparison, packet planning, bit packing, DELTA_MASK selection, global-motion search and prediction, compression selection, and CPV container output. Frames are streamed from FFmpeg and completed frame records are written to a temporary file; the complete video is never accumulated in RAM.

The adaptive global palette preserves the original 36-colour seed exactly: 12 hues (red, green, blue, yellow, cyan, magenta, orange, pink, spring green, violet, brown and grey), each in dark, normal and light variants. FFmpeg selects the remaining colours from the FPS-reduced and scaled source before denoising. Transparent palette padding is ignored and the first 36 palette entries always retain the seed order.

## Compression and quality settings

CPV v1.1–v1.3 applies these optimisations:

1. **Bit-packed palette indices.** INDEX_LITERAL and DELTA_MASK indices use the minimum width for the active palette: 4 bits for up to 16 colours, 5 bits for up to 32, 6 bits for up to 64, 7 bits for up to 128, and 8 bits for up to 256. INDEX_REPEAT keeps a one-byte index.
2. **DELTA_MASK packets.** Mixed spans of changed and unchanged pixels are represented by a bitmask. The encoder compares the complete byte cost against ordinary SKIP plus data packets and emits the mask only when it is smaller.
3. **Per-frame LZ4-HC vs DEFLATE.** The encoder compares LZ4 high-compression level 9 with zlib DEFLATE level 9 and chooses the smaller stream, including the four-byte decoded-size prefix. A frame is left uncompressed unless compression saves at least 1% or 16 bytes.
4. **Global motion prediction (v1.3).** The encoder searches for a whole-frame translation of up to eight pixels in either axis. It encodes and compresses both ordinary and motion-predicted P-frames, retaining motion only when the complete stored frame is smaller.

### Palette quality

--palette-quality controls the global palette budget:

| Palette quality | Palette budget | Behaviour |
|---:|---:|---|
| 0–39 | 36 legacy seed colours | Strong colour reduction |
| 40–59 | 64 colours | Balanced colour reduction |
| 60–79 | 128 colours | Higher colour fidelity |
| 80–99 | 256 colours | Highest palette budget |
| 100 | Up to 256 + RAW565 | Preserve every source RGB565 value |

Quality 100 uses RAW565 packets for pixels that cannot be represented exactly by the palette. It preserves the filtered/scaled source at RGB565 precision but can produce larger files.

### Pre-processing quality

--preprocess-quality controls FFmpeg's hqdn3d spatial and temporal denoising filter. It is applied after frame-rate reduction and scaling. The palette is generated before denoising.

| Pre-process quality | Behaviour |
|---:|---|
| 0 | Disabled; no denoising (default) |
| 1–25 | Very mild denoising |
| 26–50 | Mild to moderate denoising |
| 51–75 | Stronger spatial and temporal denoising |
| 76–99 | Stronger denoising; fine details may be lost |
| 100 | FFmpeg hqdn3d defaults (4:3:6:4.5) |

Higher values mean stronger and more lossy filtering, not higher visual fidelity. Denoising may reduce noise and frame-to-frame changes, improving compression, but can also remove texture and fine detail. Start around 20–40 for noisy footage; use 0 for clean animation, UI recordings or sharp material.

### Other options

- --keyframe-interval SEC: target interval between I-frames; default 2 seconds, maximum 10 seconds.
- --fit contain|stretch: preserve aspect ratio with letterboxing (default) or stretch to exact dimensions.
- --background RRGGBB: letterbox colour (default 000000).
- --no-global-motion: disable motion analysis and prediction.
- --no-delta-mask: disable mixed SKIP/data masks.
- --no-frame-compression or --no-lz4: disable LZ4-HC and DEFLATE.
- --ffmpeg PATH, --ffprobe PATH: select external FFmpeg executables.
- --self-test: run native packet, bit-packing, palette and motion tests.

Run tools/cpv_convert --help for the full command-line help. The converter reports the palette size, frame counts, keyframes, duration, compressed frame counts, compression and motion savings, total file size, conversion time and packet sizes by type.

## Play a CPV file on the host

~~~bash
python3 tools/play.py output.cpv
~~~

The desktop player validates the file header, global palette, frame records and flags, packet streams (including bit-packed indices, DELTA_MASK and global-motion prediction), compressed payload bounds, and keyframe index. It supports sequential playback and keyframe-based seeking. The preview player requires NumPy, Tkinter and a graphical desktop session.

Controls:

- **Space**: pause or resume.
- **Left Arrow** / **−1 s**: seek backward by one second.
- **Right Arrow** / **+1 s**: seek forward by one second.
- **Home**: jump to the beginning.
- **End**: jump to the end.

The calculator-side decoder must apply v1.3 global motion before decoding the associated P-frame packets. A no-allocation in-place C++ helper is provided in cpv_motion.hpp and tested by tools/test_motion.cpp; it is not yet wired into the calculator playback loop.

## CPV v1 format

See cpv_v1.md for the complete byte-level container, packet, encoder, decoder, validation and test specification. The modern CPV v1 format is not backward-compatible with the old legacy CP file format or the earlier experimental tile-based CPV draft. Legacy files must be converted again with the CPV v1.3 encoder.
