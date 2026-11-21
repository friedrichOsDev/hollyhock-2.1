# CPV Video Format Specification

**Document:** CPV v1.0  
**File extension:** .cpv  
**Status:** Proposed format specification  
**Target platform:** Casio fx-CP400 running Hollyhock 2.1  
**Primary playback target:** 528 × 320 pixels at 10 frames per second  
**Byte order:** Little-endian

---

## 1. Scope and design goals

CPV is a compact, silent, tile-based video format designed for software decoding on the Casio fx-CP400. Its encoder is intended to run on a desktop computer, where it can spend substantially more time searching for compact representations. The calculator decoder is deliberately restricted to simple integer operations, bounded recursion, sequential reads, and direct RGB565 framebuffer updates.

The format is designed for mixed video content, including animation, live-action footage, gameplay, and screen recordings. No single image-content assumption is made.

The primary design goals are:

- Keep decoding predictable and inexpensive.
- Reduce file size by representing unchanged image regions without pixel data.
- Allow lossy colour quantisation to be performed by the desktop encoder.
- Adapt spatial compression granularity to local image complexity.
- Permit seeking through indexed keyframes.
- Avoid requiring a full compressed frame in RAM.
- Keep the file format deterministic and straightforward to validate.

### 1.1 Non-goals for version 1

CPV v1 does not define:

- Audio or subtitles.
- Inter-frame motion vectors or motion compensation.
- B-frames or bidirectional prediction.
- YUV or chroma-subsampled pixel formats.
- Huffman, arithmetic, or context-adaptive entropy coding.
- Alpha-channel storage.
- Decoder-side scaling or colour-space conversion.
- A mandatory encoder quantisation algorithm.

These features may be considered for a future major version. A v1 decoder must not assume their existence.

### 1.2 Conformance language

The words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and **MAY** are used as normative requirements.

A **CPV file** is a byte sequence following the container and frame syntax in this document.

A **decoder** is conforming when it correctly interprets every valid v1.0 file and rejects malformed or unsupported data without reading outside the file or writing outside the decoded image.

An **encoder** is conforming when it emits files satisfying all mandatory structural and semantic requirements in this document.

---

## 2. Core properties and limits

| Property | CPV v1 value |
|---|---|
| Maximum width | 528 pixels |
| Maximum height | 320 pixels |
| Target frame rate | 10 FPS |
| Format frame-rate range | Greater than 0 and at most 60 FPS |
| Pixel format | RGB565 |
| Pixel storage | 16 bits per pixel |
| Audio | Not supported |
| Frame types | I-frame and P-frame |
| Spatial coding | Adaptive quadtree tiles |
| Tile sizes | 32 × 32, 16 × 16, and 8 × 8 |
| Tile coding modes | SOLID, PALETTE, RLE565, RAW565, SPLIT, SKIP |
| Keyframe index | Required |
| Compression | Intra-tile palette/RLE/raw coding and unchanged-tile omission |

The dimensions stored in the file describe the actual video image. They do not describe the physical calculator display. A video may use any positive width and height not exceeding the limits.

The format does not require the video to fill the complete display. The player is responsible for positioning a smaller image on screen. Scaling is outside the v1 codec specification.

At the maximum resolution:

- Pixel count: 528 × 320 = 168,960 pixels.
- One RGB565 image: 337,920 bytes (330 KiB).
- Root tile grid: 17 columns × 10 rows = 170 root nodes.
- Minimum tile size: 8 × 8 pixels.

A decoder can use one 337,920-byte framebuffer at maximum resolution. A second full framebuffer is not required by the format.

---

## 3. General binary conventions

### 3.1 Integer representation

All multi-byte integer fields are stored in **little-endian** byte order.

Examples:

- A 16-bit value 0x1234 is stored as bytes 34 12.
- A 32-bit value 0x12345678 is stored as bytes 78 56 34 12.
- A 64-bit value 0x0123456789ABCDEF is stored as bytes EF CD AB 89 67 45 23 01.

Unsigned integer fields are interpreted as unsigned values. Signed integers are not used in the v1 file syntax.

### 3.2 Coordinate system and pixel order

The image coordinate origin is the top-left corner:

- X increases from left to right.
- Y increases from top to bottom.
- The first pixel is at coordinate (0, 0).

Pixels inside a tile are serialized in row-major order: left to right, then top to bottom.

All tile rectangles are clipped to the actual image bounds. Pixels outside the image are never encoded or written.

### 3.3 Reserved fields

Reserved fields and reserved flag bits MUST be written as zero.

A v1 decoder MUST reject a file if a reserved field or flag bit is non-zero, unless a future specification explicitly assigns meaning to it.

