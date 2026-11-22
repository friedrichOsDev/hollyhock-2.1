#include "cpv_decoder.hpp"

#include <sdk/os/mem.hpp>
#include <stdint.h>

#include "cpv_decompress.hpp"
#include "cpv_motion.hpp"

namespace CPV {
namespace {

static const uint32_t kFileHeaderSize = 64;
static const uint32_t kFrameHeaderSize = 20;
static const uint32_t kIndexHeaderSize = 8;
static const uint32_t kIndexEntrySize = 12;
static const uint32_t kFrameFlagLZ4 = 0x01;
static const uint32_t kFrameFlagDeflate = 0x02;
static const uint32_t kFrameFlagGlobalMotion = 0x04;
static const uint32_t kFrameCompressionMask = 0x03;
static const uint32_t kFrameFlagsMask = 0x07;
static const uint32_t kFrameI = 0;
static const uint32_t kFrameP = 1;
static const uint32_t kMaxWidth = 528;
static const uint32_t kMaxHeight = 320;
static const uint32_t kMaxRunLength = 8191;
static const uint32_t kMaxMotion = 8;

static uint16_t ReadU16(const uint8_t *data) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8));
}

static uint32_t ReadU32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

static bool Equal4(const uint8_t *data, char a, char b, char c, char d) {
    return data[0] == static_cast<uint8_t>(a) &&
           data[1] == static_cast<uint8_t>(b) &&
           data[2] == static_cast<uint8_t>(c) &&
           data[3] == static_cast<uint8_t>(d);
}

static void CopyError(char *destination, const char *source) {
    if (!destination)
        return;
    unsigned i = 0;
    if (source) {
        while (source[i] && i < 127) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = 0;
}

} // namespace

Decoder::Decoder()
    : m_fileSize(0),
      m_framesOffset(0),
      m_indexOffset(0),
      m_nextOffset(0),
      m_elapsedMs(0),
      m_frameStartMs(0),
      m_currentFrame(0xFFFFFFFFU),
      m_frameDurationMs(0),
      m_totalDurationMs(0),
      m_frameCount(0),
      m_pixelCount(0),
      m_streamCapacity(0),
      m_storedCapacity(0),
      m_width(0),
      m_height(0),
      m_fpsNumerator(0),
      m_fpsDenominator(0),
      m_paletteCount(0),
      m_versionMinor(0),
      m_indexBits(8),
      m_keyframes(0),
      m_keyframeCount(0),
      m_storedBuffer(0),
      m_decodedBuffer(0),
      m_pixels(0),
      m_open(false),
      m_hasFrame(false),
      m_debugOperation("IDLE"),
      m_lastReadBytes(0),
      m_lastStoredFrameBytes(0),
      m_lastDecodedFrameBytes(0),
      m_lastPacketCount(0),
      m_lastSeekFrames(0),
      m_lastCompression(0),
      m_debugCallback(0),
      m_debugContext(0) {
    m_error[0] = 0;
    for (unsigned i = 0; i < 5; ++i)
        m_lastPacketTypes[i] = 0;
    for (unsigned i = 0; i < 256; ++i)
        m_palette[i] = 0;
}

Decoder::~Decoder() {
    Close();
}

void Decoder::SetError(const char *message) {
    CopyError(m_error, message);
}

void Decoder::SetOperation(const char *operation) {
    m_debugOperation = operation ? operation : "UNKNOWN";
    NotifyDebug(0, 0);
}

void Decoder::NotifyDebug(uint32_t current, uint32_t total) {
    if (m_debugCallback)
        m_debugCallback(m_debugContext, m_debugOperation, current, total);
}

void Decoder::SetDebugCallback(DebugCallback callback, void *context) {
    m_debugCallback = callback;
    m_debugContext = context;
}

const char *Decoder::Error() const {
    return m_error[0] ? m_error : "Unknown CPV error";
}

const char *Decoder::LastOperation() const {
    return m_debugOperation ? m_debugOperation : "UNKNOWN";
}

