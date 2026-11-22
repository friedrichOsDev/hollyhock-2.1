# CPV Video Format Specification

**Document:** CPV v1.3  
**File extension:** `.cpv`  
**Status:** Proposed specification — implementation baseline  
**Target platform:** Casio fx-CP400 running Hollyhock 2.1  
**Primary playback target:** 528 × 320 pixels at 10 frames per second  
**Maximum frame dimensions:** 528 × 320 pixels  
**Maximum nominal frame rate:** 30 FPS  
**Pixel format:** RGB565  
**Byte order:** Little-endian  
**Audio:** Not supported

---

## 1. Purpose

CPV is a compact video container and codec designed for simple, fast software decoding on the Casio fx-CP400. The desktop encoder may perform analysis and optimisation, but the calculator decoder must use only bounded integer operations, sequential file reads, palette lookups, and framebuffer writes.

CPV v1 is based on a proven and deliberately simple architecture:

- One **global colour palette** for the complete video.
- One linear **raster-ordered pixel stream**, not a tile tree.
- **Delta frames** that encode only pixels that differ from the preceding reconstructed frame.
- Run-length packets for repeated colours.
- Literal packets for sequences containing different palette colours.
- A direct RGB565 packet for content where palette quantisation would cause too much visual loss.
- Periodic keyframes and a compact keyframe index for seeking.

The format is intended for mixed video content: animation, live-action footage, gameplay, and screen recordings. It does not assume that all content consists of flat colours or that motion occurs only in small rectangular regions.

### 1.1 Design priorities

In order of priority:

1. Small files at useful visual quality.
2. Fast and predictable decoding on the calculator.
3. Fast desktop conversion.
4. Low calculator memory use.
5. Bounded seeking time.
6. A format that is straightforward to inspect, validate, and implement.

### 1.2 What CPV v1 deliberately avoids

CPV v1 does not define:

- Audio, subtitles, or multiple tracks.
- Per-tile or per-frame palettes.
- Recursive tiles or quadtrees.
- Motion vectors or motion compensation.
- B-frames or bidirectional prediction.
- YUV or chroma-subsampled pixels.
- Huffman, arithmetic, or context-adaptive entropy coding.
- Alpha channels.
- Decoder-side scaling or colour enhancement.

These features are not required to achieve the goals of v1. A future incompatible format version may introduce them if benchmarks demonstrate a real benefit.

### 1.3 Normative language

The words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and **MAY** are normative. Requirements using MUST or MUST NOT are necessary for v1 conformance.

---

## 2. Core model

A CPV v1 file contains:

1. A fixed-size file header.
2. One global palette of RGB565 colours.
3. A sequence of frame records.
4. A keyframe index at the end of the file.

Every stored frame reconstructs one complete displayed image. A frame is one of:

- **I-frame (keyframe):** Reconstructs every pixel without depending on an earlier frame.
- **P-frame (delta frame):** Starts with the preceding reconstructed image and modifies selected pixel ranges. Unchanged ranges are represented by SKIP packets.

Pixels are processed in row-major order:

`pixel_index = y × width + x`

The first pixel is the top-left pixel. The final pixel is the bottom-right pixel.

There is no tile grid. Packet boundaries may occur at any pixel position, including across scanline boundaries. This avoids per-tile metadata, local palettes, recursive traversal, and unnecessary packet boundaries at tile edges.

### 2.1 Reference image

A P-frame MUST be decoded relative to the **previous reconstructed RGB565 image**.

The reference is not the original uncompressed source frame. It is the exact image that the decoder produced after applying the previous CPV frame.

The encoder MUST maintain the same reconstruction state as the decoder. This is essential when palette quantisation is used; otherwise the encoder and decoder could disagree about whether a pixel is unchanged.

### 2.2 Exact delta rule

SKIP is an exact operation in CPV v1:

- If the next output RGB565 pixel equals the previous reconstructed pixel, it MAY be included in a SKIP run.
- If it differs, it MUST be encoded using a data packet.

A v1 encoder MUST NOT use SKIP merely because two pixels are visually similar. Approximate temporal skipping can freeze small movements, accumulate error, and cause visible trails. Lossy colour decisions belong to palette quantisation, not to the definition of SKIP.

### 2.3 Global palette

The file contains exactly one palette for the complete video. Every palette entry is a 16-bit RGB565 value.

Palette indices in INDEX_LITERAL and DELTA_MASK packets are packed using the minimum fixed bit width required by the global palette:

`index_bits = max(1, ceil(log2(palette_count)))`

The bit widths are 1, 2, 3, 4, 5, 6, 7, or 8 bits. Bits are stored least-significant-bit first. Each packet starts at a byte boundary; unused high bits in its final byte MUST be zero. INDEX_REPEAT continues to store its single index as one byte for simpler decoding.

A palette index MUST be in the range `0 .. palette_count - 1`.

RAW565 packets bypass the palette and store direct RGB565 values. They are a quality escape, not the normal compression path.

---

## 3. Limits and supported properties

| Property | CPV v1 |
|---|---|
| Minimum width | 1 pixel |
| Maximum width | 528 pixels |
| Minimum height | 1 pixel |
| Maximum height | 320 pixels |
| Maximum pixels per frame | 168,960 |
| Nominal frame rate | Greater than 0 and no greater than 30 FPS |
| Recommended frame rate | 10 FPS |
| Palette entries | 1–256 |
| INDEX_LITERAL index size | 1–8 bits (minimum for palette) |
| INDEX_REPEAT index size | 1 byte |
| Pixel format | RGB565 |
| Frame types | I-frame and P-frame |
| Audio | Not supported |
| Alpha channel | Not supported |
| Previous-frame references | Immediately preceding reconstructed frame only |
| Recommended keyframe interval | 2 seconds |
| Maximum dependency interval | 10 seconds |

Smaller resolutions and lower frame rates are valid and SHOULD be used when they provide a better quality-to-size ratio.

The normal calculator target is 528 × 320 at 10 FPS. The maximum dimensions describe the encoded frame and do not require the player to scale the video to fill the display.

The nominal frame rate is stored as a rational number. The normal profile uses `fps_num = 10` and `fps_den = 1`.

Individual frame durations are authoritative for presentation timing. They allow coalesced identical frames and variable-duration frames without duplicating image data.

---

## 4. Primitive types and integer limits

All multi-byte file fields are **little-endian**.

| Type | Size | Description |
|---|---:|---|
| `uint8` | 1 byte | Unsigned 8-bit integer |
| `uint16` | 2 bytes | Unsigned 16-bit integer |
| `uint32` | 4 bytes | Unsigned 32-bit integer |
| `char[n]` | n bytes | Fixed byte sequence |

CPV v1 deliberately uses 32-bit file offsets and millisecond timestamps instead of 64-bit fields. This keeps file parsing and offset arithmetic inexpensive on the calculator's 32-bit software environment.

Consequences:

- Maximum representable file size: less than 4 GiB.
- Maximum total duration: `0xFFFFFFFF` milliseconds (approximately 49.7 days).
- Maximum single frame payload: `0xFFFFFFFF` bytes.
- Maximum frame count: `0xFFFFFFFF`.