### 3.4 Exact byte consumption

A decoder MUST respect the declared file, frame, and payload boundaries. It MUST NOT read beyond the current frame payload while decoding a tile.

After decoding all root tiles of a frame, the decoder's input position MUST equal the end of that frame's payload exactly. Remaining or missing payload bytes make the frame invalid.

---

## 4. Pixel format

### 4.1 RGB565

CPV v1 uses 16-bit RGB565 pixels:

| Bits | Component | Number of levels |
|---|---|---:|
| 15–11 | Red | 32 |
| 10–5 | Green | 64 |
| 4–0 | Blue | 32 |

The 16-bit word is:

~~~text
15                         0
RRRRR GGGGGG BBBBB
~~~

The word is stored as a little-endian 16-bit integer. The first byte in the file contains bits 0–7; the second byte contains bits 8–15.

### 4.2 Converting RGB888 input

A desktop encoder converting an 8-bit-per-channel RGB source SHOULD use round-to-nearest conversion:

~~~text
R5 = (R8 * 31 + 127) / 255
G6 = (G8 * 63 + 127) / 255
B5 = (B8 * 31 + 127) / 255

RGB565 = (R5 << 11) | (G6 << 5) | B5
~~~

All divisions in this formula are integer divisions.

CPV does not store alpha. An encoder receiving alpha-capable input MUST composite it onto a background before RGB565 conversion. The background colour is an encoder option and is not stored in the CPV file.

### 4.3 Decoding RGB565

The decoded pixel value is the RGB565 word itself. A player may write it directly to a compatible 16-bit RGB565 framebuffer.

If the platform's display memory uses another pixel format, the player is responsible for conversion. Such conversion is not part of the CPV bitstream.

---

## 5. File layout

A CPV v1 file consists of three consecutive regions:

~~~text
+------------------------------+
| File Header                  |
| 40 bytes                     |
+------------------------------+
| Frame Records                |
| Frame Header + Frame Payload |
| repeated frame_count times   |
+------------------------------+
| Frame Index                  |
| IDX1 header + index entries  |
+------------------------------+
~~~

The first frame record starts immediately after the file header. The frame records end exactly at the file's index offset. The index is mandatory and is the final structure in the file.

All offsets in the file are absolute byte offsets from the beginning of the file.

---

## 6. File header

The v1.0 file header is exactly 40 bytes.

| Offset | Size | Field | Type | Required value / meaning |
|---:|---:|---|---|---|
| 0 | 4 | magic | char[4] | ASCII bytes CPV followed by NUL |
| 4 | 2 | version_major | uint16 | 1 |
| 6 | 2 | version_minor | uint16 | 0 |
| 8 | 4 | header_size | uint32 | 40 |
| 12 | 2 | width | uint16 | 1–528 |
| 14 | 2 | height | uint16 | 1–320 |
| 16 | 2 | fps_num | uint16 | Numerator of nominal frame rate |
| 18 | 2 | fps_den | uint16 | Denominator of nominal frame rate |
| 20 | 4 | frame_count | uint32 | Number of stored frame records |
| 24 | 4 | flags | uint32 | 0 in v1.0 |
| 28 | 8 | index_offset | uint64 | Absolute byte offset of IDX1 |
| 36 | 4 | total_duration_ms | uint32 | Sum of all frame durations |

### 6.1 Magic and version

The magic bytes are:

~~~text
43 50 56 00
 C  P  V NUL
~~~

A decoder MUST reject a file with a different magic value.

For this specification, version 1.0 is identified by:

- version_major = 1
- version_minor = 0

A decoder that does not support the specified major version MUST reject the file. A decoder implementing this document MUST require the minor version to be 0.

### 6.2 Dimensions

Width and height MUST both be non-zero.

Width MUST NOT exceed 528. Height MUST NOT exceed 320.

### 6.3 Frame rate

The nominal frame rate is:

~~~text
fps = fps_num / fps_den
~~~

Both fields MUST be non-zero. The resulting rate MUST be greater than 0 and no greater than 60 frames per second.

The recommended target is 10/1 FPS.

The frame rate is descriptive timing metadata. Actual presentation timing is controlled by each frame's duration field. A player MUST use frame durations for playback timing rather than assuming that every frame lasts exactly 1000/fps milliseconds.

### 6.4 Frame count and duration

frame_count MUST be greater than zero.

total_duration_ms MUST be greater than zero and MUST equal the sum of duration_ms over all stored frame records.

The duration is limited by the 32-bit field. Encoders MUST reject or split content whose total duration cannot be represented.

### 6.5 Flags

The v1.0 file-level flags field MUST be zero. No optional file-level features are defined in v1.0.

