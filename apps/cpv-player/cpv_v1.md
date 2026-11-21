# CPV Video Format Specification

**Document:** CPV v1.0  
**File extension:** `.cpv`  
**Status:** Proposed specification — implementation baseline  
**Target platform:** Casio fx-CP400 running Hollyhock 2.1  
**Primary playback target:** 528 × 320 pixels at 10 frames per second  
**Maximum frame dimensions:** 528 × 320 pixels  
**Maximum nominal frame rate:** 30 FPS  
**Pixel storage:** RGB565  
**Byte order:** Little-endian  
**Audio:** Not supported

---

## 1. Purpose and design principles

CPV is a compact video container and codec intended for reliable software decoding on the Casio fx-CP400. The desktop encoder may perform comparatively expensive analysis, but the calculator decoder must remain small, deterministic, and fast.

CPV v1 uses a **single video-wide colour palette** and a **linear, raster-ordered frame token stream**. Delta frames describe only changes relative to the image reconstructed from the preceding frame. The format deliberately avoids per-tile palettes, recursive tile trees, motion-vector search, and entropy coders.

The design is based on five principles:

1. **A global palette:** A single palette is generated for the complete video. It avoids repeating local palette data throughout the file and lets the encoder build one reusable colour lookup table.
2. **Linear delta coding:** Pixels are processed from left to right and top to bottom. Unchanged consecutive pixels are represented by SKIP packets.
3. **Efficient changed-pixel coding:** Changed pixels can be stored as palette-index literals, repeated palette indices, or direct RGB565 values.
4. **Simple decoding:** The calculator only reads packet headers, copies values, repeats values, or leaves previous pixels unchanged.
5. **Bounded seeking:** Periodic keyframes and a keyframe index allow playback to resume without decoding the complete video from its beginning.

The format is suitable for mixed content: animation, live-action video, gameplay, and screen recordings. It does not assume that every frame is mostly static or that every region has a small local palette.

### 1.1 Explicit non-goals

CPV v1 does not define:

- Audio, subtitles, or multiple tracks.
- Motion compensation or motion vectors.
- B-frames or bidirectional prediction.
- YUV or chroma-subsampled pixel formats.
- Huffman, arithmetic, or context-adaptive entropy coding.
- Alpha channels.
- Decoder-side scaling or image enhancement.
- A mandatory palette-generation implementation.
- A mandatory dithering algorithm.

These features are outside the v1 format. A future incompatible format revision may introduce them.

### 1.2 Normative language

The terms **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and **MAY** are normative. Requirements using MUST or MUST NOT are necessary for v1 conformance.

---

## 2. Core architecture

A CPV v1 file contains:

1. A fixed-size file header.
2. One global RGB565 palette.
3. A sequence of frame records.
4. A keyframe index at the end of the file.

Each frame reconstructs one complete displayed image. A frame is either:

- **I-frame (keyframe):** Reconstructs every pixel without referring to an earlier frame.
- **P-frame (delta frame):** Starts from the preceding reconstructed image and applies packets that update selected pixel ranges.

Frames are processed in row-major order:

`pixel_index = y * width + x`

The first pixel is the top-left pixel. The final pixel is the bottom-right pixel.

There is no tile grid. Packet boundaries may occur at any pixel position, including across scanline boundaries. This is intentional: the encoder can describe long unchanged or repeated runs without paying per-tile metadata overhead.

### 2.1 Reconstruction state

The reference image for a P-frame is the **decoded RGB565 image from the preceding frame**, not the original source frame and not an unquantized intermediate image.

The encoder MUST maintain the same reconstructed image that a conforming decoder would produce. This rule prevents drift between encoder-side comparisons and decoder output.

An I-frame replaces the complete reconstruction state. A P-frame modifies that state in place.

### 2.2 Global palette

The file has exactly one palette. Each palette entry is a 16-bit RGB565 colour.

Palette indices are one byte wide, even when the palette contains fewer than 256 entries. This keeps the decoder simple and avoids bit-level extraction in the hot pixel loop.

A palette index MUST be in the range `0 .. palette_count - 1`.

The RAW565 packet type bypasses the palette and stores direct RGB565 pixels. It is intended for image details that would otherwise suffer excessive palette quantisation.

---

## 3. Limits and supported properties