These limits are sufficient for the intended calculator use. A file or duration that cannot be represented MUST be rejected by the encoder.

Implementations MUST read and write file fields explicitly in little-endian order. They MUST NOT rely on native structure alignment or host byte order.

All file offsets are absolute byte offsets from the beginning of the file.

Reserved fields MUST be written as zero. A strict v1 decoder MUST reject non-zero reserved fields and flag bits.

---

## 5. RGB565 pixel format

RGB565 stores one pixel in 16 bits:

- Red: 5 bits.
- Green: 6 bits.
- Blue: 5 bits.

```text
15             11 10               5 4              0
+----------------+------------------+----------------+
|    Red (5)     |    Green (6)      |    Blue (5)    |
+----------------+------------------+----------------+
```

The packed value is:

`rgb565 = (red5 << 11) | (green6 << 5) | blue5`

A conventional conversion from 8-bit RGB components is:

```text
red5   = (red8   × 31 + 127) / 255
green6 = (green8 × 63 + 127) / 255
blue5  = (blue8  × 31 + 127) / 255
```

The 16-bit value is stored little-endian.

Palette entries and RAW565 pixels use exactly the same RGB565 representation.

---

## 6. File layout

A CPV v1 file has the following layout:

```text
+------------------------------+
| File Header (64 bytes)       |
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

The palette immediately follows the 64-byte file header. Frame records immediately follow the palette. The index immediately follows the last frame record.

No padding or alignment bytes are permitted between structures.

The original CPV format used the two-byte magic `CP` and a different frame representation. CPV v1 uses `CPV\0` and is **not backward-compatible** with legacy CP files. A legacy player MUST NOT be expected to read a CPV v1 file.

---

## 7. File header

The file header is exactly **64 bytes**.

| Offset | Field | Type | Size | Required value / meaning |
|---:|---|---|---:|---|
| 0 | `magic` | char[4] | 4 | ASCII `CPV\0` |
| 4 | `version_major` | uint8 | 1 | 1 |
| 5 | `version_minor` | uint8 | 1 | 3 for CPV v1.3; v1.0/v1.1/v1.2 readers MAY also be supported |
| 6 | `header_size` | uint16 | 2 | 64 |
| 8 | `flags` | uint32 | 4 | 0 in v1 |
| 12 | `width` | uint16 | 2 | 1–528 |
| 14 | `height` | uint16 | 2 | 1–320 |
| 16 | `fps_num` | uint16 | 2 | Positive numerator |
| 18 | `fps_den` | uint16 | 2 | Positive denominator |
| 20 | `frame_count` | uint32 | 4 | Number of stored frame records |
| 24 | `palette_count` | uint16 | 2 | 1–256 |
| 26 | `reserved0` | uint16 | 2 | 0 |
| 28 | `palette_offset` | uint32 | 4 | MUST equal 64 |
| 32 | `frames_offset` | uint32 | 4 | First frame record |
| 36 | `index_offset` | uint32 | 4 | Keyframe index |
| 40 | `duration_ms` | uint32 | 4 | Sum of all stored frame durations |
| 44 | `reserved1` | uint8[20] | 20 | All zero |
| | **Total** | | **64** | |

### 7.1 Header validation

Before playback, a decoder MUST verify:

- Magic is exactly `CPV\0`.
- Major version is 1 and minor version is 0, 1, 2, or 3. Version 1.0 uses byte-sized INDEX_LITERAL values and has no frame compression or DELTA_MASK packets; version 1.1 enables bit-packing, DELTA_MASK, and LZ4; version 1.2 adds DEFLATE frame payloads; version 1.3 adds optional global-motion prediction.
- Header size is exactly 64.
- Flags and all reserved fields are zero.
- Width and height are within the specified limits.
- FPS numerator and denominator are non-zero.
- The rational FPS does not exceed 30.
- Frame count is non-zero.
- Palette count is in the range 1–256.
- `palette_offset` equals 64.
- `frames_offset = palette_offset + palette_count × 2`.
- `index_offset` is greater than `frames_offset` and inside the file.
- Exactly `frame_count` frame records can be parsed between `frames_offset` and `index_offset`.
- The end offset of the final frame record equals `index_offset` exactly; gaps and trailing bytes before the index are invalid.
- The index fits inside the actual file and ends exactly at end-of-file.
- The sum of frame durations equals `duration_ms`.

All offset and size calculations MUST be checked for overflow before use.

---

## 8. Global palette block

The palette starts at `palette_offset` and contains exactly `palette_count` entries.

Each entry is a little-endian RGB565 value:

| Field | Type | Size |
|---|---|---:|
| `colour` | uint16 | 2 bytes |

The palette block size is:

`palette_size = palette_count × 2`

The first frame starts at:

`frames_offset = palette_offset + palette_size`

Palette entries MUST be unique in v1. If the source video needs fewer distinct palette colours, the encoder MUST store only the required number of entries rather than padding the palette with duplicates.

A decoder SHOULD keep the palette in a fixed array of 256 RGB565 values. The maximum palette memory is 512 bytes.

### 8.1 Palette generation

Palette generation is performed by the desktop encoder. It is not part of decoding.

A conforming encoder:

- MUST create one palette for the complete video.
- MUST use between 1 and 256 unique entries.
- MUST ensure every palette-coded pixel references a valid entry.
- SHOULD analyse representative frames from the complete video, not just the first frame.
- SHOULD account for scene changes and visually important frames.
- MAY use FFmpeg palette-generation filters or a custom quantiser.
- MAY denoise source frames before palette generation and encoding.
- MAY use spatial dithering.

A global palette avoids storing local palette headers repeatedly and allows one reusable colour lookup table to be used for the whole conversion.

### 8.2 Fast palette mapping

The encoder SHOULD avoid searching all palette entries for every source pixel.

A recommended approach is:

1. Convert an input pixel to RGB565.
2. Build a 65,536-byte lookup table once per conversion.
3. Store in each table position the nearest palette index for that RGB565 value.
4. Map each source pixel using one lookup.

The table covers every possible 16-bit RGB565 value. It requires 64 KiB on the desktop and is not needed by the calculator.

A recommended integer colour-distance metric is:

`D = 3 × ΔR² + 4 × ΔG² + 2 × ΔB²`

where the component differences use their native 5-, 6-, and 5-bit ranges. The exact quantiser and tie-breaking policy are encoder-defined.

---

## 9. Frame record

Every stored frame is represented by a frame record. Its fixed header is exactly **20 bytes**.

| Offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0 | `magic` | char[4] | 4 | ASCII `FRM1` |
| 4 | `frame_type` | uint8 | 1 | 0 = I-frame, 1 = P-frame |
| 5 | `flags` | uint8 | 1 | Bit 0: independent LZ4 block; bit 1: independent DEFLATE stream; bit 2: global-motion P-frame prediction; bits 3–7 zero. LZ4 and DEFLATE are mutually exclusive; either may combine with global motion |
| 6 | `header_size` | uint16 | 2 | 20 |
| 8 | `frame_number` | uint32 | 4 | Sequential stored-record number, starting at 0 |
| 12 | `duration_ms` | uint32 | 4 | Display duration |
| 16 | `payload_size` | uint32 | 4 | Number of payload bytes |
| | **Total** | | **20** | |

The payload begins immediately after the frame header and contains exactly `payload_size` stored bytes.

### 9.3 Optional independent LZ4 frame payload

In CPV v1.1–v1.3, frame flag bit 0 (FRAME_FLAG_LZ4 = 0x01) indicates that the stored frame payload is a raw LZ4 block. CPV v1.2 and newer additionally support flag bit 1 (FRAME_FLAG_DEFLATE = 0x02), indicating a zlib-wrapped DEFLATE stream (which uses Huffman coding). LZ4 and DEFLATE are mutually exclusive. Either compression flag may be combined with the global-motion flag defined in Section 9.5. Each frame is compressed independently; there is no cross-frame dictionary.

For a compressed frame, the stored payload is:

| Field | Type | Size |
|---|---|---:|
| `uncompressed_size` | uint32 little-endian | 4 bytes |
| `lz4_block` | raw LZ4 block | remaining payload bytes |

The four-byte size is the exact byte count of the uncompressed frame stream (including the optional four-byte global-motion header). The LZ4 block MUST decompress to exactly this size. The decoder MUST validate the size before allocating or decompressing; it MUST NOT allow the decompressed frame stream to exceed `3 × width × height + 4` bytes. Without global motion, the normal bound is `3 × width × height` bytes.

If frame flag bit 0 is clear, the stored payload is the ordinary packet stream. A v1.0 frame MUST have flags equal to zero. A v1.1–v1.3 encoder SHOULD use LZ4 only when the four-byte size prefix plus compressed block is smaller than the original frame stream by a worthwhile margin. An uncompressed frame does not need an LZ4 dependency.

LZ4 is used for its bounded, fast block decoder and is an optional second compression layer. It does not change packet semantics, delta reference rules, or seeking: a decoder decompresses only the selected frame before parsing its packet stream.

The next frame begins at:

`next_frame_offset = current_frame_offset + 20 + payload_size`

### 9.1 Frame numbering

The first stored record MUST have frame number zero and MUST be an I-frame.

Every subsequent stored record increments the frame number by exactly one.

`frame_count` in the file header counts stored frame records, not necessarily the number of source frames before identical frames were coalesced.

### 9.2 Frame duration and coalescing

`duration_ms` is the amount of time for which the reconstructed image is displayed.

A frame duration MUST be greater than zero.

The encoder SHOULD coalesce consecutive frames when their reconstructed RGB565 images are identical:

- Do not write a new frame record.
- Extend the duration of the previous stored frame.
- Preserve the same reconstructed reference image.

This is exact frame deduplication, not approximate motion suppression. It is especially useful for static scenes, pause screens, slides, and source videos whose frame rate is higher than their actual image-change rate.

When coalescing, the encoder MUST add durations without exceeding the uint32 limit. If the combined duration would overflow, it MUST start a new valid I-frame record.

The total video duration is the sum of stored frame durations and MUST equal the file header's `duration_ms`.

---

### 9.4 Optional independent DEFLATE frame payload (CPV v1.2)

When frame flag bit 1 is set, the stored payload consists of a four-byte little-endian uncompressed frame-stream size followed by one zlib-wrapped DEFLATE stream. The stream MUST decompress to exactly the declared size. The upper bound is 3 × width × height + 4 bytes when global motion is also enabled, otherwise 3 × width × height bytes. A decoder MUST reject invalid, truncated, trailing, or oversized DEFLATE data.

DEFLATE is a lossless compressor using LZ77 and Huffman coding. It may be smaller than LZ4 for some packet streams but can require more decoder CPU and memory. An encoder MAY compare DEFLATE level 9 and LZ4 high-compression and select whichever complete representation (including the four-byte size prefix) is smaller, provided it is worthwhile. CPV v1.1 readers do not support this flag and MUST reject it.

### 9.5 Optional global-motion prediction (CPV v1.3)

Global motion is an optional prediction applied to P-frames before ordinary packet decoding. It represents a small translation of the complete previous reconstructed RGB565 image. The decoder performs the translation, fills newly exposed pixels with one RGB565 colour, and then applies the normal P-frame packet stream. SKIP and DELTA_MASK semantics remain exact comparisons against this translated prediction.

Frame flag bit 2 (FRAME_FLAG_GLOBAL_MOTION = 0x04) indicates that the uncompressed frame stream begins with this four-byte header:

| Field | Type | Size | Meaning |
|---|---|---:|---|
| dx | int8 | 1 | Horizontal translation in pixels; positive moves right |
| dy | int8 | 1 | Vertical translation in pixels; positive moves down |
| fill_colour | uint16 little-endian | 2 | RGB565 value used for pixels exposed by the translation |

The vector MUST be non-zero and MUST satisfy -8 <= dx, dy <= 8, abs(dx) < width, and abs(dy) < height. The flag is valid only for P-frames and only in CPV v1.3. It MAY be combined with exactly one frame-compression flag. The global-motion header is part of the uncompressed frame stream; therefore it follows the four-byte uncompressed-size prefix after decompression when LZ4 or DEFLATE is used.

The translated prediction is defined by:

predicted[x + dx, y + dy] = previous[x, y]

for every source coordinate whose destination remains inside the frame. Every destination pixel not reached by this mapping is set to fill_colour. The decoder MUST apply this translation before interpreting any packet in the P-frame. The subsequent packet stream still covers exactly width × height pixels in raster order.

The encoder SHOULD estimate motion from the quantised reconstructed RGB565 frames, build a normal P-frame and a motion-predicted P-frame, compress both complete representations, and select global motion only when the stored motion representation is strictly smaller. A decoder does not estimate motion; it only applies the stored vector.

A v1.0, v1.1, or v1.2 decoder MUST reject this flag. A v1.3 decoder MUST reject the flag on I-frames, a zero vector, vectors outside the permitted range, and a truncated motion header.

## 10. Packet stream

The frame payload is a sequence of packets. Each packet describes a run of consecutive pixels in raster order.

There are exactly four packet types:

| Opcode | Name | Purpose |
|---:|---|---|
| `00` | INDEX_LITERAL | A sequence of palette indices |
| `01` | INDEX_REPEAT | Repeat one palette index |
| `10` | SKIP | Preserve pixels from the previous frame |
| `11` | RAW565 | A sequence of direct RGB565 pixels |

The two most significant bits of the first packet byte contain the primary opcode. The remaining bits encode the run length. DELTA_MASK is an extended SKIP subtype and does not consume a new primary opcode.

### 10.1 Compact packet header

CPV v1 uses a compact one-byte or two-byte packet header. This keeps the common case small while allowing long unchanged or repeated runs.

The first byte is:

```text
  7 6 5 4 3 2 1 0