### 6.6 Header size and index offset

header_size MUST equal 40 in v1.0.

index_offset MUST point to the first byte of the mandatory frame index. It MUST be at or after the end of the file header and MUST be within the physical file.

The frame-record region is the byte range:

~~~text
[header_size, index_offset)
~~~

The index MUST end exactly at the end of the file. A v1.0 file MUST NOT contain trailing bytes after the index.

---

## 7. Frame records

Every frame record consists of a 16-byte frame header followed immediately by the declared payload.

~~~text
+------------------------------+
| Frame Header                 |
| 16 bytes                     |
+------------------------------+
| Frame Payload                |
| payload_size bytes           |
+------------------------------+
~~~

### 7.1 Frame header

| Offset | Size | Field | Type | Required value / meaning |
|---:|---:|---|---|---|
| 0 | 1 | frame_type | uint8 | 0 = I-frame, 1 = P-frame |
| 1 | 1 | flags | uint8 | 0 in v1.0 |
| 2 | 2 | header_size | uint16 | 16 |
| 4 | 4 | duration_ms | uint32 | Display duration of this frame |
| 8 | 4 | payload_size | uint32 | Number of payload bytes |
| 12 | 4 | frame_number | uint32 | Zero-based sequential frame number |

The frame payload begins immediately after byte 15 of the frame header.

### 7.2 Frame types

| Value | Name | Meaning |
|---:|---|---|
| 0 | I-frame | Self-contained image; no SKIP nodes are allowed |
| 1 | P-frame | Image update relative to the previously decoded frame |

The first stored frame MUST be an I-frame.

A P-frame refers only to the immediately preceding stored frame. There are no references to future frames or arbitrary older frames.

### 7.3 Frame numbering

The first frame has frame_number 0. Every following stored frame increments the value by exactly one.

The frame header's frame_number MUST match its position in the frame-record sequence. Duplicate, missing, or out-of-order frame numbers are invalid.

### 7.4 Frame duration and timeline

duration_ms MUST be greater than zero.

The presentation start time of frame 0 is 0 ms. The presentation start time of every subsequent frame is the sum of all preceding frame durations.

~~~text
frame_start_ms[0] = 0
frame_start_ms[n] = sum(duration_ms[0 .. n-1])
~~~

The frame is displayed for duration_ms milliseconds.

Consecutive identical source images MAY be coalesced by an encoder into one stored frame with a longer duration. In that case, frame_count describes the number of stored frame records, not the number of original input samples.

### 7.5 Keyframe interval

Frame 0 MUST be an I-frame.

An encoder SHOULD emit an I-frame at least once per 1000 ms of presentation time. It MAY emit additional I-frames at scene changes or whenever doing so improves compression or decode performance.

Every I-frame MUST have a corresponding frame-index entry.

### 7.6 Frame payload

The frame payload is a sequence of encoded root tile nodes. It contains no additional frame-level prefix.

Root nodes are serialized in row-major order: left to right across the top row, followed by the next row.

The decoder can determine the number and geometry of root nodes entirely from the width and height in the file header.

---

## 8. Adaptive tile tree

CPV uses an adaptive quadtree. Every image begins as a grid of nominal 32 × 32 root tiles. A root tile may remain a 32 × 32 node or be split into four smaller nodes. A 16 × 16 node may also be split. Splitting stops at 8 × 8.

The only permitted node sizes are:

- 32 × 32
- 16 × 16
- 8 × 8

### 8.1 Root grid

The root grid dimensions are:

~~~text
root_columns = ceil(width / 32)
root_rows    = ceil(height / 32)
~~~

The root nodes are visited in row-major order.

At the maximum resolution of 528 × 320, the root grid is 17 columns by 10 rows, for 170 root nodes.

### 8.2 Edge clipping

A node has a nominal square size S and an image-space origin (x, y).

Its visible dimensions are:

~~~text
visible_width  = min(S, width  - x)
visible_height = min(S, height - y)
~~~

The node is valid only when x is less than width and y is less than height.

Only visible pixels are encoded. No padding pixels are present in the bitstream.

### 8.3 Split geometry

A SPLIT node divides a square node into four children with side length S/2.

The child order is fixed:

1. Top-left:  (x,       y)
2. Top-right: (x + S/2, y)
3. Bottom-left: (x,       y + S/2)
4. Bottom-right: (x + S/2, y + S/2)

A child is present only when its top-left coordinate lies inside the image:

~~~text
child_x < width AND child_y < height
~~~

Children whose top-left coordinate is outside the image are omitted entirely. A child that partially extends beyond the image is present but clipped to the image bounds.