| Property | CPV v1 requirement |
|---|---|
| Minimum width | 1 pixel |
| Maximum width | 528 pixels |
| Minimum height | 1 pixel |
| Maximum height | 320 pixels |
| Maximum pixels per frame | 168,960 |
| Nominal frame rate | Greater than 0 and no greater than 30 FPS |
| Recommended frame rate | 10 FPS |
| Palette entries | 1–256 |
| Pixel format | RGB565 |
| Frame types | I-frame and P-frame |
| Audio | Not supported |
| Alpha | Not supported |
| Inter-frame references | Previous reconstructed frame only |
| Maximum keyframe interval | 2 seconds recommended; MUST NOT exceed 10 seconds |
| Byte order | Little-endian |

The width and height limits describe the encoded frame dimensions. A smaller video is valid. The decoder MUST NOT upscale or downscale the image as part of decoding.

The nominal frame rate is stored as a rational number. For the standard 10 FPS profile, the values are `fps_num = 10` and `fps_den = 1`.

Frame durations are stored individually. Consequently, a file may contain variable-duration frames or coalesced identical frames while retaining a nominal frame-rate value.

---

## 4. Primitive data types and byte order

All multi-byte integer fields in CPV v1 are **little-endian**.

| Type | Size | Description |
|---|---:|---|
| `uint8` | 1 byte | Unsigned 8-bit integer |
| `uint16` | 2 bytes | Unsigned 16-bit integer |
| `uint32` | 4 bytes | Unsigned 32-bit integer |
| `uint64` | 8 bytes | Unsigned 64-bit integer |
| `char[n]` | n bytes | Fixed byte sequence |

Implementations MUST read and write fields explicitly in little-endian order. They MUST NOT rely on native C/C++ structure alignment or host byte order.

All offsets in the file are absolute byte offsets from the beginning of the file.

All reserved fields MUST be written as zero. A v1 decoder MUST reject a file if a reserved field or reserved flag bit is non-zero.

---

## 5. RGB565 pixel format

RGB565 stores one pixel in 16 bits:

- Red: 5 bits.
- Green: 6 bits.
- Blue: 5 bits.

Bit layout:

```text
15             11 10               5 4              0
+----------------+------------------+----------------+
|    Red (5)     |    Green (6)      |    Blue (5)    |
+----------------+------------------+----------------+
```

The packed value is:

`rgb565 = (red5 << 11) | (green6 << 5) | blue5`

For a component in the 8-bit range, a conventional conversion is:

```text
red5   = (red8   * 31 + 127) / 255
green6 = (green8 * 63 + 127) / 255
blue5  = (blue8  * 31 + 127) / 255
```

The resulting 16-bit value is stored little-endian.

Palette entries and RAW565 pixel values use exactly the same RGB565 representation.

A decoder MUST interpret RGB565 values directly. It MUST NOT apply additional palette quantisation.

---

## 6. File layout

A CPV v1 file is laid out as follows:

```text
+------------------------------+
| File Header (80 bytes)       |
+------------------------------+
| Global Palette               |
| palette_count × 2 bytes      |
+------------------------------+
| Frame Record 0 (I-frame)     |
+------------------------------+
| Frame Record 1               |
+------------------------------+
| ...                          |
+------------------------------+
| Frame Record N-1             |
+------------------------------+
| Keyframe Index               |
+------------------------------+
| End of file                  |
+------------------------------+
```

The palette immediately follows the 80-byte file header. Frame records immediately follow the palette. The index immediately follows the final frame record.

No padding or alignment bytes are permitted between these structures.

---

## 7. File header

The file header is exactly **80 bytes**.

| Offset | Field | Type | Size | Required value / meaning |
|---:|---|---|---:|---|
| 0 | `magic` | char[4] | 4 | ASCII `CPV\0` |
| 4 | `version_major` | uint16 | 2 | 1 |
| 6 | `version_minor` | uint16 | 2 | 0 |
| 8 | `header_size` | uint32 | 4 | 80 |
| 12 | `flags` | uint32 | 4 | 0 in v1 |
| 16 | `width` | uint16 | 2 | 1–528 |
| 18 | `height` | uint16 | 2 | 1–320 |
| 20 | `fps_num` | uint32 | 4 | Positive numerator |
| 24 | `fps_den` | uint32 | 4 | Positive denominator |
| 28 | `frame_count` | uint32 | 4 | Number of frame records |
| 32 | `palette_count` | uint16 | 2 | 1–256 |
| 34 | `reserved0` | uint16 | 2 | 0 |
| 36 | `palette_offset` | uint64 | 8 | MUST equal 80 |
| 44 | `frames_offset` | uint64 | 8 | First frame record |
| 52 | `index_offset` | uint64 | 8 | Keyframe index |
| 60 | `duration_ms` | uint64 | 8 | Sum of all frame durations |
| 68 | `reserved1` | uint8[12] | 12 | All zero |
| | **Total** | | **80** | |

