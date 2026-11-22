#ifndef CPV_PLAYBACK_EVENTS_HPP
#define CPV_PLAYBACK_EVENTS_HPP

#include <stdint.h>

namespace CPV {

// Convert stored frame duration into nominal source-frame intervals.
// This is media-metadata arithmetic only; it does not read a hardware clock.
static inline uint32_t EventStepsForFrameDuration(uint32_t durationMs,
                                                  uint16_t fpsNumerator,
                                                  uint16_t fpsDenominator) {
    if (durationMs == 0 || fpsNumerator == 0 || fpsDenominator == 0)
        return 1U;

    const uint64_t numerator =
        static_cast<uint64_t>(durationMs) * fpsNumerator;
    const uint64_t denominator =
        static_cast<uint64_t>(1000U) * fpsDenominator;
    uint64_t steps = (numerator + denominator / 2U) / denominator;

    if (steps == 0)
        steps = 1U;
    if (steps > 0xFFFFFFFFULL)
        steps = 0xFFFFFFFFULL;
    return static_cast<uint32_t>(steps);
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