The decoder derives the number and order of children from the parent position and image dimensions. No child count or child coordinates are stored.

SPLIT is valid only for nodes of size 32 or 16. A node of size 8 MUST NOT use SPLIT.

### 8.4 Node traversal

Each node starts with exactly one mode byte. The mode determines how the remainder of that node is parsed.

For SPLIT, the decoder reads the mode byte and then immediately decodes all present children in the order defined above. Child nodes are concatenated directly, without lengths or separators.

This recursive representation is unambiguous because the decoder knows each node's size, coordinates, valid children, and decoded pixel count.

Maximum recursion depth is two SPLIT operations: 32 → 16 → 8.

---

## 9. Tile coding modes

The first byte of every tile node is its mode.

| Mode byte | Name | Allowed frame type | Meaning |
|---:|---|---|---|
| 0x00 | SOLID | I or P | Fill the visible node with one RGB565 value |
| 0x01 | PALETTE | I or P | Palette followed by packed pixel indices |
| 0x02 | RLE565 | I or P | Run-length encoded RGB565 pixels |
| 0x03 | RAW565 | I or P | Uncompressed RGB565 pixels |
| 0x04 | SPLIT | I or P | Recursively decode child nodes |
| 0x05 | SKIP | P only | Keep pixels from the preceding frame |
| 0x06–0xFF | Reserved | Not allowed | Invalid in v1.0 |

Unknown mode values MUST be rejected.

Let N be the visible pixel count of the current node:

~~~text
N = visible_width * visible_height
~~~

The visible pixels are always interpreted in row-major order.

### 9.1 SOLID — 0x00

Layout:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Mode byte = 0x00 |
| 1 | 2 | RGB565 colour, uint16 little-endian |

Total node size: 3 bytes.

The decoder fills every visible pixel in the node with the specified colour.

For a lossless encoding, the encoder uses SOLID only when all converted RGB565 pixels in the node are identical. A lossy encoder MAY also use SOLID to represent a varying source region with one representative colour, provided its configured quality policy permits the resulting distortion.

### 9.2 PALETTE — 0x01

PALETTE stores a local palette and a packed index for each visible pixel.

Layout:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Mode byte = 0x01 |
| 1 | 1 | palette_count_minus_one |
| 2 | 2 × palette_count | Palette entries, RGB565 uint16 values |
| Following | ceil(N × index_bits / 8) | Packed pixel indices |

The actual palette size is:

~~~text
palette_count = palette_count_minus_one + 1
~~~

The valid palette_count range is 2 through 256. A palette with one colour MUST use SOLID instead.

palette_count MUST NOT exceed N.

The number of bits per pixel index is:

~~~text
index_bits = ceil(log2(palette_count))
~~~

The palette entries are RGB565 words in little-endian order.

#### 9.2.1 Packed index bit order

Indices are packed into a continuous bitstream. Rows are not byte-aligned.

For pixel index i, its first bit is at bit position:

~~~text
bit_offset = i * index_bits
~~~

Bits are stored least-significant-bit first within each byte. The least-significant bit of the index is stored first.

The bitstream starts immediately after the final palette entry. No alignment or padding is inserted between the palette and the index stream.

The number of index-stream bytes is:

~~~text
index_bytes = ceil(N * index_bits / 8)
~~~

If the last byte is only partially used, all unused high bits MUST be zero.

Every decoded index MUST be less than palette_count. An out-of-range index makes the node invalid.

PALETTE is the primary lossy coding mode. The encoder may map several source colours to one palette entry. The decoder simply looks up the RGB565 value for each index; it performs no quantisation.

### 9.3 RLE565 — 0x02

RLE565 encodes a linear sequence of RGB565 pixels using literal and repeated-colour tokens.

Layout:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Mode byte = 0x02 |
| 1 onward | Variable | RLE token stream |

Each token starts with one control byte.

- If bit 7 is 0, the token is a literal token.
- If bit 7 is 1, the token is a repeat token.

The number of pixels represented by a token is:

~~~text
count = (control & 0x7F) + 1
~~~

#### 9.3.1 Literal token

For a literal token, the decoder reads count consecutive RGB565 words after the control byte.

~~~text
control: 0x00 .. 0x7F
count:   1 .. 128
data:    count * uint16 RGB565 values
~~~

#### 9.3.2 Repeat token

For a repeat token, the decoder reads one RGB565 word and writes it count times.

~~~text
control: 0x80 .. 0xFF
count:   1 .. 128
data:    one uint16 RGB565 value
~~~

#### 9.3.3 Token stream termination

The decoder reads tokens until exactly N pixels have been produced.

Tokens may cross image row boundaries because RLE operates on the node's linear row-major pixel sequence.