### 7.1 Header validation

A v1 decoder MUST verify all of the following before playback:

- The magic value is exactly `CPV\0`.
- The major version is 1. A minor version greater than 0 MUST be rejected by a strict v1.0 decoder.
- `header_size` is exactly 80.
- `flags`, `reserved0`, and every byte of `reserved1` are zero.
- Width, height, frame rate, frame count, and palette count are within their allowed ranges.
- `fps_num` and `fps_den` are non-zero.
- The rational frame rate does not exceed 30 FPS.
- `palette_offset` is exactly 80.
- `frames_offset` equals `palette_offset + palette_count × 2`.
- `index_offset` is greater than `frames_offset` and lies within the file.
- The index and frame records fit inside the actual file size.
- The sum of frame durations equals `duration_ms`.

All offset and size calculations MUST use overflow-safe arithmetic.

---

## 8. Global palette block

The palette starts at `palette_offset` and contains exactly `palette_count` entries.

Each entry is:

| Field | Type | Size |
|---|---|---:|
| `colour` | uint16 | 2 bytes |

The entries are in palette-index order. Entry zero corresponds to index zero.

Each entry is a little-endian RGB565 value.

The palette block size is:

`palette_size = palette_count × 2`

The first frame begins at:

`frames_offset = palette_offset + palette_size`

A decoder MUST read the complete palette before decoding the first frame. It SHOULD keep the palette in a small fixed array of 256 RGB565 entries.

### 8.1 Palette generation

Palette generation is an encoder operation and is not part of decoding. The format does not prescribe a specific quantisation algorithm.

A conforming encoder:

- MUST generate one palette for the complete video.
- MUST use between 1 and 256 entries.
- MUST map every palette-coded pixel to a valid palette entry.
- SHOULD build the palette using representative samples from the complete video, not only the first frame.
- SHOULD weight temporally important or visually representative frames during palette generation.
- MAY use FFmpeg palette-generation filters or a custom quantiser.
- MAY apply controlled denoising before palette generation and frame encoding.

A single global palette avoids the repeated palette headers and repeated nearest-colour searches associated with independent local palettes.

### 8.2 Palette mapping and lookup tables

The encoder SHOULD precompute a mapping from RGB565 source colours to the nearest palette entry.

There are only 65,536 possible RGB565 values. A 65,536-byte lookup table can therefore map every possible RGB565 value to its palette index.

This changes per-pixel palette mapping from a search through up to 256 colours into one table lookup.

The encoder MAY use a different mapping strategy, provided the output indices refer to the palette stored in the file.

The exact colour-distance function is encoder-defined. A recommended integer metric is:

`D = 3 × ΔR² + 4 × ΔG² + 2 × ΔB²`

where the component differences are measured in their native RGB565 ranges (5, 6, and 5 bits).

---

## 9. Frame record

Every frame is stored as one frame record.

The frame header is exactly **20 bytes**:

| Offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0 | `magic` | char[4] | 4 | ASCII `FRM1` |
| 4 | `frame_type` | uint8 | 1 | 0 = I-frame, 1 = P-frame |
| 5 | `flags` | uint8 | 1 | 0 in v1 |
| 6 | `header_size` | uint16 | 2 | 20 |
| 8 | `frame_number` | uint32 | 4 | Sequential record number, starting at 0 |
| 12 | `duration_ms` | uint32 | 4 | Display duration of this frame |
| 16 | `payload_size` | uint32 | 4 | Number of payload bytes |
| | **Total** | | **20** | |

The payload begins immediately after the frame header and contains exactly `payload_size` bytes.

The next frame record begins at:

`next_frame_offset = current_frame_offset + 20 + payload_size`

### 9.1 Frame numbering

The first frame record MUST have `frame_number = 0` and MUST be an I-frame.

Each subsequent record MUST increment `frame_number` by exactly one.

`frame_count` in the file header is the number of stored frame records. If the encoder combines identical consecutive source frames into one longer-duration record, the combined result counts as one stored frame record.

### 9.2 Frame duration

`duration_ms` is the amount of time for which the reconstructed image is displayed.

A frame duration MUST be greater than zero.

The total video duration is the sum of all frame durations and MUST equal the file header's `duration_ms`.

The nominal frame rate describes the intended output sampling rate. Individual frame durations remain authoritative for playback timing.

