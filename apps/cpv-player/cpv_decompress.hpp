#ifndef CPV_DECOMPRESS_HPP
#define CPV_DECOMPRESS_HPP

#include <stddef.h>
#include <stdint.h>

/*
 * Bounded, allocation-free decoders for independent CPV frame streams.
 * DEFLATE input is a zlib-wrapped stream; LZ4 input is one raw LZ4 block.
 */
namespace CPV {
namespace Detail {

struct BitReader {
    const uint8_t *input;
    uint32_t size;
    uint32_t position;
    uint32_t bits;
    unsigned bitCount;

    BitReader(const uint8_t *data, uint32_t length)
        : input(data), size(length), position(0), bits(0), bitCount(0) {}

    bool Read(unsigned count, uint32_t *value) {
        if (!value || count > 24)
            return false;
        while (bitCount < count) {
            if (position >= size)
                return false;
            bits |= static_cast<uint32_t>(input[position++]) << bitCount;
            bitCount += 8;
        }
        *value = bits & ((1U << count) - 1U);
        bits >>= count;
        bitCount -= count;
        return true;
    }

    void AlignByte() {
        bits = 0;
        bitCount = 0;
    }
};

struct Huffman {
    uint16_t count[16];
    uint16_t symbols[288];
    uint16_t symbolCount;

    Huffman() : symbolCount(0) {
        for (unsigned i = 0; i < 16; ++i)
            count[i] = 0;
    }

    bool Build(const uint8_t *lengths, unsigned number) {
        if (!lengths || number > 288)
            return false;
        for (unsigned i = 0; i < 16; ++i)
            count[i] = 0;
        symbolCount = 0;

        for (unsigned i = 0; i < number; ++i) {
            if (lengths[i] > 15)
                return false;
            ++count[lengths[i]];
        }
        count[0] = 0;

        int left = 1;
        for (unsigned bits = 1; bits <= 15; ++bits) {
            left <<= 1;
            left -= count[bits];
            if (left < 0)
                return false;
        }

        uint16_t offsets[16];
        offsets[1] = 0;
        for (unsigned bits = 1; bits < 15; ++bits)
            offsets[bits + 1] =
                static_cast<uint16_t>(offsets[bits] + count[bits]);

        for (unsigned symbol = 0; symbol < number; ++symbol) {
            const unsigned length = lengths[symbol];
            if (length != 0)
                symbols[offsets[length]++] = static_cast<uint16_t>(symbol);
        }
        for (unsigned bits = 1; bits <= 15; ++bits)
            symbolCount = static_cast<uint16_t>(symbolCount + count[bits]);
        return true;
    }