uint32_t Decoder::LastReadBytes() const { return m_lastReadBytes; }
uint32_t Decoder::LastStoredFrameBytes() const { return m_lastStoredFrameBytes; }
uint32_t Decoder::LastDecodedFrameBytes() const { return m_lastDecodedFrameBytes; }
uint32_t Decoder::LastPacketCount() const { return m_lastPacketCount; }
uint32_t Decoder::PacketCount(uint8_t opcode) const {
    return opcode < 5 ? m_lastPacketTypes[opcode] : 0;
}
uint32_t Decoder::LastSeekFrames() const { return m_lastSeekFrames; }
uint8_t Decoder::LastCompression() const { return m_lastCompression; }

bool Decoder::ReadAt(uint32_t offset, void *buffer, uint32_t size) {
    m_lastReadBytes = 0;
    if (!m_file.IsOpen() || (!buffer && size) ||
        offset > m_fileSize || size > m_fileSize - offset ||
        offset > 0x7FFFFFFFU || size > 0x7FFFFFFFU) {
        SetError("Read outside the file");
        return false;
    }
    if (m_file.Seek(static_cast<int>(offset), SEEK_SET) < 0) {
        SetError("File seek failed");
        return false;
    }

    uint8_t *destination = static_cast<uint8_t *>(buffer);
    uint32_t remaining = size;
    while (remaining) {
        const int request = remaining > 0x7FFFFFFFU
            ? 0x7FFFFFFF : static_cast<int>(remaining);
        const int received = m_file.Read(destination, request);
        if (received <= 0 || static_cast<uint32_t>(received) > remaining) {
            SetError("Unexpected end of file");
            return false;
        }
        destination += received;
        m_lastReadBytes += static_cast<uint32_t>(received);
        remaining -= static_cast<uint32_t>(received);
        if (size >= 512U)
            NotifyDebug(m_lastReadBytes, size);
    }
    return true;
}

void Decoder::Close() {
    m_file.Close();
    if (m_keyframes) free(m_keyframes);
    if (m_storedBuffer) free(m_storedBuffer);
    if (m_decodedBuffer) free(m_decodedBuffer);
    if (m_pixels) free(m_pixels);

    m_keyframes = 0;
    m_storedBuffer = 0;
    m_decodedBuffer = 0;
    m_pixels = 0;
    m_keyframeCount = 0;
    m_open = false;
    m_hasFrame = false;
    m_fileSize = 0;
    m_currentFrame = 0xFFFFFFFFU;
    m_nextOffset = 0;
}