A token that would produce more than N pixels is invalid. A token whose required data bytes extend beyond the frame payload is invalid.

The token stream ends immediately after the token that produces the Nth pixel. No terminator byte is used.

An encoder SHOULD use repeat tokens only when they are smaller than the equivalent literal representation. A repeat token of length one is valid but normally inefficient.

RLE565 is lossless relative to the RGB565 values it stores.

### 9.4 RAW565 — 0x03

Layout:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Mode byte = 0x03 |
| 1 onward | N × 2 | RGB565 words in row-major order |

The node contains exactly N little-endian RGB565 words.

Total node size:

~~~text
1 + (N * 2) bytes
~~~

RAW565 is the fallback coding mode for complex or noisy regions. It is lossless relative to the converted RGB565 input.

### 9.5 SPLIT — 0x04

Layout:

| Relative offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Mode byte = 0x04 |
| Following | Variable | Child node records |

SPLIT contains no explicit length or child count. The child nodes follow directly in the fixed order defined in Section 8.3.

SPLIT is permitted only for 32 × 32 and 16 × 16 nodes.

A decoder MUST reject SPLIT at 8 × 8.

SPLIT allows a large region to use a compact representation while a visually complex subregion uses smaller tiles. It also permits individual children of a P-frame to use SKIP.

### 9.6 SKIP — 0x05

SKIP contains only its mode byte.

It is valid only in P-frames. An I-frame containing SKIP is invalid.

The decoder MUST leave every visible pixel in the node unchanged from the immediately preceding decoded frame.

SKIP does not copy from an arbitrary reference frame and does not use motion vectors. It preserves the same image-space rectangle.

A P-frame may contain SKIP at any valid node size. A parent may use SPLIT so that some children are skipped while other children are replaced.

---

## 10. I-frame and P-frame decoding semantics

### 10.1 I-frame

An I-frame describes the complete image.

The decoder starts with the root tile at coordinate (0, 0) and decodes every root node in row-major order.

Every visible pixel MUST be written by the I-frame. SKIP is forbidden.

The decoder MUST NOT depend on framebuffer contents from before the I-frame.

### 10.2 P-frame

A P-frame updates the image produced by the immediately preceding stored frame.

The decoder processes every root node in the same root-grid order as an I-frame.

- SOLID, PALETTE, RLE565, and RAW565 replace the node's visible pixels.
- SPLIT recursively processes the child regions.
- SKIP preserves the previous frame's pixels in that region.

P-frames are sparse replacement frames, not pixel-residual frames. The codec does not subtract pixels from the previous frame and does not reconstruct motion-compensated blocks.

### 10.3 Reference image semantics

The reference image for a P-frame is the fully reconstructed RGB565 image of the immediately preceding stored frame.

This is important for lossy encoding: an encoder MUST make SKIP decisions against the image that a conforming decoder will actually have reconstructed, not merely against the original uncompressed source frame.

A desktop encoder SHOULD maintain a local reconstruction of its own output while encoding so that later P-frames use exactly the same reference pixels as the calculator decoder.

### 10.4 In-place decoding

The format permits in-place decoding into one RGB565 framebuffer:

- I-frame nodes write the complete image.
- P-frame replacement nodes overwrite their corresponding areas.
- SKIP nodes perform no writes.

A second framebuffer is not required for codec correctness. A player MAY use double buffering if required by its display implementation.

---

## 11. Frame index

The frame index is mandatory and follows the final frame payload immediately.

It begins with the four ASCII bytes IDX1:

~~~text
49 44 58 31
 I  D  X  1
~~~

### 11.1 Index header

| Relative offset | Size | Field | Type |
|---:|---:|---|---|
| 0 | 4 | magic | char[4], IDX1 |
| 4 | 4 | entry_count | uint32 |

entry_count is the number of I-frames in the file.

### 11.2 Index entry

Each entry is exactly 16 bytes.

| Relative offset | Size | Field | Type | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | frame_number | uint32 | Stored frame number of the I-frame |
| 4 | 4 | elapsed_ms | uint32 | Presentation start time of that I-frame |
| 8 | 8 | file_offset | uint64 | Absolute offset of that frame's frame header |

Entries are stored in increasing presentation-time order.

### 11.3 Index requirements

The following rules are mandatory:

1. entry_count MUST be greater than zero.
2. The first entry MUST refer to frame 0.
3. Frame 0's elapsed_ms MUST be 0.
4. Every I-frame MUST have exactly one index entry.
5. No P-frame may have an index entry.
6. Frame numbers, elapsed times, and file offsets MUST be strictly increasing.
7. file_offset MUST point to the first byte of the corresponding frame header.
8. Each entry MUST match the frame type, frame number, and calculated presentation time found by parsing the frame sequence.
9. entry_count MUST equal the number of I-frames.
10. The index MUST end exactly at end-of-file.