    int Decode(BitReader *reader) const {
        if (!reader || symbolCount == 0)
            return -1;
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= 15; ++length) {
            uint32_t bit = 0;
            if (!reader->Read(1, &bit))
                return -1;
            code |= static_cast<int>(bit);
            const int amount = count[length];
            if (code - amount < first) {
                const int result = index + code - first;
                if (result < 0 || result >= symbolCount)
                    return -1;
                return symbols[result];
            }
            index += amount;
            first += amount;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

static inline bool ReadBits(BitReader *reader, unsigned count,
                            uint32_t *value) {
    return reader && reader->Read(count, value);
}

static inline bool BuildFixed(Huffman *literalLength, Huffman *distance) {
    uint8_t literalLengths[288];
    uint8_t distanceLengths[32];
    for (unsigned i = 0; i <= 143; ++i) literalLengths[i] = 8;
    for (unsigned i = 144; i <= 255; ++i) literalLengths[i] = 9;
    for (unsigned i = 256; i <= 279; ++i) literalLengths[i] = 7;
    for (unsigned i = 280; i <= 287; ++i) literalLengths[i] = 8;
    for (unsigned i = 0; i < 32; ++i) distanceLengths[i] = 5;
    return literalLength->Build(literalLengths, 288) &&
           distance->Build(distanceLengths, 32);
}

static inline bool BuildDynamic(BitReader *reader, Huffman *literalLength,
                                Huffman *distance) {
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint32_t value = 0;
    if (!ReadBits(reader, 5, &value)) return false;
    const unsigned literalCount = value + 257U;
    if (!ReadBits(reader, 5, &value)) return false;
    const unsigned distanceCount = value + 1U;
    if (!ReadBits(reader, 4, &value)) return false;
    const unsigned codeLengthCount = value + 4U;
    if (literalCount > 286 || distanceCount > 32)
        return false;

    uint8_t codeLengthLengths[19] = {};
    for (unsigned i = 0; i < codeLengthCount; ++i) {
        if (!ReadBits(reader, 3, &value))
            return false;
        codeLengthLengths[order[i]] = static_cast<uint8_t>(value);
    }
    Huffman codeLengthTree;
    if (!codeLengthTree.Build(codeLengthLengths, 19))
        return false;

    uint8_t lengths[318] = {};
    const unsigned total = literalCount + distanceCount;
    unsigned position = 0;
    while (position < total) {
        const int symbol = codeLengthTree.Decode(reader);
        if (symbol < 0)
            return false;
        if (symbol <= 15) {
            lengths[position++] = static_cast<uint8_t>(symbol);
        } else if (symbol == 16) {
            if (position == 0 || !ReadBits(reader, 2, &value))
                return false;
            const unsigned repeat = value + 3U;
            if (repeat > total - position)
                return false;
            const uint8_t previous = lengths[position - 1];
            for (unsigned i = 0; i < repeat; ++i)
                lengths[position++] = previous;
        } else if (symbol == 17) {
            if (!ReadBits(reader, 3, &value))
                return false;
            const unsigned repeat = value + 3U;
            if (repeat > total - position)
                return false;
            for (unsigned i = 0; i < repeat; ++i)
                lengths[position++] = 0;
        } else if (symbol == 18) {
            if (!ReadBits(reader, 7, &value))
                return false;
            const unsigned repeat = value + 11U;
            if (repeat > total - position)
                return false;
            for (unsigned i = 0; i < repeat; ++i)
                lengths[position++] = 0;
        } else {
            return false;
        }
    }
    if (lengths[256] == 0)
        return false;
    return literalLength->Build(lengths, literalCount) &&
           distance->Build(lengths + literalCount, distanceCount);
}

static inline bool InflateBlocks(const uint8_t *input, uint32_t inputSize,
                                 uint8_t *output, uint32_t outputCapacity,
                                 uint32_t *outputSize, uint32_t *consumed) {
    BitReader reader(input, inputSize);
    uint32_t written = 0;
    bool finalBlock = false;

    static const uint16_t lengthBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t lengthExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t distanceBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,
        8193,12289,16385,24577
    };
    static const uint8_t distanceExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    while (!finalBlock) {
        uint32_t finalValue = 0;
        uint32_t type = 0;
        if (!ReadBits(&reader, 1, &finalValue) ||
            !ReadBits(&reader, 2, &type))
            return false;
        finalBlock = finalValue != 0;

        if (type == 0) {
            reader.AlignByte();
            if (reader.position > reader.size ||
                reader.size - reader.position < 4)
                return false;
            const uint32_t length =
                static_cast<uint32_t>(reader.input[reader.position]) |
                (static_cast<uint32_t>(reader.input[reader.position + 1]) << 8);
            const uint32_t complement =
                static_cast<uint32_t>(reader.input[reader.position + 2]) |
                (static_cast<uint32_t>(reader.input[reader.position + 3]) << 8);
            reader.position += 4;
            if ((length ^ 0xFFFFU) != complement ||
                length > reader.size - reader.position ||
                length > outputCapacity - written)
                return false;
            for (uint32_t i = 0; i < length; ++i)
                output[written++] = reader.input[reader.position++];
            continue;
        }
        if (type == 3)
            return false;

        Huffman literalLength;
        Huffman distance;
        const bool treesValid = type == 1
            ? BuildFixed(&literalLength, &distance)
            : BuildDynamic(&reader, &literalLength, &distance);
        if (!treesValid)
            return false;

        for (;;) {
            const int symbol = literalLength.Decode(&reader);
            if (symbol < 0)
                return false;
            if (symbol < 256) {
                if (written >= outputCapacity)
                    return false;
                output[written++] = static_cast<uint8_t>(symbol);
                continue;
            }
            if (symbol == 256)
                break;
            if (symbol < 257 || symbol > 285)
                return false;

            const unsigned lengthIndex = static_cast<unsigned>(symbol - 257);
            uint32_t extra = 0;
            if (!ReadBits(&reader, lengthExtra[lengthIndex], &extra))
                return false;
            const uint32_t matchLength =
                static_cast<uint32_t>(lengthBase[lengthIndex]) + extra;

            const int distanceSymbol = distance.Decode(&reader);
            if (distanceSymbol < 0 || distanceSymbol >= 30)
                return false;
            if (!ReadBits(&reader, distanceExtra[distanceSymbol], &extra))
                return false;
            const uint32_t back =
                static_cast<uint32_t>(distanceBase[distanceSymbol]) + extra;
            if (back == 0 || back > written ||
                matchLength > outputCapacity - written)
                return false;
            for (uint32_t i = 0; i < matchLength; ++i) {
                const uint8_t value = output[written - back];
                output[written++] = value;
            }
        }
    }

    if (reader.position != reader.size)
        return false;
    *outputSize = written;
    *consumed = reader.position;
    return true;
}

static inline uint32_t Adler32(const uint8_t *data, uint32_t size) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint32_t i = 0; i < size; ++i) {
        a += data[i];
        if (a >= 65521U) a -= 65521U;
        b += a;
        if (b >= 65521U) b -= 65521U;
    }
    return (b << 16) | a;
}

} // namespace Detail