bool Decoder::Open(const char *path) {
    Close();
    m_error[0] = 0;
    SetOperation("OPEN FILE");

    if (m_file.Open(path) < 0) {
        SetError("Cannot open CPV file");
        return false;
    }
    m_open = true;

    if (m_file.GetSize(&m_fileSize) < 0 || m_fileSize < 74 ||
        m_fileSize > 0x7FFFFFFFU) {
        SetError("Invalid file size or file exceeds 2 GiB");
        Close();
        return false;
    }

    SetOperation("READ FILE HEADER");
    uint8_t header[kFileHeaderSize];
    if (!ReadAt(0, header, sizeof(header))) {
        Close();
        return false;
    }

    if (!Equal4(header, 'C', 'P', 'V', 0) ||
        header[4] != 1 ||
        header[5] > 3 ||
        ReadU16(header + 6) != kFileHeaderSize) {
        SetError("Unsupported CPV header or version");
        Close();
        return false;
    }

    m_versionMinor = header[5];
    if (ReadU32(header + 8) != 0 ||
        ReadU16(header + 26) != 0) {
        SetError("Non-zero reserved file header fields");
        Close();
        return false;
    }
    for (unsigned i = 44; i < 64; ++i) {
        if (header[i] != 0) {
            SetError("Non-zero reserved file header bytes");
            Close();
            return false;
        }
    }

    m_width = ReadU16(header + 12);
    m_height = ReadU16(header + 14);
    m_fpsNumerator = ReadU16(header + 16);
    m_fpsDenominator = ReadU16(header + 18);
    m_frameCount = ReadU32(header + 20);
    m_paletteCount = ReadU16(header + 24);
    const uint32_t paletteOffset = ReadU32(header + 28);
    m_framesOffset = ReadU32(header + 32);
    m_indexOffset = ReadU32(header + 36);
    m_totalDurationMs = ReadU32(header + 40);

    if (m_width == 0 || m_width > kMaxWidth ||
        m_height == 0 || m_height > kMaxHeight ||
        m_fpsNumerator == 0 || m_fpsDenominator == 0 ||
        static_cast<uint32_t>(m_fpsNumerator) >
            static_cast<uint32_t>(m_fpsDenominator) * 30U ||
        m_frameCount == 0 ||
        m_paletteCount == 0 || m_paletteCount > 256 ||
        m_totalDurationMs == 0 ||
        paletteOffset != kFileHeaderSize ||
        m_framesOffset != paletteOffset +
            static_cast<uint32_t>(m_paletteCount) * 2U ||
        m_indexOffset <= m_framesOffset ||
        m_indexOffset >= m_fileSize) {
        SetError("Invalid CPV dimensions, timing, or offsets");
        Close();
        return false;
    }

    m_pixelCount = static_cast<uint32_t>(m_width) *
                   static_cast<uint32_t>(m_height);
    m_streamCapacity = m_pixelCount * 3U + 4U;
    m_storedCapacity = m_streamCapacity + 4U;
    m_indexBits = 8;
    if (m_versionMinor != 0) {
        uint16_t value = static_cast<uint16_t>(m_paletteCount - 1U);
        m_indexBits = 1;
        while (value > 1) {
            value >>= 1;
            ++m_indexBits;
        }
    }

    if (m_fileSize > 0x7FFFFFFFU ||
        m_storedCapacity < m_streamCapacity) {
        SetError("CPV allocation size is not representable");
        Close();
        return false;
    }

    SetOperation("READ PALETTE");
    uint8_t paletteBytes[512];
    if (!ReadAt(paletteOffset, paletteBytes,
                static_cast<uint32_t>(m_paletteCount) * 2U)) {
        Close();
        return false;
    }
    for (unsigned i = 0; i < m_paletteCount; ++i) {
        m_palette[i] = ReadU16(paletteBytes + i * 2U);
        for (unsigned previous = 0; previous < i; ++previous) {
            if (m_palette[previous] == m_palette[i]) {
                SetError("Duplicate colour in global palette");
                Close();
                return false;
            }
        }
    }

    SetOperation("ALLOCATE BUFFERS");
    m_storedBuffer = static_cast<uint8_t *>(malloc(m_storedCapacity));
    m_decodedBuffer = static_cast<uint8_t *>(malloc(m_streamCapacity));
    m_pixels = static_cast<uint16_t *>(
        malloc(m_pixelCount * static_cast<uint32_t>(sizeof(uint16_t))));
    if (!m_storedBuffer || !m_decodedBuffer || !m_pixels) {
        SetError("Not enough memory for CPV decoder buffers");
        Close();
        return false;
    }

    for (uint32_t i = 0; i < m_pixelCount; ++i)
        m_pixels[i] = 0;

    if (!ValidateFile()) {
        Close();
        return false;
    }

    m_nextOffset = m_framesOffset;
    m_elapsedMs = 0;
    m_frameStartMs = 0;
    m_currentFrame = 0xFFFFFFFFU;
    m_frameDurationMs = 0;
    m_hasFrame = false;
    SetOperation("READY");
    return true;
}

bool Decoder::ReadFrameHeader(uint32_t offset, FrameHeader *header) {
    if (!header)
        return false;
    uint8_t bytes[kFrameHeaderSize];
    if (!ReadAt(offset, bytes, sizeof(bytes)))
        return false;
    if (!Equal4(bytes, 'F', 'R', 'M', '1')) {
        SetError("Invalid frame magic");
        return false;
    }
    if (bytes[4] > kFrameP || ReadU16(bytes + 6) != kFrameHeaderSize) {
        SetError("Invalid frame type or header size");
        return false;
    }
    header->type = bytes[4];
    header->flags = bytes[5];
    header->number = ReadU32(bytes + 8);
    header->durationMs = ReadU32(bytes + 12);
    header->payloadSize = ReadU32(bytes + 16);
    return true;
}

