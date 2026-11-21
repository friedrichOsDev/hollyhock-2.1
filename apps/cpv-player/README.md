# CPV Player

A Hollyhock 2.1 application for playing CPV video files on the Casio fx-CP400.

## Host tools

The host-side tools are located in the \`tools/\` directory. Run the commands below from this application directory.

### Requirements

- Python 3.10 or newer.
- FFmpeg and FFprobe for video conversion.
- Tkinter for the desktop preview player (often provided by a separate system package on Linux).

### Convert a video

\`\`\`bash
python3 tools/convert.py <video-file> --output <output.cpv> --width <width> --height <height> --fps <fps>
\`\`\`

Example:

\`\`\`bash
python3 tools/convert.py input.mp4 --output output.cpv --width 528 --height 320 --fps 10
\`\`\`

The input must be readable by FFmpeg. Output dimensions are limited to 528 × 320 pixels and the frame rate must not exceed 60 FPS. By default, the converter preserves the source aspect ratio and fills unused space with black.

Optional conversion settings:

- \`--quality 0..100\`: encoder-side palette budget; default is 75. Lower values allow more aggressive colour quantisation. Quality 100 avoids additional lossy quantisation beyond RGB565 conversion.
- \`--fit contain|stretch\`: preserve the aspect ratio with letterboxing, or stretch to the requested dimensions.
- \`--background RRGGBB\`: letterbox colour (default: \`000000\`).
- \`--keyframe-interval SECONDS\`: target keyframe interval (default: 1.0).

Run \`python3 tools/convert.py --help\` for all options.

### Play a CPV file on the host

\`\`\`bash
python3 tools/play.py <video.cpv>
\`\`\`

The preview window starts playback immediately. Use:

- **Space** to pause or resume.
- **Left Arrow** or the **−1 s** button to seek backward by one second.
- **Right Arrow** or the **+1 s** button to seek forward by one second.

### Format specification

See [cpv_v1.md](cpv_v1.md) for the complete CPV v1.0 binary format and codec specification.
