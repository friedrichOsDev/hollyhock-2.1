# CPV Player

The host-side CPV v1.3 encoder is implemented in native C++; tools/play.py is the validated Python reference decoder and desktop preview player. The Casio application now includes a streaming CPV v1.3 decoder and a UI-Library-based playback screen. It supports I/P frames, bit-packed INDEX_LITERAL, INDEX_REPEAT, SKIP, DELTA_MASK, RAW565, LZ4, zlib/DEFLATE, global-motion prediction, and keyframe seeking. The player uses a separate RGB565 reference surface and scales frames through the UI Library framebuffer. Hardware playback and timing still require verification on a real fx-CP400.

## Calculator player

The calculator app uses the UI Library for the framebuffer-backed main menu, `.cpv` file selector, persistent settings and calculator lifecycle. Select **Play video** and choose a CPV file from `fls0`. The decoder validates the file header, palette, frame records, frame flags, packet bounds and keyframe index before playback.

The calculator decoder streams frame payloads from the file; it does not load the complete video into RAM. The current implementation accepts files up to 2 GiB (the SDK file-seek API uses signed offsets) and at most 65,535 indexed I-frames to keep its index allocation bounded. It holds a 256-entry RGB565 palette, the keyframe index, a bounded stored/decompressed frame buffer and one persistent RGB565 reference image. For the selected 100×60 profile, the reference image itself occupies only 12,000 bytes. The reference image is kept separate from LCD VRAM so that the UI overlay cannot contaminate SKIP/DELTA_MASK reconstruction.

Playback controls: **EXE** pauses/resumes, **Left/Right** seek by 5 seconds, **Up/Down** seek by 20 seconds, and **Backspace/Power-Clear** or the action-bar Escape event exits playback. Settings let you repeat the video, show/hide the HUD, and enable **Debug mode**. EXE and the middle third of the touchscreen pause/resume; the left/right thirds seek by 5 seconds. Touch-down and touch-up are both accepted, with one action per touch. Left/Right seek by 5 seconds and Up/Down by 20 seconds.

**Timing limitation:** this SDK version exposes non-blocking input polling but no public monotonic clock or sleep function. `kSpinIterationsPerMs` is currently set to `0U`, as a deliberate no-wait debug setting. Frame pacing is therefore not real-time and must not be used to judge playback FPS. Accurate timing should ultimately use a timer service in the SDK/UI Library.

### Debug mode

Enable **Debug mode** in the app settings before opening a video. Instead of displaying the video image, the screen displays a live text diagnostic view. It shows the current operation (file open, header/palette read, frame/index validation, payload read, DEFLATE/LZ4 decompression, global-motion prediction, packet decoding, seeking or frame completion), the current work counter, the current frame, read bytes, stored/decompressed frame sizes, packet counts by opcode, compression method and number of frames decoded during a seek.

The decoder reports phase changes before long operations and periodic progress while scanning frame/index records, reading large payloads, seeking and decoding packet streams. This helps distinguish time spent in file I/O, decompression, packet parsing and framebuffer output. The current SDK exposes no monotonic timer, so this debug view intentionally reports operation names and work counters rather than claiming unreliable millisecond measurements.

The UI Library framebuffer includes `BlitRGB565()` for bounded RGB565 image transfer and integer nearest-neighbour scaling. It avoids per-pixel calls through the public API and does not allocate a second destination image.

Build the calculator application with the SH-4 toolchain:

~~~sh
apps/cpv-player/build.sh
~~~

or, after building the SDK and UI Library:

~~~sh
make -C apps/cpv-player SDK_DIR=../../sdk UI_DIR=../../ui all
~~~

Copy `apps/cpv-player/cpv_player.hhk` to `fls0` and start it from Hollyhock 2.1. (The cross-toolchain build and real-device playback must still be checked on the target setup.)

## Host encoder requirements

- C++17 compiler (GCC or Clang).
- FFmpeg and FFprobe executables.
- zlib, LZ4 and libdeflate development libraries.
- Python 3, NumPy and Tkinter only for the reference preview player and integration tests.

On Arch Linux / CachyOS:

~~~bash
sudo pacman -S base-devel ffmpeg zlib lz4 libdeflate python-numpy tk
~~~