---

## 10. Packet stream

The frame payload is a sequence of packets. Each packet describes a run of consecutive pixels in the linear raster order.

There are exactly four packet types:

| Opcode | Name | Purpose |
|---:|---|---|
| `00` | INDEX_LITERAL | A sequence of palette indices |
| `01` | INDEX_REPEAT | Repeat one palette index |
| `10` | SKIP | Preserve pixels from the previous frame |
| `11` | RAW565 | A sequence of direct RGB565 pixels |

The two most significant bits of the first packet byte contain the opcode. The six least significant bits encode the run length.

### 10.1 Packet header and run length

Let `b` be the first packet byte.

```text
  7 6 5 4 3 2 1 0
+-----+-----------+
| OP  | LENGTH    |
+-----+-----------+
  2 bits  6 bits
```

The run length is decoded as follows:

- If `b & 0x3F` is in the range 0–62, the run length is `(b & 0x3F) + 1`.
- If `b & 0x3F` equals 63, the next two bytes contain the run length as a little-endian uint16.
- An extended run length MUST be in the range 64–65,535.

Thus, short packets encode lengths from 1 to 63 using one header byte. Long packets encode lengths from 64 to 65,535 using a three-byte header.

Runs longer than 65,535 pixels MUST be split into multiple packets.

### 10.2 INDEX_LITERAL — opcode 00

INDEX_LITERAL stores a sequence of palette indices.

Packet layout:

```text
+---------------------------+
| Packet header             |
+---------------------------+
| index[0]                  |
| index[1]                  |
| ...                       |
| index[length - 1]         |
+---------------------------+
```

The packet contains exactly `length` index bytes following the packet header.

For every index:

`0 <= index < palette_count`

The decoder writes the corresponding palette RGB565 colour to the output image.

INDEX_LITERAL is intended for changed areas containing several different palette colours that are not efficiently represented by repeated-colour packets.

### 10.3 INDEX_REPEAT — opcode 01

INDEX_REPEAT writes one palette colour repeatedly.

Packet layout:

```text
+---------------------------+
| Packet header             |
+---------------------------+
| palette_index             |
+---------------------------+
```

The decoder reads one index byte and writes the corresponding palette colour `length` times.

The index MUST be smaller than `palette_count`.

INDEX_REPEAT is useful for flat areas, backgrounds, UI elements, and any repeated palette colour.

### 10.4 SKIP — opcode 10

SKIP advances the output position without changing any pixels.

Packet layout:

```text
+---------------------------+
| Packet header             |
+---------------------------+
```

SKIP is valid only in P-frames.

For every pixel covered by SKIP, the decoder keeps the RGB565 value already present at the same position in the previous reconstructed frame.

A decoder MUST reject SKIP in an I-frame.

SKIP is intended for unchanged pixels and is especially effective in animation, screen recordings, static camera scenes, and videos with large stationary backgrounds.

### 10.5 RAW565 — opcode 11

RAW565 stores direct RGB565 pixel values without palette quantisation.

Packet layout:

```text
+---------------------------+
| Packet header             |
+---------------------------+
| pixel[0] : uint16         |
| pixel[1] : uint16         |
| ...                       |
| pixel[length - 1]         |
+---------------------------+
```

Each pixel is a little-endian RGB565 value. The packet contains exactly `length × 2` pixel bytes following the packet header.

RAW565 is intended for areas where mapping to the global palette would create too much visual error. It is also the lossless-within-RGB565 fallback for high-detail content.

The decoder MUST write each raw value directly to the reconstructed image.

### 10.6 Packet position and coverage

Packets are applied in order. The first packet begins at pixel index zero. Every packet advances the current output position by exactly its run length.

The sum of all packet run lengths in a frame MUST equal:

`pixel_count = width × height`

The decoder MUST reject a frame if:

- A packet would advance beyond `pixel_count`.
- The packet payload is truncated.
- A palette index is invalid.
- SKIP appears in an I-frame.
- The packet stream ends before exactly `pixel_count` pixels have been covered.
- Bytes remain unused at the end of the declared frame payload.

Packets may cross scanline boundaries. The decoder MUST NOT assume that packet boundaries align to rows.

---

## 11. Frame reconstruction

### 11.1 I-frame reconstruction

An I-frame defines the complete image and MUST NOT contain SKIP packets.

The decoder starts at pixel position zero and applies packets until exactly `width × height` pixels have been written.