+-----+-+---------+
| OP  |L| LENGTH  |
+-----+-+---------+
  2b  1b   5b
```

- Bits 7–6: packet opcode.
- Bit 5: length form.
- Bits 4–0: short length or high bits of a long length.

**Short form — bit 5 is zero**

`run_length = (first_byte & 0x1F) + 1`

Short packets encode run lengths from 1 to 32 pixels and use a one-byte header.

**Long form — bit 5 is one**

The next byte is read. The run length is:

`run_length = ((first_byte & 0x1F) << 8) | second_byte`

Long packets encode run lengths from 33 to 8,191 pixels and use a two-byte header.

A long-form run length below 33 is invalid. Runs longer than 8,191 pixels MUST be split into multiple packets.

The packet header is a bit-field byte sequence. The general little-endian rule for file fields does not change this explicit packet encoding.

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

The packet contains exactly `run_length` index bytes after the packet header.

Indices are packed with `index_bits = max(1, (palette_count - 1).bit_length())` bits per index, least-significant-bit first. The packed byte count is `ceil(run_length × index_bits / 8)`. Every unpacked index MUST be smaller than `palette_count`. Unused high bits in the final byte MUST be zero. The decoder writes the corresponding palette RGB565 value for each index.

For example, a 64-colour palette uses six bits per index, reducing a long literal stream from one byte per pixel to 0.75 bytes per pixel. A 256-colour palette still uses eight bits.

INDEX_LITERAL is intended for changed areas containing several different palette colours. It avoids the overhead of emitting a separate repeat packet for every colour transition.

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

The decoder reads one index byte and writes its palette colour `run_length` times.

The index MUST be smaller than `palette_count`.

INDEX_REPEAT is efficient for flat regions, backgrounds, UI elements, and repeated colours.

### 10.4 SKIP — opcode 10

SKIP advances the output position without writing any pixel values.

Packet layout:

```text
+---------------------------+
| Packet header             |
+---------------------------+
```

SKIP is valid only in P-frames.

For each pixel covered by SKIP, the decoder retains the RGB565 value already stored at the same position in the previous reconstructed frame.

A decoder MUST reject SKIP in an I-frame.

SKIP is intended for unchanged pixels and is particularly effective for animation, screen recordings, stationary backgrounds, and low-motion footage.

#### DELTA_MASK — extended SKIP packet (CPV v1.1)

A DELTA_MASK packet represents a mixed span containing both unchanged and changed pixels. It uses the SKIP opcode with a reserved long-length sentinel:

| Bytes | Meaning |
|---|---|
| `0xA0 0x00` | SKIP opcode, long form, reserved length value zero |
| `span_length` | uint16 little-endian, 1–65,535 pixels |
| `mask` | `ceil(span_length / 8)` bytes, one bit per pixel |
| `changed_indices` | bit-packed palette indices for mask bits set to 1, in raster order |

Mask bits are least-significant-bit first. A set bit means that pixel is changed and the decoder reads one palette index for it. A clear bit means the pixel is unchanged and the decoder leaves the reference pixel untouched. The number of following indices is the population count of the first `span_length` mask bits. Unused high mask bits and unused high bits in the final packed-index byte MUST be zero.

DELTA_MASK MUST contain at least one set bit and at least one clear bit; all-clear and all-set spans MUST use ordinary SKIP or data packets. DELTA_MASK is valid only in P-frames. It cannot contain RAW565 pixels; the encoder MUST split out direct-colour pixels into ordinary RAW565 packets. A decoder MUST reject a zero span, a span that exceeds the remaining frame pixels, truncated mask or index data, invalid palette indices, non-zero padding bits, and any DELTA_MASK in an I-frame.

The encoder SHOULD compare the complete encoded size of the mask form against the ordinary sequence of SKIP and data packets, including all headers. It MUST emit DELTA_MASK only when the mask form is strictly smaller. This avoids adding overhead to simple motion or contiguous changed regions.

The sentinel is invalid as an ordinary long SKIP length (ordinary long packet lengths are 33–8,191). It is therefore unambiguous for a v1.1 decoder. A v1.0 decoder rejects it as an invalid packet length.

### 10.5 RAW565 — opcode 11

RAW565 stores direct RGB565 pixel values without palette mapping.

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

Every pixel value is a little-endian uint16. The packet contains exactly `run_length × 2` bytes after the header.

The decoder writes each RGB565 value directly.

RAW565 is a quality escape for detail that cannot be represented acceptably by the global palette. It is not the normal compression mode: a literal palette index costs one byte per pixel, whereas RAW565 costs two.

The encoder SHOULD select RAW565 only when the quality benefit is worth the additional data. For lossy quality profiles, palette coding SHOULD remain the default.

### 10.6 Packet coverage

Packets are applied in sequence. The first packet begins at pixel zero. Every packet advances the pixel position by its run length.

The total run length of all packets in a frame MUST equal:

`pixel_count = width × height`

Packets may cross scanline boundaries. A decoder MUST NOT assume packet boundaries align to rows.

A decoder MUST reject a frame if:

- A packet would advance beyond the frame's pixel count.
- A packet payload is truncated.
- An index is outside the palette.
- SKIP occurs in an I-frame.
- The payload ends before exactly `pixel_count` pixels have been covered.
- Unconsumed bytes remain at the end of the declared payload.

---

## 11. Frame reconstruction

### 11.1 I-frame

An I-frame MUST define every pixel and MUST NOT contain SKIP packets.

The decoder begins at pixel position zero and applies packets until exactly `width × height` pixels have been written.

A successfully decoded I-frame establishes the reference image for following P-frames.

### 11.2 P-frame

A P-frame is decoded against the immediately preceding reconstructed frame.

The decoder begins at pixel position zero:

- INDEX_LITERAL writes the specified palette colours.
- INDEX_REPEAT writes one palette colour repeatedly.
- SKIP keeps existing pixels.
- DELTA_MASK selectively writes changed palette pixels and keeps clear-mask pixels.
- RAW565 writes direct RGB565 values.

After the payload has been decoded, the position MUST equal `width × height`.

A P-frame MUST NOT refer to any frame other than the immediately preceding reconstructed image.

### 11.3 Keyframe interval

The first stored frame MUST be an I-frame.

Encoders SHOULD create an I-frame approximately every 2 seconds. The time span over which a P-frame depends on a preceding I-frame MUST NOT exceed 10 seconds.

If an unchanged image is coalesced for longer than 10 seconds, no new record is needed during that unchanged interval. The next changed frame MUST be an I-frame if the previous keyframe is now more than 10 seconds old.

An encoder MAY create an I-frame earlier:

- At a scene change.
- When a P-frame would be larger than a new I-frame.
- When seeking performance benefits from a shorter interval.

Every I-frame MUST have one index entry.

### 11.4 Scene changes

Scene-change detection is encoder-defined. It MAY be used to create an I-frame at the beginning of a new scene.

---

## 12. Keyframe index

The index is located at the absolute file offset stored in `index_offset`.

The index begins with an 8-byte header:

| Offset | Field | Type | Size |
|---:|---|---|---:|
| 0 | `magic` | char[4] | 4 |
| 4 | `entry_count` | uint32 | 4 |

The magic is ASCII `IDX1`.

The header is followed by `entry_count` entries. Each entry is exactly **12 bytes**:

| Offset | Field | Type | Size |
|---:|---|---|---:|
| 0 | `frame_number` | uint32 | 4 |
| 4 | `elapsed_ms` | uint32 | 4 |
| 8 | `file_offset` | uint32 | 4 |

Each entry references an I-frame:

- `frame_number`: stored frame-record number.
- `elapsed_ms`: presentation start time from the beginning of the video.
- `file_offset`: absolute offset of the frame's `FRM1` header.

Entries MUST be ordered by increasing frame number, elapsed time, and file offset.

The index MUST contain an entry for frame zero at elapsed time zero.

For every entry, `frame_number` and `file_offset` MUST identify the same parsed I-frame record. `elapsed_ms` MUST equal the sum of the durations of all preceding stored frame records.

The number of entries MUST equal the number of I-frames.

The index size is:

`index_size = 8 + entry_count × 12`

The index MUST end exactly at end-of-file. Trailing bytes are invalid in v1.

### 12.1 Seeking

To seek to a target time:

1. Find the entry with the greatest `elapsed_ms` that is less than or equal to the target time.
2. Seek to its `file_offset`.
3. Decode that I-frame.
4. Decode subsequent frames in order until the target presentation interval is reached.
5. Display the frame whose interval contains the target time.

A decoder MUST NOT start P-frame decoding without a valid preceding I-frame.

A target time inside a coalesced frame's duration refers to that same stored image.

---

## 13. Encoder design

The exact implementation language and palette algorithm are not part of the binary format. This section defines the recommended conversion strategy.

### 13.1 Input pipeline

The encoder SHOULD accept input formats supported by FFmpeg.

It SHOULD:

1. Read the first video stream.
2. Ignore audio, subtitles, and data streams.
3. Resample to the selected output frame rate.
4. Scale to the selected output dimensions.
5. Preserve aspect ratio by default and letterbox when required.
6. Convert each output pixel to RGB565.
7. Map the RGB565 pixel through the global palette lookup table.

The output dimensions MUST NOT exceed 528 × 320. The normal target is 528 × 320 at 10 FPS.

### 13.2 Palette analysis

The encoder SHOULD build one palette for the complete video.

To avoid spending unnecessary time analysing every frame at full resolution, the encoder MAY sample representative frames. A useful strategy is:

- Sample at a lower temporal rate than the output video.
- Include frames around detected scene changes.
- Include representative high-motion and high-detail scenes.
- Avoid building the palette from only the opening seconds.

A global palette SHOULD contain up to 256 colours when quality is the priority. Smaller palettes MAY be selected for stronger compression.

A palette based on the complete video is usually more reliable than a palette based only on the first frame.

### 13.3 Fast colour lookup

The encoder SHOULD create the RGB565-to-palette lookup table once. It MUST NOT perform a 256-colour nearest-neighbour search for every pixel in the normal encoding loop.

Because the table contains only 65,536 entries, its construction is a one-time operation. Implementations SHOULD build it using vectorised operations or compiled native code. The main frame loop SHOULD also use vectorised operations or a compiled fast path for RGB565 conversion, palette mapping, and pixel comparison. A pure Python loop that performs several Python-level operations for every pixel is discouraged for full-resolution 10 FPS conversion.

The calculator decoder does not need this lookup table. It only needs the palette stored in the file.

### 13.4 Frame encoding algorithm

The encoder SHOULD use the following high-level process:

1. Convert the next source frame to RGB565.
2. Map pixels to the global palette, except pixels intentionally selected for RAW565.
3. Determine the exact RGB565 value that the decoder will reconstruct at each pixel.
4. Compare that reconstructed value with the previous reconstructed frame.
5. In P-frames, gather consecutive unchanged pixels into SKIP runs.
6. Gather consecutive equal palette indices into INDEX_REPEAT runs.
7. Gather mixed palette-index sequences into INDEX_LITERAL runs.
8. Gather selected direct-colour pixels into RAW565 runs.
9. Split runs when they exceed 8,191 pixels.
10. Write the frame record and update the encoder's reconstruction state.

The encoder MAY use lookahead to select the smallest representation. It SHOULD compare actual encoded byte counts, including packet headers.

### 13.5 Packet selection

For a changed palette-coded sequence:

- INDEX_REPEAT is preferred for a run of the same index.
- INDEX_LITERAL is preferred for a mixed sequence of indices.
- A short one-pixel sequence MAY use either representation; the smaller or simpler choice SHOULD be used.

RAW565 is a quality decision. It MUST NOT be selected merely because it is a valid representation. Since it uses twice as many pixel bytes as INDEX_LITERAL, its use SHOULD be justified by a defined visual-quality threshold.

The encoder SHOULD avoid producing a large number of one-pixel packets when one literal packet can encode a mixed sequence.

### 13.6 Frame deduplication

The encoder SHOULD compare the newly reconstructed frame with the preceding reconstructed frame.

When they are identical, it SHOULD extend the previous stored frame's duration instead of writing a new all-SKIP P-frame.

This decision MUST be based on exact reconstructed RGB565 pixels, not a perceptual threshold.

### 13.7 Keyframes

The encoder MUST start with an I-frame.

The recommended keyframe interval is 2 seconds. The maximum P-frame dependency interval is 10 seconds, subject to the static-frame coalescing rule in Section 11.3.

The encoder MAY insert additional I-frames after scene changes or when a P-frame would not provide a size advantage.

### 13.8 Streaming output

The encoder SHOULD process frames sequentially and stream the encoded output to disk.

It SHOULD NOT keep the complete compressed video in RAM or repeatedly reallocate one buffer to append each frame.

Recommended memory use:

- Current decoded source frame.
- Previous reconstructed frame.
- Current reconstructed frame or an in-place reconstruction buffer.
- Global palette and lookup table.
- Current frame packet buffer.
- Small index-entry array.

The encoder SHOULD write to a temporary file and atomically rename it after successful completion. A failed conversion SHOULD NOT leave a partially written final output file.

---

## 14. Quality and compression policy

Lossy colour quantisation is an intended feature of CPV.

The encoder MAY expose a quality parameter from 0 to 100. The parameter is an encoder policy and is not stored as a decoder requirement in the file.

Recommended behaviour:

- Lower quality values SHOULD use fewer global palette colours and favour compact palette-coded data.
- Higher quality values SHOULD use more palette colours and reduce colour error.
- RAW565 MAY preserve important details when palette quantisation would be visibly unacceptable.
- Quality 100 SHOULD preserve the source after conversion to RGB565, using RAW565 where the global palette cannot represent a source pixel exactly.
- Quality values below 100 MAY quantise pixels to the global palette.

The exact palette-size mapping, colour-distance thresholds, sampling policy, and dithering strategy are encoder-defined. An encoder MUST document its quality mapping so that results are reproducible.

The normal calculator profile SHOULD prioritise palette-coded data and exact SKIP detection. RAW565 SHOULD be used selectively.

### 14.1 Legacy 36-colour seed palette and adaptive colours

The host encoder MUST reproduce the original CPV seed palette as the first 36 RGB565 entries. It is generated from these twelve RGB888 base hues, in this order:

| Hue | RGB888 |
|---|---|
| Red | (255, 0, 0) |
| Green | (0, 255, 0) |
| Blue | (0, 0, 255) |
| Yellow | (255, 255, 0) |
| Cyan | (0, 255, 255) |
| Magenta | (255, 0, 255) |
| Orange | (255, 128, 0) |
| Pink | (255, 0, 128) |
| Spring green / turquoise | (0, 255, 128) |
| Violet | (128, 0, 255) |
| Brown | (139, 69, 19) |
| Grey | (128, 128, 128) |

For each hue, the encoder appends three variants before moving to the next hue: dark (r//2, g//2, b//2), normal (r, g, b), and light (r + (255-r)//2, g + (255-g)//2, b + (255-b)//2). Each variant is converted to RGB565 using the legacy truncation (r >> 3, g >> 2, b >> 3). These 36 entries MUST occupy palette indices 0–35 in exactly this order.

To reproduce the legacy image-seeded palette generation, the encoder SHOULD supply these 36 RGB888 colours as one raw 6×6 frame to FFmpeg, scale that seed image to the output width using nearest-neighbour sampling, stack it with the FPS-reduced and scaled source video before hqdn3d, and run global palettegen over the combined stream. The final palette starts with the exact 36 RGB565 seed entries; remaining unique entries are taken from FFmpeg's seed-weighted adaptive palette until the configured budget is reached. Transparent palette padding MUST be ignored.

The current host quality budget is 36 colours for quality 0–39, 64 for 40–59, 128 for 60–79, and 256 for 80–100. Quality 100 additionally uses RAW565 for pixels not represented exactly. At least 36 palette entries are therefore required for files generated by this encoder, although a decoder MUST continue accepting older files with smaller valid palettes.


### 14.2 Denoising

Optional spatial or temporal denoising MAY be applied before palette mapping.

Denoising can substantially reduce the number of distinct colours and small frame-to-frame changes in noisy live-action footage. Excessive denoising can remove detail and SHOULD NOT be enabled without a quality trade-off.

Temporal denoising MUST NOT alter the semantics of SKIP. SKIP remains an exact comparison of reconstructed pixel values.

### 14.3 Dithering

Spatial dithering MAY be applied during palette mapping.

Temporal dithering SHOULD be avoided because changing dither patterns can create flicker and prevent otherwise static pixels from being skipped.

---

## 15. Decoder architecture

A conforming decoder needs:

- The 64-byte file header.
- A palette of up to 256 RGB565 values (512 bytes).
- A persistent reconstructed image surface.
- A 20-byte frame-header structure.
- A small packet reader.
- A keyframe index reader when seeking is required.

A second framebuffer is optional. A complete compressed frame buffer is not required.

### 15.1 Sequential decoding

A decoder SHOULD:

1. Read and validate the file header.
2. Read the global palette.
3. Seek to `frames_offset`.
4. Read the next 20-byte frame header.
5. Validate frame type, number, duration, and payload bounds.
6. Decode packet data in order.
7. Validate exact pixel coverage and payload consumption.
8. Present the reconstructed image for the specified duration.
9. Continue with the following record.

### 15.2 Packet decoder pseudocode

First, resolve the frame payload. If an LZ4 or DEFLATE flag is set, read and validate the four-byte uncompressed-size prefix and decompress exactly one independent block or stream. The uncompressed frame stream may begin with the four-byte global-motion header. Apply that prediction to the reference surface before parsing the remaining ordinary packet stream.

```text
compression_flags = frame_flags & (FRAME_FLAG_LZ4 | FRAME_FLAG_DEFLATE)
if compression_flags == FRAME_FLAG_LZ4:
    require minor_version >= 1
    uncompressed_size = read_u32_le()
    require 1 <= uncompressed_size <= 3 * width * height + 4
    frame_stream = lz4_decompress_exactly(remaining_payload, uncompressed_size)