bool Decoder::ValidateFile() {
    SetOperation("VALIDATE INDEX");
    uint8_t indexHeader[kIndexHeaderSize];
    if (!ReadAt(m_indexOffset, indexHeader, sizeof(indexHeader)))
        return false;
    if (!Equal4(indexHeader, 'I', 'D', 'X', '1')) {
        SetError("Missing CPV keyframe index");
        return false;
    }

    if (m_fileSize - m_indexOffset < kIndexHeaderSize) {
        SetError("Truncated CPV keyframe index header");
        return false;
    }
    const uint32_t indexCount = ReadU32(indexHeader + 4);
    if (indexCount == 0 || indexCount > m_frameCount ||
        indexCount > 65535U ||
        indexCount > (m_fileSize - m_indexOffset -
                      kIndexHeaderSize) / kIndexEntrySize ||
        m_indexOffset + kIndexHeaderSize +
            indexCount * kIndexEntrySize != m_fileSize) {
        SetError("Invalid keyframe index size");
        return false;
    }

    m_keyframes = static_cast<KeyframeEntry *>(
        malloc(indexCount * static_cast<uint32_t>(sizeof(KeyframeEntry))));
    if (!m_keyframes) {
        SetError("Not enough memory for keyframe index");
        return false;
    }

    uint32_t position = m_framesOffset;
    uint32_t elapsed = 0;
    uint32_t lastKeyTime = 0;
    uint32_t expectedKeys = 0;

    SetOperation("SCAN FRAME HEADERS");
    for (uint32_t number = 0; number < m_frameCount; ++number) {
        if ((number & 0x0FU) == 0)
            NotifyDebug(number, m_frameCount);
        if (position > m_indexOffset ||
            kFrameHeaderSize > m_indexOffset - position) {
            SetError("Frame header overlaps keyframe index");
            return false;
        }

        FrameHeader frame;
        if (!ReadFrameHeader(position, &frame))
            return false;
        if (frame.number != number || frame.durationMs == 0 ||
            (number == 0 && frame.type != kFrameI)) {
            SetError("Invalid frame number, duration, or first frame");
            return false;
        }

        const uint8_t flags = frame.flags;
        const uint8_t compression = flags & kFrameCompressionMask;
        if ((flags & ~kFrameFlagsMask) != 0 ||
            compression == kFrameCompressionMask ||
            (compression == kFrameFlagLZ4 && m_versionMinor < 1) ||
            (compression == kFrameFlagDeflate && m_versionMinor < 2) ||
            ((flags & kFrameFlagGlobalMotion) && m_versionMinor < 3) ||
            ((flags & kFrameFlagGlobalMotion) && frame.type != kFrameP) ||
            (m_versionMinor == 0 && flags != 0)) {
            SetError("Unsupported or invalid frame flags");
            return false;
        }

        if (frame.type == kFrameI) {
            lastKeyTime = elapsed;
            if (expectedKeys >= indexCount) {
                SetError("Keyframe index is missing entries");
                return false;
            }
            m_keyframes[expectedKeys].frameNumber = number;
            m_keyframes[expectedKeys].timeMs = elapsed;
            m_keyframes[expectedKeys].fileOffset = position;
            ++expectedKeys;
        } else if (elapsed - lastKeyTime > 10000U) {
            SetError("P-frame exceeds the 10-second keyframe interval");
            return false;
        }

        const uint32_t maximumDecoded =
            m_pixelCount * 3U +
            ((flags & kFrameFlagGlobalMotion) ? 4U : 0U);
        const uint32_t maximumStored = compression
            ? maximumDecoded + 4U : maximumDecoded;
        if (frame.payloadSize == 0 ||
            frame.payloadSize > maximumStored ||
            (compression && frame.payloadSize < 5U)) {
            SetError("Frame payload size is outside the supported bound");
            return false;
        }

        const uint32_t payloadOffset = position + kFrameHeaderSize;
        if (payloadOffset > m_indexOffset ||
            frame.payloadSize > m_indexOffset - payloadOffset) {
            SetError("Frame payload overlaps or exceeds the keyframe index");
            return false;
        }
        if (elapsed > 0xFFFFFFFFU - frame.durationMs) {
            SetError("Total duration exceeds the CPV v1 limit");
            return false;
        }

        elapsed += frame.durationMs;
        position = payloadOffset + frame.payloadSize;
    }

    if (position != m_indexOffset || elapsed != m_totalDurationMs ||
        expectedKeys != indexCount) {
        SetError("Frame sequence or duration does not match the file header");
        return false;
    }
    m_keyframeCount = expectedKeys;

    uint32_t previousNumber = 0;
    uint32_t previousTime = 0;
    uint32_t previousOffset = 0;
    SetOperation("READ KEYFRAME INDEX");
    for (uint32_t i = 0; i < indexCount; ++i) {
        if ((i & 0x0FU) == 0)
            NotifyDebug(i, indexCount);
        uint8_t entryBytes[kIndexEntrySize];
        const uint32_t offset = m_indexOffset + kIndexHeaderSize +
                                i * kIndexEntrySize;
        if (!ReadAt(offset, entryBytes, sizeof(entryBytes)))
            return false;
        const KeyframeEntry entry = {
            ReadU32(entryBytes),
            ReadU32(entryBytes + 4),
            ReadU32(entryBytes + 8)
        };
        const KeyframeEntry &expected = m_keyframes[i];
        if (entry.frameNumber != expected.frameNumber ||
            entry.timeMs != expected.timeMs ||
            entry.fileOffset != expected.fileOffset ||
            (i > 0 && (entry.frameNumber <= previousNumber ||
                       entry.timeMs <= previousTime ||
                       entry.fileOffset <= previousOffset))) {
            SetError("Keyframe index does not match frame records");
            return false;
        }
        previousNumber = entry.frameNumber;
        previousTime = entry.timeMs;
        previousOffset = entry.fileOffset;
    }

    if (m_keyframes[0].frameNumber != 0 ||
        m_keyframes[0].timeMs != 0 ||
        m_keyframes[0].fileOffset != m_framesOffset) {
        SetError("The first keyframe index entry is invalid");
        return false;
    }
    SetOperation("VALIDATION COMPLETE");
    return true;
}