The total index size is:

~~~text
index_size = 8 + (entry_count * 16)
~~~

The index begins at the absolute file offset stored in the file header's index_offset field.

The complete file size is therefore:

~~~text
file_size = index_offset + 8 + (entry_count * 16)
~~~

A decoder MUST use overflow-safe arithmetic when validating this equation.

### 11.4 Seeking

To seek to a target presentation time:

1. Find the index entry with the greatest elapsed_ms that is less than or equal to the target time.
2. Seek to that entry's file_offset.
3. Decode its I-frame.
4. Decode subsequent frame records in order until the target time falls within the decoded frame's presentation interval.
5. Display that frame for its remaining duration.

A decoder MUST NOT start decoding a P-frame without first decoding its referenced preceding image. The index guarantees a way to find an earlier I-frame.

---

## 12. Encoder requirements and recommendations

The file syntax does not mandate a particular desktop quantisation implementation. Different encoders may produce different valid CPV files from the same source.

The following requirements and recommendations define the intended encoder behaviour.

### 12.1 Input preparation

An encoder SHOULD:

1. Decode input video using a desktop multimedia library such as FFmpeg.
2. Apply the requested output frame rate, normally 10 FPS.
3. Resize or letterbox the input to dimensions no larger than 528 × 320.
4. Composite transparency onto a background.
5. Convert source pixels to RGB565.
6. Encode the prepared frames as I-frames and P-frames.

The CPV file does not store the original source dimensions, crop rectangle, or scaling filter.

### 12.2 Candidate selection

For every node, the encoder SHOULD evaluate the legal coding options:

- SOLID
- PALETTE
- RLE565
- RAW565
- SPLIT, when the node size permits it

For P-frames, SKIP SHOULD also be evaluated.

The encoder should include all mode bytes, palette data, packed indices, and recursive child mode bytes in its size calculations. It should not compare only the compressed pixel payloads.

The encoder SHOULD choose the candidate that offers the best result under its configured quality policy. A simple recommended objective is:

~~~text
cost = encoded_size_bytes + lambda * distortion
~~~

Where:

- encoded_size_bytes is the complete number of bytes needed for the node and descendants.
- distortion measures the difference between the source RGB565 pixels and the reconstructed RGB565 pixels.
- lambda controls the trade-off between file size and image quality.

The quality policy and lambda value are encoder parameters. They are not stored in the CPV v1 bitstream.

### 12.3 Lossy colour quantisation

Lossy quantisation is permitted and expected.

PALETTE is the primary mechanism for mapping many source colours to fewer representative RGB565 colours. SOLID may also be used as a lossy representation of a region if the encoder's quality policy permits it.

The decoder does not perform quantisation. All quantisation is completed by the encoder. The decoder only reads RGB565 palette values and indices.

An encoder MAY use median cut, weighted median cut, k-means, or another palette-generation algorithm. It SHOULD evaluate the final reconstructed tile rather than relying only on the number of unique source colours.

For a useful quality metric, an encoder may calculate weighted squared RGB565 error:

~~~text
dR = source_R5 - decoded_R5
dG = source_G6 - decoded_G6
dB = source_B5 - decoded_B5

pixel_error = 3*(dR*dR) + 4*(dG*dG) + 2*(dB*dB)
tile_distortion = sum(pixel_error for all visible pixels)
~~~

This weighting gives the green component a larger influence because RGB565 allocates more precision to green. It is a recommendation, not a decoder requirement.

A quality setting of 100 SHOULD avoid additional lossy quantisation beyond conversion to RGB565. Lower quality settings MAY permit progressively greater distortion. Exact mappings between user-facing quality values and quantisation thresholds are encoder-specific and SHOULD be documented by each encoder.

### 12.4 Adaptive tile choice

An encoder SHOULD compare keeping a node intact against splitting it.

For a 32 × 32 node, the candidates include direct coding at 32 × 32 and a SPLIT representation containing 16 × 16 children. The same decision applies to a 16 × 16 node and its 8 × 8 children.

The encoder SHOULD include the SPLIT mode byte and all child records in the comparison.

This permits large tiles in visually simple areas and smaller tiles where local changes or colour complexity justify the additional node metadata.

### 12.5 P-frame generation

For each P-frame, the encoder SHOULD compare the target frame with its own reconstructed previous frame.

If a region can be represented acceptably by retaining the previous reconstructed pixels, the encoder MAY write SKIP.

Changed regions are encoded as complete replacement tiles. No residual data is generated.