elif compression_flags == FRAME_FLAG_DEFLATE:
    require minor_version >= 2
    uncompressed_size = read_u32_le()
    require 1 <= uncompressed_size <= 3 * width * height + 4
    frame_stream = deflate_decompress_exactly(remaining_payload, uncompressed_size)
else:
    frame_stream = stored_payload

if frame_flags & FRAME_FLAG_GLOBAL_MOTION:
    require minor_version >= 3
    require frame_type == P_FRAME
    dx = read_i8(frame_stream)
    dy = read_i8(frame_stream)
    fill_colour = read_u16_le(frame_stream)
    require -8 <= dx <= 8 and -8 <= dy <= 8
    require dx != 0 or dy != 0
    shift_reference_surface(dx, dy, fill_colour)
    packet_stream = remaining_frame_stream
else:
    packet_stream = frame_stream

pixel_position = 0

while packet_stream_bytes_remaining > 0:
    first = read_u8()
    opcode = first >> 6

    if opcode == SKIP and first == 0xA0 and peek_u8() == 0x00:
        require minor_version >= 1
        consume_u8()  # reserved long-length sentinel
        span_length = read_u16_le()
        require 1 <= span_length <= 65535
        require frame_type == P_FRAME
        mask = read_bytes(ceil(span_length / 8))
        changed_count = popcount(mask[0:span_length bits])
        indices = read_bitpacked_indices(changed_count, index_bits)
        for each bit in mask:
            if bit is set:
                require next index < palette_count
                surface[pixel_position] = palette[next index]
            pixel_position += 1
        require unused mask and index padding bits are zero
        continue

    if (first & 0x20) == 0:
        run_length = (first & 0x1F) + 1
    else:
        second = read_u8()
        run_length = ((first & 0x1F) << 8) | second
        require 33 <= run_length <= 8191

    require run_length <= pixel_count - pixel_position

    switch opcode:
        INDEX_LITERAL:
            indices = read_bitpacked_indices(run_length, index_bits)
            for index in indices:
                require index < palette_count
                surface[pixel_position] = palette[index]
                pixel_position += 1

        INDEX_REPEAT:
            index = read_u8()
            require index < palette_count
            repeat run_length times:
                surface[pixel_position] = palette[index]
                pixel_position += 1

        SKIP:
            require frame_type == P_FRAME
            pixel_position += run_length

        RAW565:
            repeat run_length times:
                surface[pixel_position] = read_u16_le()
                pixel_position += 1