bool Decoder::ReadPackedIndex(const uint8_t *data, uint32_t dataSize,
                              uint32_t index, uint8_t *value) const {
    if (!data || !value || m_indexBits == 0)
        return false;
    const uint32_t bitOffset = index * m_indexBits;
    const uint32_t byteOffset = bitOffset >> 3;
    const unsigned shift = bitOffset & 7U;
    if (byteOffset >= dataSize)
        return false;

    uint32_t packed = data[byteOffset];
    if (shift + m_indexBits > 8U) {
        if (byteOffset + 1U >= dataSize)
            return false;
        packed |= static_cast<uint32_t>(data[byteOffset + 1U]) << 8;
    }
    const uint32_t mask = (1U << m_indexBits) - 1U;
    const uint32_t indexValue = (packed >> shift) & mask;
    if (indexValue >= m_paletteCount)
        return false;
    *value = static_cast<uint8_t>(indexValue);
    return true;
}

bool Decoder::ValidatePackedPadding(const uint8_t *data, uint32_t dataSize,
                                    uint32_t count) const {
    if (!data && dataSize)
        return false;
    const uint32_t totalBits = count * m_indexBits;
    const uint32_t expectedBytes = (totalBits + 7U) >> 3;
    if (expectedBytes != dataSize)
        return false;
    const unsigned usedBits = totalBits & 7U;
    if (usedBits == 0 || dataSize == 0)
        return true;
    const uint8_t unusedMask =
        static_cast<uint8_t>(0xFFU << usedBits);
    return (data[dataSize - 1U] & unusedMask) == 0;
}