Only after the entire frame payload has been validated and decoded should the frame be considered a valid reference for subsequent P-frames.

### 11.2 P-frame reconstruction

A P-frame is decoded against the immediately preceding reconstructed frame.

The decoder begins at pixel position zero. For each packet:

- INDEX_LITERAL writes the specified palette colours.
- INDEX_REPEAT writes one palette colour repeatedly.
- SKIP preserves the previous RGB565 pixels.
- RAW565 writes the specified direct RGB565 values.

After all packets have been processed, the output position MUST equal `width × height`.

No frame may refer to an arbitrary earlier frame. P-frames refer only to the previous reconstructed frame.

### 11.3 Keyframe interval

The first frame MUST be an I-frame.

Encoders SHOULD create a new I-frame approximately once per second. The interval between I-frames MUST NOT exceed 10 seconds.

For the normal 10 FPS profile, one I-frame per 10 stored source-frame intervals is recommended. Earlier I-frames MAY be emitted after scene changes or when a P-frame would be larger than a new I-frame.

Every I-frame MUST have an entry in the keyframe index.

### 11.4 Scene changes

The encoder MAY detect scene changes and emit an I-frame at the beginning of the new scene.

Scene-change detection is encoder-defined. It is not required for decoding.

---

## 12. Keyframe index

The index is located at the absolute file offset stored in `index_offset`.

The index begins with an 8-byte header:

| Offset | Field | Type | Size |
|---:|---|---|---:|
| 0 | `magic` | char[4] | 4 |
| 4 | `entry_count` | uint32 | 4 |

The magic value MUST be ASCII `IDX1`.

The header is followed by `entry_count` entries. Each entry is exactly 20 bytes:

| Offset | Field | Type | Size |
|---:|---|---|---:|
| 0 | `frame_number` | uint32 | 4 |
| 4 | `elapsed_ms` | uint64 | 8 |
| 12 | `file_offset` | uint64 | 8 |

Each entry points to an I-frame.

- `frame_number` is the record number of the keyframe.
- `elapsed_ms` is the presentation time of the keyframe relative to the start of the video.
- `file_offset` is the absolute offset of the corresponding `FRM1` frame header.

The index MUST contain an entry for frame zero.

Entries MUST be sorted by increasing frame number and increasing elapsed time. Frame offsets MUST also increase strictly.

The number of entries MUST equal the number of I-frames in the frame sequence.

The index size is:

`index_size = 8 + entry_count × 20`

The index MUST end exactly at end-of-file. Trailing bytes are not permitted in v1.

### 12.1 Seeking

To seek to a target time:

1. Find the index entry with the greatest `elapsed_ms` that is less than or equal to the target time.
2. Seek to its `file_offset`.
3. Decode that I-frame.
4. Decode following frames in order until the target time is reached.
5. Display the frame whose presentation interval contains the target time.

A decoder MUST NOT begin P-frame decoding without a valid preceding I-frame.

---

## 13. Encoder requirements and recommendations

The exact encoder implementation is not part of the binary format. The following describes the recommended CPV v1 encoding pipeline.

### 13.1 Input and scaling

The encoder SHOULD accept video sources supported by FFmpeg.

It SHOULD:

1. Read the first video stream.
2. Ignore audio, subtitle, and data streams.
3. Convert to the requested target frame rate.
4. Scale to the requested output dimensions.
5. Preserve source aspect ratio by default, using letterboxing when required.
6. Convert source pixels to RGB565 before palette mapping.

The output width and height MUST NOT exceed 528 × 320.

The normal target is 528 × 320 at 10 FPS, but smaller dimensions and lower frame rates are valid and often beneficial for the calculator.

### 13.2 Palette analysis

The encoder SHOULD analyse representative frames from the complete video and build one global palette.

It SHOULD avoid generating a palette from only the first frame. A palette that represents the whole video reduces colour error in later scenes.

The encoder MAY use temporal sampling rather than analysing every frame during palette generation.

### 13.3 Palette lookup

The encoder SHOULD build a reusable RGB565-to-palette-index lookup table once per conversion.

A 65,536-byte table is sufficient for all possible RGB565 inputs. Once built, the encoder can map source pixels to palette indices with one lookup each.

The encoder SHOULD NOT perform a fresh search through all palette entries for every pixel.

### 13.4 Delta comparison

The encoder MUST compare each new source frame against the previous **reconstructed** frame.

For each pixel, the encoder determines the value that will actually be written to the output:

- a palette colour, or
- a direct RAW565 colour.