On Debian / Ubuntu:

~~~bash
sudo apt install build-essential ffmpeg zlib1g-dev liblz4-dev libdeflate-dev python3-numpy python3-tk
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

FFmpeg performs video decoding, scaling, frame-rate conversion, hqdn3d filtering and palette generation. The computationally intensive encoder path is native C++: RGB565 conversion, 65,536-entry nearest-palette lookup, frame comparison, packet planning, bit packing, compression-aware DELTA_MASK selection, global-motion search and prediction, compression selection, and CPV container output. Frames are streamed from FFmpeg and completed frame records are written to a temporary file; the complete video is never accumulated in RAM.

The adaptive global palette preserves the original 36-colour seed exactly: 12 hues (red, green, blue, yellow, cyan, magenta, orange, pink, spring green, violet, brown and grey), each in dark, normal and light variants. FFmpeg selects the remaining colours from the FPS-reduced and scaled source before denoising. Transparent palette padding is ignored and the first 36 palette entries always retain the seed order.

## Compression and quality settings

CPV v1.1–v1.3 applies these optimisations:

1. **Bit-packed palette indices.** INDEX_LITERAL and DELTA_MASK indices use the minimum width for the active palette: 4 bits for up to 16 colours, 5 bits for up to 32, 6 bits for up to 64, 7 bits for up to 128, and 8 bits for up to 256. INDEX_REPEAT keeps a one-byte index.
2. **Compression-aware DELTA_MASK packets.** For P-frames the encoder builds three alternatives: no masks, the original per-chunk byte-size heuristic, and masks for every eligible mixed span. It compares complete packet streams using a level-12 libdeflate zlib-size probe (including the four-byte uncompressed-size prefix), then runs the full compression portfolio only for the selected packetisation. This lets a locally larger mask survive when it produces a smaller compressed frame, without multiplying the full portfolio cost across every alternative.
3. **Per-frame compression portfolio.** For each frame the encoder tests eight zlib/DEFLATE configurations (levels 1, 3, 6, 9 and four level-9 strategies), LZ4-HC levels 1, 6, 9 and 12, and libdeflate zlib levels 6, 9, 10 and 12. Raw payload is also a candidate. The winner is selected by actual stored size, including the four-byte decoded-size prefix; compressed data is used only when it is strictly smaller than raw. libdeflate uses the standard zlib wrapper, so all candidates use existing CPV compression flags and no format version bump is needed.
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
- --no-delta-mask: disable all DELTA_MASK packetisation trials.
- --no-frame-compression or --no-lz4: disable LZ4-HC and DEFLATE.
- --ffmpeg PATH, --ffprobe PATH: select external FFmpeg executables.
- --self-test: run native packet, bit-packing, palette and motion tests.

Run tools/cpv_convert --help for the full command-line help. The converter reports the palette size, frame counts, keyframes, duration, compression codec counts and winning candidate per frame, DELTA_MASK packetisation winners, compression and motion savings, total file size, conversion time and packet sizes by type.

## Reproducible quality benchmark

Run `python3 apps/cpv-player/tools/benchmark_quality.py [input-video.mp4]` from the repository root. The default input is `apps/cpv-player/tools/2026-11-21_200236.mp4`. The benchmark fixes the output profile to 132×74 at 5 FPS, a 10-second keyframe interval, contain-fit and a black background. It runs nine cases: palette quality 25, 50, 70, 80 and 100 at preprocess quality 100; then preprocess quality 0, 25, 50 and 75 at palette quality 80 (the 80/100 baseline is shared). This isolates palette budget and denoising effects instead of changing both at once.

Every run writes a new CPV file and converter log. `manifest.json` records the exact source-file size, SHA-256 and FFprobe metadata, as well as the shared output profile. `summary.csv` records file size, palette/frame/compression statistics, conversion time, full reference-decoder time and PSNR. PSNR is measured against one common FFmpeg reference at the same output dimensions and FPS but without denoising; therefore it includes both palette/RGB565 error and the visual change caused by preprocessing. Every generated CPV is fully decoded during the benchmark. The benchmark output directory is newly created and is never overwritten.

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