bool Decoder::DecodeFrameAt(uint32_t offset, const FrameHeader &header) {
    m_lastStoredFrameBytes = 0;
    m_lastDecodedFrameBytes = 0;
    m_lastPacketCount = 0;
    m_lastCompression = header.flags & kFrameCompressionMask;
    for (unsigned i = 0; i < 5; ++i)
        m_lastPacketTypes[i] = 0;
    SetOperation("READ FRAME PAYLOAD");
    if (header.payloadSize > m_storedCapacity ||
        !ReadAt(offset + kFrameHeaderSize, m_storedBuffer,
                header.payloadSize))
        return false;
    m_lastStoredFrameBytes = header.payloadSize;

    const uint8_t compression = header.flags & kFrameCompressionMask;
    const uint8_t *payload = m_storedBuffer;
    uint32_t payloadSize = header.payloadSize;

    if (compression) {
        if (payloadSize < 5U) {
            SetError("Truncated compressed frame");
            return false;
        }
        const uint32_t decodedSize = ReadU32(payload);
        const uint32_t maximumDecoded =
            m_pixelCount * 3U +
            ((header.flags & kFrameFlagGlobalMotion) ? 4U : 0U);
        if (decodedSize == 0 || decodedSize > maximumDecoded ||
            payloadSize - 4U >= decodedSize) {
            SetError("Invalid compressed frame size");
            return false;
        }

        bool decoded = false;
        if (compression == kFrameFlagDeflate) {
            SetOperation("INFLATE DEFLATE");
            decoded = InflateZlib(payload + 4, payloadSize - 4,
                                  m_decodedBuffer, m_streamCapacity,
                                  decodedSize);
        } else if (compression == kFrameFlagLZ4) {
            SetOperation("DECOMPRESS LZ4");
            decoded = DecompressLZ4Block(payload + 4, payloadSize - 4,
                                         m_decodedBuffer, m_streamCapacity,
                                         decodedSize);
        }
        if (!decoded) {
            SetError("Frame decompression failed");
            return false;
        }
        payload = m_decodedBuffer;
        payloadSize = decodedSize;
        m_lastDecodedFrameBytes = decodedSize;
    } else if (payloadSize > m_streamCapacity) {
        SetError("Uncompressed frame exceeds the safety limit");
        return false;
    }

    if (m_lastDecodedFrameBytes == 0)
        m_lastDecodedFrameBytes = payloadSize;

    uint32_t packetOffset = 0;
    if (header.flags & kFrameFlagGlobalMotion) {
        SetOperation("APPLY GLOBAL MOTION");
        if (header.type != kFrameP || payloadSize < 4U) {
            SetError("Invalid or truncated global-motion header");
            return false;
        }
        const int dx = static_cast<int8_t>(payload[0]);
        const int dy = static_cast<int8_t>(payload[1]);
        const uint16_t fill = ReadU16(payload + 2);
        if ((dx == 0 && dy == 0) ||
            dx < -static_cast<int>(kMaxMotion) ||
            dx > static_cast<int>(kMaxMotion) ||
            dy < -static_cast<int>(kMaxMotion) ||
            dy > static_cast<int>(kMaxMotion) ||
            dx <= -static_cast<int>(m_width) ||
            dx >= static_cast<int>(m_width) ||
            dy <= -static_cast<int>(m_height) ||
            dy >= static_cast<int>(m_height) ||
            !cpv_apply_global_motion(m_pixels, m_width, m_height,
                                     dx, dy, fill)) {
            SetError("Invalid global-motion vector");
            return false;
        }
        packetOffset = 4;
    }

    SetOperation("DECODE PACKETS");
    if (!DecodePackets(payload + packetOffset,
                       payloadSize - packetOffset, header))
        return false;

    m_frameStartMs = m_elapsedMs;
    m_frameDurationMs = header.durationMs;
    m_elapsedMs += header.durationMs;
    m_currentFrame = header.number;
    m_nextOffset = offset + kFrameHeaderSize + header.payloadSize;
    m_hasFrame = true;
    SetOperation("FRAME COMPLETE");
    return true;
}