If that reconstructed value equals the previous reconstructed pixel, the pixel is eligible for SKIP.

This rule is important when lossy palette mapping is enabled: comparison against the unquantised source would otherwise produce incorrect deltas.

### 13.5 Packet selection

The encoder SHOULD use the following strategies:

- Use SKIP for consecutive unchanged pixels in P-frames.
- Use INDEX_REPEAT for repeated palette indices.
- Use INDEX_LITERAL for consecutive non-repeating palette indices.
- Use RAW565 for consecutive direct RGB565 values that are not represented by the palette within the selected quality policy.
- Use the largest legal run length for each packet type when doing so does not increase total output size.

The encoder MAY use lookahead or rate-distortion analysis to choose between INDEX_LITERAL, INDEX_REPEAT, and RAW565.

The encoder SHOULD avoid emitting many one-pixel packets when a single literal packet can encode the same region.

### 13.6 Frame type selection

The encoder MUST emit the first frame as an I-frame.

It SHOULD emit I-frames approximately every second and MUST NOT exceed the 10-second maximum interval.

The encoder MAY choose an earlier I-frame when a scene changes or when a P-frame would be larger than an I-frame.

### 13.7 Streaming and memory

The encoder SHOULD process frames as a stream instead of loading the entire video into memory.

It needs only:

- Current source frame.
- Previous reconstructed frame.
- Current reconstructed frame (or an in-place reconstruction strategy).
- Global palette and lookup table.
- Current frame payload buffer.
- Keyframe index entries.

The output file SHOULD be written to a temporary file and atomically renamed after successful completion. A failed conversion SHOULD NOT leave a partially written final output file.

---

## 14. Quality and compression policy

Colour quantisation is an encoder-side operation. The decoder does not know which quality setting was used.

The encoder MAY expose a quality parameter from 0 to 100:

- Lower values prioritise smaller palettes and smaller output files.
- Higher values prioritise visual fidelity.
- RAW565 MAY preserve detail when palette mapping would introduce excessive error.
- Quality 100 SHOULD preserve the source after conversion to RGB565 by using RAW565 whenever palette mapping would change a pixel.
- Quality below 100 MAY apply lossy palette mapping according to the encoder's documented policy.

The precise quantiser, palette-size mapping, colour-distance threshold, and dithering strategy are encoder-defined. Different encoders may produce different valid CPV files from the same source.

Dithering MAY be used. Temporal dithering SHOULD be avoided because it can introduce visible flicker in animation and moving scenes.

### 14.1 Compression trade-offs

The format intentionally has no single compression mode for every pixel:

- Stable regions are cheap because of SKIP.
- Flat regions are cheap because of INDEX_REPEAT.
- Detailed palette-coded regions are compact with INDEX_LITERAL.
- High-detail regions can use RAW565 to avoid excessive colour loss.

The encoder is responsible for choosing a suitable representation. The decoder remains simple regardless of that choice.

---

## 15. Decoder architecture

A conforming decoder requires:

- One 80-byte file header.
- A palette buffer of up to 256 RGB565 values.
- One RGB565 framebuffer of `width × height` pixels.
- A small frame-header structure.
- A packet reader and pixel cursor.
- A keyframe index reader for seeking.

A second framebuffer is optional. The format does not require double buffering.

### 15.1 Sequential decoding procedure

A decoder SHOULD follow this sequence:

1. Read and validate the 80-byte file header.
2. Read the global palette.
3. Seek to `frames_offset`.
4. Read the next 20-byte frame header.
5. Validate the frame header and payload bounds.
6. Read and decode packets in payload order.
7. Verify exact pixel coverage and exact payload consumption.
8. Present the reconstructed framebuffer for the frame duration.
9. Continue with the next frame record.

### 15.2 Packet decoding pseudocode

The following pseudocode describes the packet loop. It is illustrative; implementations may use different internal structures.

```text
pixel_position = 0

while payload_bytes_remaining > 0:
    token = read_u8()

    opcode = token >> 6
    length_code = token & 0x3F

    if length_code == 63:
        run_length = read_u16_le()
        require 64 <= run_length <= 65535
    else:
        run_length = length_code + 1

    require run_length <= pixel_count - pixel_position

    switch opcode:
        INDEX_LITERAL:
            repeat run_length times:
                index = read_u8()
                require index < palette_count
                framebuffer[pixel_position] = palette[index]
                pixel_position += 1

        INDEX_REPEAT:
            index = read_u8()
            require index < palette_count
            repeat run_length times:
                framebuffer[pixel_position] = palette[index]
                pixel_position += 1

        SKIP:
            require frame_type == P_FRAME
            pixel_position += run_length

        RAW565:
            repeat run_length times:
                framebuffer[pixel_position] = read_u16_le()
                pixel_position += 1

require pixel_position == pixel_count
require payload_bytes_remaining == 0
```