static inline bool InflateZlib(const uint8_t *input, uint32_t inputSize,
                               uint8_t *output, uint32_t outputCapacity,
                               uint32_t expectedSize) {
    if (!input || !output || inputSize < 6 ||
        expectedSize > outputCapacity)
        return false;
    const uint8_t cmf = input[0];
    const uint8_t flg = input[1];
    if ((cmf & 0x0FU) != 8U || (cmf >> 4) > 7U ||
        ((static_cast<unsigned>(cmf) << 8) | flg) % 31U != 0U ||
        (flg & 0x20U) != 0U)
        return false;

    const uint32_t deflateSize = inputSize - 6U;
    uint32_t outputSize = 0;
    uint32_t consumed = 0;
    if (!Detail::InflateBlocks(input + 2, deflateSize, output,
                               outputCapacity, &outputSize, &consumed))
        return false;
    if (consumed != deflateSize || outputSize != expectedSize)
        return false;

    const uint32_t storedAdler =
        (static_cast<uint32_t>(input[inputSize - 4]) << 24) |
        (static_cast<uint32_t>(input[inputSize - 3]) << 16) |
        (static_cast<uint32_t>(input[inputSize - 2]) << 8) |
        static_cast<uint32_t>(input[inputSize - 1]);
    return Detail::Adler32(output, outputSize) == storedAdler;
}

static inline bool DecompressLZ4Block(const uint8_t *input,
                                      uint32_t inputSize,
                                      uint8_t *output,
                                      uint32_t outputCapacity,
                                      uint32_t expectedSize) {
    if ((!input && inputSize) || !output || expectedSize > outputCapacity)
        return false;
    uint32_t in = 0;
    uint32_t out = 0;
    while (in < inputSize) {
        const uint8_t token = input[in++];
        uint32_t literalLength = token >> 4;
        if (literalLength == 15) {
            uint8_t extension = 255;
            while (extension == 255) {
                if (in >= inputSize)
                    return false;
                extension = input[in++];
                if (literalLength > 0xFFFFFFFFU - extension)
                    return false;
                literalLength += extension;
            }
        }
        if (literalLength > inputSize - in ||
            literalLength > outputCapacity - out)
            return false;
        for (uint32_t i = 0; i < literalLength; ++i)
            output[out++] = input[in++];

        if (in == inputSize)
            break;
        if (inputSize - in < 2)
            return false;
        const uint32_t offset =
            static_cast<uint32_t>(input[in]) |
            (static_cast<uint32_t>(input[in + 1]) << 8);
        in += 2;
        if (offset == 0 || offset > out)
            return false;

        uint32_t matchLength = (token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15U) {
            uint8_t extension = 255;
            while (extension == 255) {
                if (in >= inputSize)
                    return false;
                extension = input[in++];
                if (matchLength > 0xFFFFFFFFU - extension)
                    return false;
                matchLength += extension;
            }
        }
        if (matchLength > outputCapacity - out)
            return false;
        for (uint32_t i = 0; i < matchLength; ++i) {
            const uint8_t value = output[out - offset];
            output[out++] = value;
        }
    }
    return in == inputSize && out == expectedSize;
}

} // namespace CPV

#endif