bool Decoder::DecodePackets(const uint8_t *payload, uint32_t payloadSize,
                            const FrameHeader &header) {
    SetOperation("DECODE PACKETS");
    m_lastPacketCount = 0;
    for (unsigned i = 0; i < 5; ++i)
        m_lastPacketTypes[i] = 0;
    uint32_t position = 0;
    uint32_t pixelPosition = 0;

    while (position < payloadSize) {
        if (pixelPosition >= m_pixelCount) {
            SetError("Packet stream contains extra pixel data");
            return false;
        }

        const uint8_t first = payload[position++];
        const uint8_t opcode = first >> 6;
        uint32_t runLength = 0;
        bool deltaMask = false;

        if (opcode == 2 && first == 0xA0 && position < payloadSize &&
            payload[position] == 0) {
            if (m_versionMinor < 1 || payloadSize - position < 3U) {
                SetError("Invalid DELTA_MASK sentinel");
                return false;
            }
            ++position;
            runLength = ReadU16(payload + position);
            position += 2;
            if (runLength == 0) {
                SetError("Zero-length DELTA_MASK span");
                return false;
            }
            deltaMask = true;
        } else if ((first & 0x20U) == 0) {
            runLength = (first & 0x1FU) + 1U;
        } else {
            if (position >= payloadSize) {
                SetError("Truncated long packet header");
                return false;
            }
            runLength =
                (static_cast<uint32_t>(first & 0x1FU) << 8) |
                payload[position++];
            if (runLength < 33U || runLength > kMaxRunLength) {
                SetError("Invalid long packet length");
                return false;
            }
        }

        ++m_lastPacketCount;
        ++m_lastPacketTypes[deltaMask ? 4U : opcode];

        if (runLength > m_pixelCount - pixelPosition) {
            SetError("Packet extends beyond the frame");
            return false;
        }

        if (deltaMask) {
            if (header.type != kFrameP) {
                SetError("DELTA_MASK is invalid in an I-frame");
                return false;
            }
            const uint32_t maskSize = (runLength + 7U) >> 3;
            if (maskSize > payloadSize - position) {
                SetError("Truncated DELTA_MASK bitmap");
                return false;
            }
            const uint8_t *mask = payload + position;
            const unsigned usedBits = runLength & 7U;
            if (usedBits != 0) {
                const uint8_t unusedMask =
                    static_cast<uint8_t>(0xFFU << usedBits);
                if ((mask[maskSize - 1U] & unusedMask) != 0) {
                    SetError("Non-zero DELTA_MASK padding bits");
                    return false;
                }
            }

            uint32_t changed = 0;
            for (uint32_t i = 0; i < runLength; ++i) {
                if ((mask[i >> 3] & (1U << (i & 7U))) != 0)
                    ++changed;
            }
            if (changed == 0 || changed == runLength) {
                SetError("DELTA_MASK must contain changed and unchanged pixels");
                return false;
            }

            position += maskSize;
            const uint32_t packedSize =
                (changed * m_indexBits + 7U) >> 3;
            if (packedSize > payloadSize - position ||
                !ValidatePackedPadding(payload + position,
                                       packedSize, changed)) {
                SetError("Invalid DELTA_MASK palette data");
                return false;
            }

            uint32_t changedIndex = 0;
            for (uint32_t i = 0; i < runLength; ++i) {
                if ((mask[i >> 3] & (1U << (i & 7U))) == 0)
                    continue;
                uint8_t paletteIndex = 0;
                if (!ReadPackedIndex(payload + position, packedSize,
                                     changedIndex, &paletteIndex)) {
                    SetError("Invalid DELTA_MASK palette index");
                    return false;
                }
                m_pixels[pixelPosition + i] = m_palette[paletteIndex];
                ++changedIndex;
            }
            position += packedSize;
        } else if (opcode == 0) {
            const uint32_t packedSize =
                (runLength * m_indexBits + 7U) >> 3;
            if (packedSize > payloadSize - position ||
                !ValidatePackedPadding(payload + position,
                                       packedSize, runLength)) {
                SetError("Invalid INDEX_LITERAL data");
                return false;
            }
            for (uint32_t i = 0; i < runLength; ++i) {
                uint8_t paletteIndex = 0;
                if (!ReadPackedIndex(payload + position, packedSize,
                                     i, &paletteIndex)) {
                    SetError("Invalid INDEX_LITERAL palette index");
                    return false;
                }
                m_pixels[pixelPosition + i] = m_palette[paletteIndex];
            }
            position += packedSize;
        } else if (opcode == 1) {
            if (position >= payloadSize) {
                SetError("Truncated INDEX_REPEAT packet");
                return false;
            }
            const uint8_t paletteIndex = payload[position++];
            if (paletteIndex >= m_paletteCount) {
                SetError("Invalid INDEX_REPEAT palette index");
                return false;
            }
            for (uint32_t i = 0; i < runLength; ++i)
                m_pixels[pixelPosition + i] = m_palette[paletteIndex];
        } else if (opcode == 2) {
            if (header.type != kFrameP) {
                SetError("SKIP is invalid in an I-frame");
                return false;
            }
            // The persistent RGB565 surface already contains the prediction.
        } else if (opcode == 3) {
            const uint32_t byteCount = runLength * 2U;
            if (byteCount > payloadSize - position) {
                SetError("Truncated RAW565 packet");
                return false;
            }
            for (uint32_t i = 0; i < runLength; ++i)
                m_pixels[pixelPosition + i] =
                    ReadU16(payload + position + i * 2U);
            position += byteCount;
        } else {
            SetError("Unknown packet opcode");
            return false;
        }

        pixelPosition += runLength;
        if ((m_lastPacketCount & 0x3FU) == 0)
            NotifyDebug(pixelPosition, m_pixelCount);
    }

    if (pixelPosition != m_pixelCount || position != payloadSize) {
        SetError("Packet stream does not reconstruct exactly one frame");
        return false;
    }
    return true;
}