For SKIP, advancing the cursor leaves existing framebuffer values untouched.

A production decoder MUST also validate every read against the remaining frame payload before accessing bytes.

### 15.3 Presentation timing

The player SHOULD schedule presentation based on each frame's `duration_ms`.

The target playback rate is 10 FPS. The decoder's performance target is to complete frame decoding within the corresponding playback interval.

If decoding cannot keep up, the player MAY drop presentation of overdue frames while still maintaining a valid reference state by decoding all dependent frames in order. It MUST NOT skip P-frame reconstruction if later frames depend on it.

---

## 16. Validation and error handling

A decoder MUST reject malformed files safely. It MUST NOT read beyond the file, frame payload, palette, or index.

At minimum, the decoder MUST validate:

### File-level validation

- Magic and version.
- Header size and reserved fields.
- Width, height, frame rate, frame count, and palette count.
- Palette and frame offsets.
- Index offset and file bounds.
- Palette length.
- Exact end-of-file after the index.

### Frame-level validation

- Frame magic `FRM1`.
- Frame type is I or P.
- Flags and header size.
- Sequential frame number.
- Positive frame duration.
- Payload size and file bounds.
- First frame is an I-frame.
- A P-frame has a preceding successfully decoded frame.

### Packet-level validation

- Opcode is valid (all four two-bit values are defined in v1).
- Extended lengths are in the defined range.
- Packet data is fully present.
- Palette indices are valid.
- SKIP is not used in an I-frame.
- Pixel coverage equals exactly `width × height`.
- Payload is consumed exactly.

### Index-level validation

- Magic is `IDX1`.
- Entry count is plausible for the file size.
- Every entry references a valid I-frame.
- Entries are strictly ordered.
- The first entry references frame zero and elapsed time zero.
- Every I-frame has exactly one index entry.

On any decoding error, the player SHOULD stop playback and display a useful error. It MUST NOT continue with a partially reconstructed frame as if it were valid.

---

## 17. Performance considerations for the fx-CP400

The decoder is intentionally designed around simple operations:

- Sequential reads.
- Byte and 16-bit little-endian loads.
- Palette lookups.
- 16-bit framebuffer writes.
- Repeated-value writes.
- Cursor increments.
- No recursion.
- No floating-point operations.
- No per-pixel nearest-colour search.
- No motion search.

For the maximum 528 × 320 RGB565 frame, one framebuffer requires:

`528 × 320 × 2 = 337,920 bytes`

This is approximately 330 KiB. A second framebuffer is optional and requires another 337,920 bytes.

The global palette requires at most 512 bytes.

The decoder SHOULD use buffered sequential file reads. Reading every packet or pixel through a separate filesystem call is strongly discouraged.

The desktop encoder and calculator decoder have different goals. The encoder may perform palette analysis, lookahead, and rate-distortion decisions. None of those operations are required on the calculator.

---

## 18. Compatibility and versioning

A file conforming to this document MUST use:

- Major version 1.
- Minor version 0.
- The exact 80-byte v1 header.
- The exact 20-byte v1 frame header.
- The four packet opcodes defined in Section 10.
- The `IDX1` keyframe index.

A v1 decoder MUST reject unknown major versions.

A decoder MAY reject minor versions greater than the highest minor version it supports. A v1.0 decoder MUST reject non-zero reserved fields and flags rather than silently assuming unknown semantics.

Any future change that alters packet meaning, frame reconstruction, or required header layout requires a new major version.

---

## 19. Recommended encoder command-line interface

The command-line interface is not part of the binary format, but the host converter SHOULD provide a stable interface similar to:

```bash
python3 tools/convert.py input.mp4 \
    --output output.cpv \
    --width 528 \
    --height 320 \
    --fps 10 \
    --quality 75
```

Recommended options:

| Option | Meaning |
|---|---|
| `--output` | Output CPV file |
| `--width` | Output width, 1–528 |
| `--height` | Output height, 1–320 |
| `--fps` | Nominal output frame rate, up to 30 |
| `--quality` | Encoder quality policy, 0–100 |
| `--keyframe-interval` | Keyframe target interval; default 1 second |
| `--fit contain` | Preserve aspect ratio with letterboxing |
| `--fit stretch` | Stretch to the exact output dimensions |