The encoder SHOULD insert keyframes at least once per second and at scene changes.

### 12.6 Repeated frames

When two or more consecutive prepared frames reconstruct to the same RGB565 image, the encoder MAY combine their display durations into one stored frame record.

The resulting frame_count and index MUST describe the stored records after coalescing.

---

## 13. Decoder algorithm

A conforming decoder can use the following high-level procedure.

### 13.1 File validation

1. Read the 40-byte file header.
2. Validate magic, version, header size, dimensions, frame rate, flags, frame count, duration, and index offset.
3. Validate that the index is inside the physical file and ends at end-of-file.
4. Read and validate the index.
5. Verify that the first indexed keyframe is frame 0.

A decoder MAY validate the complete frame sequence before playback or validate each frame as it is decoded. It MUST still enforce all bounds during playback.

### 13.2 Sequential frame decoding

For each frame:

1. Read exactly 16 bytes of frame header.
2. Validate frame type, flags, header size, duration, payload size, and frame number.
3. Set the frame payload end position.
4. Calculate the root grid from the file dimensions.
5. Decode every root node in row-major order.
6. Require the input position to equal the frame payload end position.
7. Add duration_ms to the running presentation time.
8. Present the decoded framebuffer for the frame duration.

### 13.3 Node decoding pseudocode

~~~text
decode_node(x, y, size, frame_type):
    mode = read_u8()

    visible_width  = min(size, image_width  - x)
    visible_height = min(size, image_height - y)
    pixel_count = visible_width * visible_height

    switch mode:
        SOLID:
            colour = read_u16_le()
            fill_node(x, y, visible_width, visible_height, colour)

        PALETTE:
            decode_palette_node(...)

        RLE565:
            decode_rle_node(...)

        RAW565:
            read pixel_count RGB565 values
            write them in row-major order

        SPLIT:
            require size is 32 or 16
            decode each present child in fixed child order

        SKIP:
            require frame_type is P
            leave node pixels unchanged

        otherwise:
            fail frame
~~~

For a node partly outside the image, pixel_count is based only on its visible width and height.

### 13.4 Row-major framebuffer writes

For a framebuffer with a pitch of width pixels, a pixel at (x, y) is written at:

~~~text
framebuffer[y * width + x]
~~~

A platform with a different framebuffer pitch may use its actual row stride instead.

### 13.5 Streaming and memory use

A decoder SHOULD read compressed data incrementally from the file rather than loading an entire frame payload into RAM.

The largest possible RGB565 framebuffer is 337,920 bytes. A tile-sized temporary buffer is not required for correctness; pixels may be written as they are decoded.

A palette contains at most 256 RGB565 entries and therefore requires at most 512 bytes.

The maximum recursion depth is two. A decoder can implement node traversal recursively or iteratively.

---

## 14. Error handling and validation

A decoder MUST reject a file or frame when any of the following conditions occur:

- Invalid magic or unsupported version.
- Invalid dimensions or frame rate.
- Non-zero reserved flags or fields.
- Invalid header or frame-header size.
- Frame count or frame duration is zero.
- Frame number is not the expected sequential value.
- Frame payload extends beyond index_offset.
- Unknown tile mode.
- SKIP occurs in an I-frame.
- SPLIT occurs at an 8 × 8 node.
- PALETTE has fewer than 2 entries or more than 256 entries.
- PALETTE has more entries than visible pixels.
- A palette index is outside the palette.
- Unused high bits in the last palette byte are non-zero.
- RLE expands to more or fewer than the node's required pixel count.
- RLE token data is truncated or expands beyond the node.
- A tile read would cross the frame payload boundary.
- The decoded root-node sequence does not consume exactly payload_size bytes.
- An I-frame or P-frame violates its reference-image requirements.
- The index does not match the actual frame sequence.
- The calculated total duration differs from total_duration_ms.
- The index does not end exactly at end-of-file.

On an invalid frame, a player SHOULD stop playback and report a decoding error. It MUST NOT continue parsing from an uncertain byte position.

A decoder MUST validate all dimensions and lengths before using them for memory addressing or allocation. Arithmetic used to calculate byte counts and offsets MUST be checked for overflow.

### 14.1 Integrity

CPV v1.0 does not define frame checksums or a file checksum. Structural validation detects many malformed files but does not guarantee detection of arbitrary bit corruption.

An implementation that needs stronger integrity guarantees may use an external checksum or container such as an archive, but such data is not part of the CPV v1.0 syntax.

---

## 15. Compatibility and versioning

A v1.0 decoder supports:

- File version 1.0.
- RGB565.
- I-frames and P-frames.
- The six tile modes defined in Section 9.
- The mandatory IDX1 frame index.