require pixel_position == width * height
require packet_stream_bytes_remaining == 0
```

A production decoder MUST check the remaining frame payload before every read. The pseudocode omits those repeated bounds checks for readability.

### 15.3 Streaming packet reads

The calculator decoder SHOULD read packet data from a buffered file stream. It SHOULD NOT allocate a buffer as large as the entire compressed frame unless the platform's file API makes streaming impractical.

INDEX_LITERAL and DELTA_MASK indices are unpacked using the palette-dependent bit width. INDEX_REPEAT reads one byte and writes one colour repeatedly. SKIP advances the pixel cursor without writing. DELTA_MASK reads its bitmap and applies only the changed indices. RAW565 reads and copies little-endian RGB565 values sequentially.

### 15.4 Using LCD memory as the reference surface

The logical reference image must persist between frames. The specification does not require that it reside in a separate RAM allocation.

When the encoded image is displayed 1:1 and the LCD VRAM provides a stable RGB565 surface, a calculator implementation MAY use that VRAM region directly as the reconstructed image. In this configuration:

- P-frame SKIP simply advances the pixel cursor.
- Changed pixels are written directly to the corresponding VRAM positions.
- No second full-size RAM framebuffer is required.
- The LCD SHOULD be refreshed after the complete frame has been decoded.

If the player applies scaling, rotation, cropping, or a display mapping that cannot act as a stable reference surface, it MUST use another persistent image buffer or an equivalent reconstruction surface.

Any pause icon, text, cursor, or other overlay drawn into a VRAM reference surface MUST be removed or restored before decoding continues. Otherwise, a later SKIP packet would preserve the overlay as if it were video content. A player SHOULD draw overlays in a separate layer or save and restore the affected pixels.

### 15.5 Display timing

The player SHOULD schedule frame presentation using `duration_ms`.

The target playback rate is 10 FPS. The decoder should complete each frame within its presentation interval.

If decoding falls behind, the player MAY skip displaying overdue frames, but it MUST still reconstruct every dependent frame in order. It MUST NOT discard a P-frame's updates if a later P-frame depends on them.

---

## 16. Keyframe seeking

The index allows the player to seek without decoding from the beginning.

The decoder SHOULD load the keyframe index at startup if memory permits. If it does not, it MAY read index entries on demand.

For a target time:

1. Find the nearest preceding keyframe.
2. Seek to its absolute file offset.
3. Decode its complete I-frame.
4. Decode every subsequent stored frame required to reach the target interval.
5. Display the image valid at the target time.

The maximum dependency interval limits how much decoding is needed after a keyframe.

---

## 17. Validation and error handling

A decoder MUST reject malformed files safely and MUST NOT read beyond the file, palette, frame payload, or index.

### 17.1 File-level checks

- Magic and version.
- Header size and reserved fields.
- Width, height, FPS, frame count, and palette count.
- Palette offset and frame offset.
- Index offset and actual file bounds.
- Palette byte count.
- Total duration.
- File size below the 4 GiB v1 limit.

### 17.2 Frame-level checks

- Frame magic `FRM1`.
- Frame type is I or P.
- Only defined frame-flag bits are set; LZ4 and DEFLATE are not both set.
- DEFLATE is used only in minor version 2 or newer.
- Global motion is used only in minor version 3 or newer and only on P-frames.
- Header size is 20.
- Frame number is sequential.
- Duration is positive.
- Payload size is inside the file and before the index.
- First frame is an I-frame.
- A P-frame has a previously decoded reference image.

### 17.3 Packet-level checks

- Packet header is complete.
- Long run length is 33–8,191.
- Packet payload is fully present.
- Palette indices are valid.
- SKIP is not used in an I-frame.
- Packet coverage equals exactly `width × height`.
- Packet payload is consumed exactly.

### 17.4 Index-level checks

- Magic is `IDX1`.
- Entry count is consistent with the index size.
- Each entry points to a valid I-frame.
- Entries are strictly ordered.
- First entry references frame zero and elapsed time zero.
- Every I-frame has exactly one index entry.
- Index ends exactly at end-of-file.

On an error, the player SHOULD stop playback and report a useful message. It MUST NOT continue as if a partially reconstructed frame were valid.

---

## 18. Memory and performance considerations

At maximum resolution, one RGB565 image requires:

`528 × 320 × 2 = 337,920 bytes`

This is approximately 330 KiB.

The palette requires at most 512 bytes. The packet decoder itself requires only a small amount of state.

The calculator implementation SHOULD:

- Use buffered sequential reads.
- Avoid a nearest-colour search on the calculator.
- Avoid floating-point operations.
- Avoid recursion.
- Avoid allocation per packet or per pixel.
- Avoid copying the full framebuffer between frames when the reference surface can be updated in place.
- Apply global motion in place when possible: shift each row horizontally with overlap-safe memmove and fill exposed columns; then shift whole rows vertically in the appropriate copy direction and fill exposed rows. This needs no second full framebuffer and touches at most width × height RGB565 pixels.
- Use direct LCD VRAM as the reference surface when its layout permits.
- Refresh the display once per complete frame rather than once per pixel.

The desktop encoder and calculator decoder have different responsibilities. Palette generation, lookup-table creation, rate-distortion decisions, and packet optimisation happen on the desktop. The calculator only applies the stored decisions.

---

## 19. Compatibility and versioning

CPV v1.1, v1.2 and v1.3 require:

- Major version 1.
- Minor version 1 enables bit-packed palette literals, DELTA_MASK, and optional per-frame LZ4. Minor version 2 adds optional DEFLATE/Huffman-coded frame payloads. Minor version 3 adds optional global-motion prediction on P-frames.
- Minor version 0 files retain the original byte-index packet semantics and have no compression flags.
- A 64-byte file header.
- A 20-byte frame header.
- The four primary packet opcodes and the DELTA_MASK SKIP extension defined in Section 10.
- The packet-length encoding and reserved DELTA_MASK sentinel defined in Section 10.1.
- The bit-packed INDEX_LITERAL representation and DELTA_MASK. CPV v1.1 adds LZ4, v1.2 adds DEFLATE, and v1.3 adds global-motion prediction.
- An `IDX1` index at the end of the file.

A decoder MUST reject unknown major versions.

A strict v1.0 decoder MUST reject a non-zero frame flag, DELTA_MASK sentinel, or other v1.1-only encoding. A v1.3 decoder MUST accept v1.0, v1.1 and v1.2 files by using eight-bit INDEX_LITERAL values in minor version 0, rejecting DEFLATE flags in minor versions below 2, and rejecting global-motion flags in minor versions below 3. Unknown frame flag bits MUST be rejected. Any future change that alters the container header, reconstruction model, or core packet grammar requires a new major version.

---

## 20. Recommended host converter interface

The command-line interface is not part of the binary format, but the host converter SHOULD provide a stable interface similar to:

```bash
tools/cpv_convert input.mp4 \
    --output output.cpv \
    --width 528 \
    --height 320 \
    --fps 10 \
    --palette-quality 75 \
    --preprocess-quality 35
