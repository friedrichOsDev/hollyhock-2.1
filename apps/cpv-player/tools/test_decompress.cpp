#include "../cpv_decompress.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

namespace {
int failures = 0;
int checks = 0;

void Check(bool condition, const char *name) {
    ++checks;
    if (condition)
        printf("PASS: %s\n", name);
    else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

bool Compress(const uint8_t *source, uint32_t sourceSize, int level,
              int strategy, uint8_t *destination, uint32_t capacity,
              uint32_t *compressedSize) {
    z_stream stream = {};
    if (deflateInit2(&stream, level, Z_DEFLATED, MAX_WBITS, 8, strategy) != Z_OK)
        return false;
    stream.next_in = const_cast<Bytef *>(source);
    stream.avail_in = sourceSize;
    stream.next_out = destination;
    stream.avail_out = capacity;
    const int result = deflate(&stream, Z_FINISH);
    const bool ok = result == Z_STREAM_END;
    *compressedSize = static_cast<uint32_t>(stream.total_out);
    deflateEnd(&stream);
    return ok;
}
} // namespace

int main() {
    static uint8_t source[32768];
    static uint8_t compressed[65536];
    static uint8_t decoded[32768];
    for (uint32_t i = 0; i < sizeof(source); ++i) {
        const uint32_t phase = i % 251U;
        source[i] = phase < 180U
            ? static_cast<uint8_t>((i / 3U) % 17U)
            : static_cast<uint8_t>((i * 29U + i / 7U) & 0xFFU);
    }

    const int levels[] = {0, 1, 6, 9};
    const int strategies[] = {
        Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED
    };
    for (unsigned l = 0; l < sizeof(levels) / sizeof(levels[0]); ++l) {
        for (unsigned s = 0; s < sizeof(strategies) / sizeof(strategies[0]); ++s) {
            uint32_t compressedSize = 0;
            const bool encoded = Compress(source, sizeof(source), levels[l],
                strategies[s], compressed, sizeof(compressed), &compressedSize);
            const bool decodedOk = encoded && CPV::InflateZlib(
                compressed, compressedSize, decoded, sizeof(decoded), sizeof(source));
            Check(decodedOk && memcmp(source, decoded, sizeof(source)) == 0,
                  "zlib stream round-trips (stored/fixed/dynamic strategies)");
        }
    }

    uint32_t compressedSize = 0;
    Check(Compress(source, sizeof(source), 9, Z_DEFAULT_STRATEGY,
                   compressed, sizeof(compressed), &compressedSize),
          "zlib test stream is generated");
    if (compressedSize > 0) {
        compressed[compressedSize - 1U] ^= 0x01U;
        Check(!CPV::InflateZlib(compressed, compressedSize, decoded,
                                sizeof(decoded), sizeof(source)),
              "zlib Adler-32 corruption is rejected");
    }
    Check(!CPV::InflateZlib(compressed, 2, decoded, sizeof(decoded),
                            sizeof(source)),
          "truncated zlib header is rejected");
    Check(!CPV::InflateZlib(compressed, compressedSize, decoded, 8,
                            sizeof(source)),
          "insufficient DEFLATE output capacity is rejected");

    const uint8_t literalBlock[] = {0x50, 'h', 'e', 'l', 'l', 'o'};
    uint8_t lz4Output[32] = {};
    Check(CPV::DecompressLZ4Block(literalBlock, sizeof(literalBlock),
                                  lz4Output, sizeof(lz4Output), 5) &&
          memcmp(lz4Output, "hello", 5) == 0,
          "LZ4 literal-only block decodes");

    const uint8_t matchBlock[] = {0x32, 'a', 'b', 'c', 0x03, 0x00};
    memset(lz4Output, 0, sizeof(lz4Output));
    Check(CPV::DecompressLZ4Block(matchBlock, sizeof(matchBlock),
                                  lz4Output, sizeof(lz4Output), 9) &&
          memcmp(lz4Output, "abcabcabc", 9) == 0,
          "LZ4 overlapping match decodes");

    const uint8_t extendedLiteral[] = {
        0xF0, 0x01, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    Check(CPV::DecompressLZ4Block(extendedLiteral, sizeof(extendedLiteral),
                                  lz4Output, sizeof(lz4Output), 16),
          "LZ4 extended literal length decodes");

    const uint8_t invalidOffset[] = {0x10, 'x', 0x00, 0x00};
    Check(!CPV::DecompressLZ4Block(invalidOffset, sizeof(invalidOffset),
                                   lz4Output, sizeof(lz4Output), 5),
          "LZ4 zero match offset is rejected");
    Check(!CPV::DecompressLZ4Block(literalBlock, sizeof(literalBlock),
                                  lz4Output, sizeof(lz4Output), 4),
          "LZ4 declared output length must match");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