Unknown file-level flags, frame flags, tile modes, or non-zero reserved fields MUST be rejected.

A future format that changes the meaning of existing fields or tile modes MUST increment the major version.

A future minor version may add backward-compatible features only when a v1.0 decoder can safely ignore them. No such optional features are defined by this document.

---

## 16. Reference constants

The following constants are useful for implementations.

~~~c
#define CPV_VERSION_MAJOR 1
#define CPV_VERSION_MINOR 0

#define CPV_HEADER_SIZE       40
#define CPV_FRAME_HEADER_SIZE 16
#define CPV_INDEX_HEADER_SIZE 8
#define CPV_INDEX_ENTRY_SIZE  16

#define CPV_MAX_WIDTH  528
#define CPV_MAX_HEIGHT 320

#define CPV_ROOT_TILE_SIZE 32
#define CPV_MIN_TILE_SIZE  8

#define CPV_MODE_SOLID   0x00
#define CPV_MODE_PALETTE 0x01
#define CPV_MODE_RLE565  0x02
#define CPV_MODE_RAW565  0x03
#define CPV_MODE_SPLIT   0x04
#define CPV_MODE_SKIP    0x05

#define CPV_FRAME_I 0
#define CPV_FRAME_P 1
~~~

These constants describe the on-disk format. An implementation may use different internal names or data structures.

---

## 17. Implementation test checklist

A decoder and encoder implementation should be tested with at least the following cases.

### 17.1 Header and container tests

- Valid 1 × 1 file.
- Valid maximum-size 528 × 320 file.
- Width 529 rejected.
- Height 321 rejected.
- Zero width or height rejected.
- Invalid magic rejected.
- Unsupported version rejected.
- Non-zero reserved flags rejected.
- Incorrect index offset rejected.
- Truncated file header rejected.
- Trailing bytes after the index rejected.

### 17.2 Tile mode tests

- SOLID 32 × 32 node.
- SOLID clipped edge node.
- PALETTE with 2 entries.
- PALETTE with 3 entries, exercising non-power-of-two palette size.
- PALETTE with 256 entries.
- PALETTE index crossing a byte boundary.
- PALETTE with non-zero unused high bits rejected.
- PALETTE index outside palette rejected.
- RLE literal token of length 1.
- RLE literal token of length 128.
- RLE repeat token of length 1.
- RLE repeat token of length 128.
- RLE token exceeding remaining pixel count rejected.
- RLE truncated colour value rejected.
- RAW565 node.
- SPLIT from 32 × 32 to 16 × 16.
- SPLIT from 16 × 16 to 8 × 8.
- SPLIT at 8 × 8 rejected.
- SKIP in P-frame.
- SKIP in I-frame rejected.

### 17.3 Frame and timeline tests

- First frame is an I-frame.
- P-frame immediately after I-frame.
- Multiple consecutive P-frames.
- I-frame after P-frames.
- Frame number discontinuity rejected.
- Zero frame duration rejected.
- Incorrect total_duration_ms rejected.
- Frame payload with extra trailing bytes rejected.
- Frame payload truncated at every tile mode boundary.
- Keyframe index with correct offsets and timestamps.
- Seeking to the first frame.
- Seeking to a time between two keyframes.
- Seeking to the final presentation interval.

### 17.4 Visual and performance tests

- Flat-colour animation.
- Anime with flat colour regions and sharp outlines.
- Live-action footage with fine texture.
- Gameplay with large moving regions.
- Screen recordings with text and cursor movement.
- Static background with a small moving object.
- Full-screen scene change.
- Repeated identical frames.
- 10 FPS playback at 528 × 320.
- Long playback to detect timing drift and memory leaks.

---

## 18. Summary

CPV v1.0 is a silent RGB565 video format built around adaptive 32 × 32 / 16 × 16 / 8 × 8 tile trees.

Each leaf tile uses one of four image representations:

- SOLID for a single representative colour.
- PALETTE for compact indexed colour, including lossy quantisation.
- RLE565 for repeated RGB565 values.
- RAW565 for direct pixel storage.

SPLIT selects smaller spatial regions, while SKIP preserves unchanged regions in P-frames. I-frames provide complete reference images; P-frames replace only the regions that need updating. A mandatory index points to every I-frame and enables seeking.

The desktop encoder performs resizing, RGB565 conversion, palette generation, quantisation, tile selection, and keyframe placement. The calculator decoder performs only bounded tree traversal, palette lookup, simple run-length expansion, and RGB565 framebuffer writes.

This separation is the central design principle of CPV: **spend computation during conversion so that playback remains simple and predictable on the calculator.**