```

The repository's reference host encoder is implemented in native C++17. Build it with `make -C tools`. It uses FFmpeg/FFprobe for media decoding and palette generation, zlib for DEFLATE, and LZ4 for optional LZ4-HC compression.

Recommended options:

| Option | Meaning |
|---|---|
| `--output` | Output CPV file |
| `--width` | Output width, 1–528 |
| `--height` | Output height, 1–320 |
| `--fps` | Nominal output frame rate, up to 30 |
| `--palette-quality` | Global palette budget, 0–100 |
| `--keyframe-interval` | Keyframe target; default 2 seconds |
| `--fit contain` | Preserve aspect ratio with letterboxing |
| `--fit stretch` | Stretch to exact output dimensions |

The normal target profile is 528 × 320 at 10 FPS. Lower resolutions and frame rates SHOULD be offered as useful size-saving options.

---

## 21. Required implementation tests

A CPV v1.3 implementation SHOULD test every packet type and boundary.

### 21.1 Packet tests

- Short packets with lengths 1, 2, and 32.
- Long packets with lengths 33, 255, 256, and 8,191.
- Runs longer than 8,191 split across multiple packets.
- INDEX_LITERAL with varied palette indices at every supported bit width (1–8).
- Bit-packed literal packets with byte alignment and zero padding checks.
- DELTA_MASK spans of lengths 1, 7, 8, 9, 32, and 65,535.
- DELTA_MASK truncation, invalid indices, non-zero padding, zero length, and I-frame rejection.
- LZ4-compressed and uncompressed frames, including invalid declared sizes and truncated blocks.
- INDEX_REPEAT with a single palette index.
- SKIP in a P-frame.
- RAW565 with known little-endian RGB565 values.
- Invalid long lengths below 33.
- Truncated short and long headers.
- Truncated literal, repeat, and RAW565 payloads.
- Invalid palette indices.
- SKIP in an I-frame.
- Packet streams with too few or too many pixels.

### 21.2 Frame tests

- A 1 × 1 I-frame.
- Maximum 528 × 320 I-frame.
- An I-frame using INDEX_LITERAL and INDEX_REPEAT.
- An I-frame using RAW565.
- A P-frame that uses only SKIP.
- A P-frame that changes one pixel.
- A P-frame mixing all valid packet types.
- Consecutive identical source frames coalesced into a longer duration.
- A scene change followed by an I-frame.
- Variable frame durations.

### 21.3 File tests

- Minimum and maximum dimensions.
- Palette counts 1 and 256.
- Invalid offsets.
- Invalid frame count.
- Missing initial I-frame.
- Missing keyframe index entry.
- Unsorted index entries.
- Incorrect total duration.
- Extra bytes after index.
- Non-zero reserved fields.
- Unsupported version.
- Truncated frame header and payload.
- Files at and beyond the 4 GiB representable boundary.

### 21.4 Round-trip tests

For each generated test:

1. Encode source frames.
2. Decode the generated CPV file.
3. Compare decoded RGB565 images with the encoder's reconstructed images.
4. Require exact pixel equality.
5. Seek to every I-frame and verify that decoding from that keyframe produces the same images as decoding from the beginning.

For quality 100, decoded output SHOULD exactly match the source after RGB565 conversion.

---

## 22. Design rationale and relationship to the legacy CPV player

The original CPV player demonstrated that a global palette combined with linear delta coding can be compact and practical on the fx-CP400. CPV v1 intentionally retains that architecture instead of replacing it with a more complicated tile codec.

### 22.1 Improvements over the legacy packet stream

The legacy format primarily represented changed pixels as repeated single-index runs, alongside SKIP runs. This works well for flat areas but creates many small packets when adjacent changed pixels have different colours.

CPV v1 adds INDEX_LITERAL. A mixed sequence can now be stored as one packet containing consecutive palette indices. This is particularly useful for detailed animation, photographic texture, and gameplay.

The v1 packet header also defines short and long runs for four packet types. One-byte headers encode 1–32 pixels; two-byte headers encode 33–8,191 pixels.

This is a deliberate trade-off. The legacy packet header could encode 1–63 pixels in one byte and up to 16,383 pixels in two bytes, but it only distinguished DATA from SKIP. CPV v1 spends one additional header bit on the long-length form so it can identify INDEX_LITERAL, INDEX_REPEAT, SKIP, and RAW565 directly, without an extra mode byte on every data packet. This keeps data packets compact and makes decoding straightforward. Whether this trade-off wins for a particular video depends on its distribution of skip and data run lengths, so the host benchmark SHOULD report packet counts and bytes by opcode.

### 22.2 Explicit keyframes

The legacy stream relied on the initial display state and did not define a robust random-access index. CPV v1 requires the first frame to initialise every pixel and periodically inserts complete I-frames.

This removes dependence on an assumed initial screen colour and enables bounded seeking.

### 22.3 Safer sizes and dimensions

The legacy header used one-byte width and height fields, a 16-bit frame-size field, and did not define indexed keyframes. Those constraints make large frames and bounded seeking difficult. The legacy encoder also accumulated the complete compressed video in a growing memory buffer and repeatedly reallocated it.

CPV v1 uses 16-bit dimensions, a 32-bit payload size, explicit I-frames, a keyframe index, and streamed output. It also validates offsets, frame boundaries, packet coverage, and index entries.

### 22.4 Faster encoder mapping

A straightforward nearest-palette search performs up to 256 colour comparisons for every pixel. CPV v1 recommends a 65,536-byte RGB565-to-index lookup table built once per conversion.

The table replaces repeated palette searches with one array lookup per pixel.

### 22.5 Streaming conversion

An encoder SHOULD write completed frame records directly to a temporary output file. It SHOULD NOT accumulate the complete compressed video in a growing memory buffer.

This avoids unbounded memory growth and repeated large reallocations during long conversions.

### 22.6 Simple calculator decoding

The calculator does not need to build palettes, perform nearest-colour searches, analyse motion, or optimise packet choices.

Its main loop performs only:

- Read packet header.
- Determine opcode and run length.
- Read indices or RGB565 values as needed.
- Write pixels or advance over unchanged pixels.
- Update the persistent reference image.

Where the LCD VRAM can retain the image in the correct RGB565 layout, it can serve as that reference image and avoid an extra full-frame RAM buffer.

---

## 23. Benchmarking and acceptance criteria

Compression results MUST be evaluated using equivalent source material and equivalent output settings.

A meaningful comparison records:

- Exact source file and source duration.
- Output width and height.
- Output frame rate.
- Encoder quality setting and palette count.
- Keyframe interval.
- Output file size and bytes per second.
- Normalised bytes per decoded pixel, calculated as `file_size / (width × height × stored_frame_rate × duration_seconds)` for constant-rate source sampling.
- Conversion wall-clock time and conversion speed in source-seconds per wall-clock-second.
- Average and peak decode time per frame.
- Visual quality of representative scenes.

When comparing the legacy encoder with CPV v1, both MUST use the same source clip, output dimensions, frame rate, and as closely matched a visual quality target as practical. A 24-minute legacy file at a small resolution and low frame rate cannot be compared directly with a 30-second 528 × 320, 10 FPS file.

The 10 FPS target is a performance goal, not a guaranteed result.

A CPV v1 implementation is ready for calculator integration when:

- The encoder writes files conforming to this specification.
- The host decoder handles all four packet types.
- Encoder and decoder reconstruct identical RGB565 frames.
- Keyframe seeking produces the same output as sequential decoding.
- Malformed files fail safely.
- The calculator decoder performs no dynamic allocation per pixel or packet.
- File reads are buffered.
- 10 FPS playback is demonstrated on representative test clips at the chosen output profile.

File size alone is not a fair comparison. Resolution, FPS, duration, source material, and visual quality must be held constant before comparing CPV versions or encoders.