bool Decoder::DecodeNext() {
    SetOperation("DECODE NEXT FRAME");
    if (!m_open || AtEnd())
        return false;

    const uint32_t number = m_currentFrame == 0xFFFFFFFFU
        ? 0U : m_currentFrame + 1U;
    FrameHeader header;
    if (!ReadFrameHeader(m_nextOffset, &header))
        return false;
    if (header.number != number) {
        SetError("Unexpected frame number while decoding");
        return false;
    }
    return DecodeFrameAt(m_nextOffset, header);
}

bool Decoder::Seek(uint32_t targetMs) {
    SetOperation("FIND KEYFRAME");
    m_lastSeekFrames = 0;
    if (!m_open || m_keyframeCount == 0)
        return false;
    if (targetMs >= m_totalDurationMs)
        targetMs = m_totalDurationMs - 1U;

    uint32_t keyIndex = 0;
    for (uint32_t i = 1; i < m_keyframeCount; ++i) {
        if (m_keyframes[i].timeMs > targetMs)
            break;
        keyIndex = i;
    }

    const KeyframeEntry &key = m_keyframes[keyIndex];
    m_currentFrame = key.frameNumber == 0
        ? 0xFFFFFFFFU : key.frameNumber - 1U;
    m_nextOffset = key.fileOffset;
    m_elapsedMs = key.timeMs;
    m_frameStartMs = key.timeMs;
    m_frameDurationMs = 0;
    m_hasFrame = false;

    while (DecodeNext()) {
        ++m_lastSeekFrames;
        m_debugOperation = "SEEK DECODED FRAME";
        NotifyDebug(m_lastSeekFrames, m_frameCount);
        if (targetMs >= m_frameStartMs &&
            targetMs - m_frameStartMs < m_frameDurationMs) {
            SetOperation("SEEK COMPLETE");
            return true;
        }
    }
    SetError("Unable to decode the requested seek position");
    return false;
}

bool Decoder::IsOpen() const { return m_open; }
bool Decoder::HasFrame() const { return m_hasFrame; }

bool Decoder::AtEnd() const {
    return m_currentFrame != 0xFFFFFFFFU &&
           m_currentFrame + 1U >= m_frameCount;
}

uint16_t Decoder::Width() const { return m_width; }
uint16_t Decoder::Height() const { return m_height; }
uint16_t Decoder::FpsNumerator() const { return m_fpsNumerator; }
uint16_t Decoder::FpsDenominator() const { return m_fpsDenominator; }
uint32_t Decoder::FrameCount() const { return m_frameCount; }
uint16_t Decoder::PaletteCount() const { return m_paletteCount; }
uint32_t Decoder::DurationMs() const { return m_totalDurationMs; }
uint32_t Decoder::CurrentFrame() const { return m_currentFrame; }
uint32_t Decoder::CurrentTimeMs() const { return m_frameStartMs; }
uint32_t Decoder::CurrentFrameDurationMs() const { return m_frameDurationMs; }
const uint16_t *Decoder::Pixels() const { return m_pixels; }

} // namespace CPV
