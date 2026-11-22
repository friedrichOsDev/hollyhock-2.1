#ifndef CPV_DECODER_HPP
#define CPV_DECODER_HPP

#include <stdint.h>
#include <ui/filesystem.hpp>

namespace CPV {

typedef void (*DebugCallback)(void *context, const char *operation,
                              uint32_t current, uint32_t total);

struct KeyframeEntry {
    uint32_t frameNumber;
    uint32_t timeMs;
    uint32_t fileOffset;
};

class Decoder {
public:
    Decoder();
    ~Decoder();

    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    bool Open(const char *path);
    void SetDebugCallback(DebugCallback callback, void *context);
    void Close();

    // Decode the next stored frame. The first successful call decodes frame 0.
    bool DecodeNext();

    // Reconstruct the frame containing targetMs, starting at the preceding
    // indexed I-frame. All dependent P-frames are decoded in order.
    bool Seek(uint32_t targetMs);

    bool IsOpen() const;
    bool HasFrame() const;
    bool AtEnd() const;

    uint16_t Width() const;
    uint16_t Height() const;
    uint16_t FpsNumerator() const;
    uint16_t FpsDenominator() const;
    uint32_t FrameCount() const;
    uint16_t PaletteCount() const;
    uint32_t DurationMs() const;
    uint32_t CurrentFrame() const;
    uint32_t CurrentTimeMs() const;
    uint32_t CurrentFrameDurationMs() const;
    const uint16_t *Pixels() const;
    const char *Error() const;

    // Last-stage diagnostics for the calculator's text debug view.
    const char *LastOperation() const;
    uint32_t LastReadBytes() const;
    uint32_t LastStoredFrameBytes() const;
    uint32_t LastDecodedFrameBytes() const;
    uint32_t LastPacketCount() const;
    uint32_t PacketCount(uint8_t opcode) const;
    uint32_t LastSeekFrames() const;
    uint8_t LastCompression() const;

private:
    struct FrameHeader {
        uint8_t type;
        uint8_t flags;
        uint32_t number;
        uint32_t durationMs;
        uint32_t payloadSize;
    };

    UI::File m_file;
    uint32_t m_fileSize;
    uint32_t m_framesOffset;
    uint32_t m_indexOffset;
    uint32_t m_nextOffset;
    uint32_t m_elapsedMs;
    uint32_t m_frameStartMs;
    uint32_t m_currentFrame;
    uint32_t m_frameDurationMs;
    uint32_t m_totalDurationMs;
    uint32_t m_frameCount;
    uint32_t m_pixelCount;
    uint32_t m_streamCapacity;
    uint32_t m_storedCapacity;
    uint16_t m_width;
    uint16_t m_height;
    uint16_t m_fpsNumerator;
    uint16_t m_fpsDenominator;
    uint16_t m_paletteCount;
    uint8_t m_versionMinor;
    uint8_t m_indexBits;
    uint16_t m_palette[256];

    KeyframeEntry *m_keyframes;
    uint32_t m_keyframeCount;
    uint8_t *m_storedBuffer;
    uint8_t *m_decodedBuffer;
    uint16_t *m_pixels;

    bool m_open;
    bool m_hasFrame;

    const char *m_debugOperation;
    uint32_t m_lastReadBytes;
    uint32_t m_lastStoredFrameBytes;
    uint32_t m_lastDecodedFrameBytes;
    uint32_t m_lastPacketCount;
    uint32_t m_lastPacketTypes[5];
    uint32_t m_lastSeekFrames;
    uint8_t m_lastCompression;
    DebugCallback m_debugCallback;
    void *m_debugContext;

    char m_error[128];

    bool ReadAt(uint32_t offset, void *buffer, uint32_t size);
    bool ReadFrameHeader(uint32_t offset, FrameHeader *header);
    bool ValidateFile();
    bool DecodeFrameAt(uint32_t offset, const FrameHeader &header);
    bool DecodePackets(const uint8_t *payload, uint32_t payloadSize,
                       const FrameHeader &header);
    bool ReadPackedIndex(const uint8_t *data, uint32_t dataSize,
                         uint32_t index, uint8_t *value) const;
    bool ValidatePackedPadding(const uint8_t *data, uint32_t dataSize,
                               uint32_t count) const;
    void SetError(const char *message);
    void SetOperation(const char *operation);
    void NotifyDebug(uint32_t current, uint32_t total);
};

} // namespace CPV

#endif