The normal calculator profile is 528 × 320 at 10 FPS. Lower resolutions and frame rates SHOULD be encouraged when they provide a useful quality-to-size improvement.

---

## 20. Conformance test vectors and required tests

A CPV v1 implementation SHOULD include tests for all packet types and all structural boundaries.

### 20.1 Packet tests

- INDEX_LITERAL with one, 63, 64, and 65,535 pixels.
- INDEX_REPEAT with one, 63, 64, and 65,535 pixels.
- SKIP with one, 63, 64, and 65,535 pixels in a P-frame.
- RAW565 with one, 63, 64, and 65,535 pixels.
- A run longer than 65,535 pixels split across multiple packets.
- Invalid palette indices.
- Truncated short and extended packets.
- Invalid extended lengths.
- SKIP in an I-frame.
- Packet streams that cover too few or too many pixels.

### 20.2 Frame tests

- A one-pixel I-frame.
- A complete I-frame using only INDEX_LITERAL.
- An I-frame using INDEX_REPEAT and RAW565.
- A P-frame that uses only SKIP.
- A P-frame that changes one pixel.
- A P-frame that changes a long contiguous range.
- A P-frame that mixes SKIP, INDEX_LITERAL, INDEX_REPEAT, and RAW565.
- A scene change followed by a new I-frame.
- Variable frame durations.

### 20.3 File tests

- Minimum valid dimensions: 1 × 1.
- Maximum dimensions: 528 × 320.
- Minimum palette count: 1.
- Maximum palette count: 256.
- Missing index.
- Invalid offsets.
- Invalid frame count.
- Missing first I-frame.
- Missing keyframe index entry.
- Unsorted index entries.
- Extra bytes after the index.
- Incorrect total duration.
- Unsupported version and non-zero reserved fields.

### 20.4 Round-trip tests

For every generated test file:

1. Encode the source frames.
2. Decode the resulting CPV file with the host decoder.
3. Compare the decoded RGB565 frames with the encoder's reconstructed frames.
4. Require exact equality for every decoded pixel.
5. Seek to every keyframe and verify that sequential decoding from that point produces the same frames as decoding from the beginning.

For quality 100, the encoder SHOULD also verify that decoded pixels match the source after RGB565 conversion.

---

## 21. Design rationale

### 21.1 Why one global palette?

A global palette is stored once instead of repeating palette definitions for every tile. It also allows one lookup table to be reused for every frame.

This is particularly useful for a calculator decoder with limited memory and CPU resources.

### 21.2 Why linear packets instead of tiles?

The decoder does not need to manage tile coordinates, split trees, local palette headers, or recursive traversal.

A packet only needs to know its run length and operation. It can update the framebuffer with a simple pointer and counter.

### 21.3 Why INDEX_LITERAL?

A repeat-only RLE stream handles long runs of one colour well, but it is inefficient when many adjacent changed pixels have different colours.

INDEX_LITERAL stores an entire sequence of indices behind one packet header. This avoids the per-pixel packet overhead of a repeat-only representation.

### 21.4 Why RAW565?

A fixed palette cannot represent every RGB565 colour in a complex photographic frame.

RAW565 lets the encoder preserve selected detail without making the complete file uncompressed. The encoder can use it only where the quality benefit justifies the additional bytes.

### 21.5 Why no motion vectors in v1?

Motion estimation and compensation would increase encoder complexity and decoder work. The linear delta stream already captures exact unchanged pixels and repeated values with little decoder overhead.

Motion compensation may be revisited only after the simpler codec has been benchmarked on representative material.

---

## 22. Implementation acceptance criteria

The CPV v1 implementation is considered ready for calculator integration when all of the following are true:

- The host encoder writes only files conforming to this specification.
- The host decoder validates and decodes every defined packet type.
- Encoder and decoder reconstruct identical RGB565 framebuffers.
- Keyframe seeking produces identical output to sequential decoding.
- Files with malformed headers, packets, or indices fail safely.
- The calculator decoder performs no dynamic memory allocation per pixel.
- Sequential file reading is buffered.
- Playback reaches the 10 FPS target at the intended device resolution for the selected test profile.
- Encoder benchmarks report conversion time, output size, dimensions, frame rate, and quality setting.

The final compression and playback targets MUST be established using repeatable test clips. File size alone is not a meaningful comparison unless source material, output resolution, frame rate, duration, and visual quality are held constant.
