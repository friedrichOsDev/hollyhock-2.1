#ifndef CPV_PLAYBACK_EVENTS_HPP
#define CPV_PLAYBACK_EVENTS_HPP

#include <stdint.h>

namespace CPV {

struct UInt64Pair {
    uint32_t high;
    uint32_t low;
};

// Multiply a 32-bit value by a 16-bit value without 64-bit arithmetic.
// The exact product is returned as two 32-bit words.
static inline UInt64Pair MultiplyU32ByU16(uint32_t value,
                                          uint16_t multiplier) {
    const uint32_t lowProduct =
        (value & 0xFFFFU) * static_cast<uint32_t>(multiplier);
    const uint32_t highProduct =
        (value >> 16) * static_cast<uint32_t>(multiplier);

    const uint32_t shiftedHigh = highProduct << 16;
    const uint32_t low = lowProduct + shiftedHigh;
    const uint32_t carry = low < lowProduct ? 1U : 0U;

    UInt64Pair result = {
        (highProduct >> 16) + carry,
        low
    };
    return result;
}

// Add a 32-bit value to a two-word unsigned integer.
static inline UInt64Pair AddU32(UInt64Pair value, uint32_t addend) {
    const uint32_t oldLow = value.low;
    value.low += addend;
    if (value.low < oldLow)
        ++value.high;
    return value;
}

// Divide a two-word unsigned integer by a 32-bit denominator.
// Uses restoring long division with a 32-bit remainder and an explicit
// carry bit, avoiding __udivdi3 and all 64-bit arithmetic operators.
// The quotient saturates if it needs more than 32 bits.
static inline uint32_t DivideU64PairByU32Saturating(UInt64Pair numerator,
                                                     uint32_t denominator) {
    if (denominator == 0)
        return 0xFFFFFFFFU;

    uint32_t remainder = 0;
    uint32_t quotient = 0;

    for (int bit = 63; bit >= 0; --bit) {
        const uint32_t inputBit = bit >= 32
            ? ((numerator.high >> (bit - 32)) & 1U)
            : ((numerator.low >> bit) & 1U);
        const bool carry = (remainder & 0x80000000U) != 0;
        remainder = (remainder << 1) | inputBit;

        if (carry || remainder >= denominator) {
            // With carry set, this subtraction is modulo 2^32 and equals
            // (2^32 + remainder) - denominator, which is < denominator.
            remainder -= denominator;
            if (bit >= 32)
                return 0xFFFFFFFFU;
            quotient |= 1U << bit;
        }
    }
    return quotient;
}

// Convert stored frame duration into nominal source-frame intervals.
// This is media-metadata arithmetic only; it does not read a hardware clock
// and deliberately contains no 64-bit multiply or divide operations.
static inline uint32_t EventStepsForFrameDuration(uint32_t durationMs,
                                                  uint16_t fpsNumerator,
                                                  uint16_t fpsDenominator) {
    if (durationMs == 0 || fpsNumerator == 0 || fpsDenominator == 0)
        return 1U;

    const UInt64Pair product =
        MultiplyU32ByU16(durationMs, fpsNumerator);
    const uint32_t denominator =
        1000U * static_cast<uint32_t>(fpsDenominator);
    const UInt64Pair rounded =
        AddU32(product, denominator / 2U);

    uint32_t steps =
        DivideU64PairByU32Saturating(rounded, denominator);
    if (steps == 0)
        steps = 1U;
    return steps;
}

// Consume one logical step in response to one SDK timer event.
// Returns true only when the countdown was actually decremented.
static inline bool ConsumePlaybackEventStep(uint32_t *remainingEvents,
                                            bool paused, bool ended) {
    if (!remainingEvents || paused || ended || *remainingEvents == 0)
        return false;
    --(*remainingEvents);
    return true;
}

} // namespace CPV

#endif
