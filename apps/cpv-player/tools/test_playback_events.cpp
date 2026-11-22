#include "../cpv_playback_events.hpp"

#include <stdio.h>

namespace {
unsigned checks = 0;
unsigned failures = 0;

void Check(bool condition, const char *message) {
    ++checks;
    if (condition) {
        printf("PASS: %s\n", message);
    } else {
        ++failures;
        printf("FAIL: %s\n", message);
    }
}
}

int main() {
    Check(CPV::EventStepsForFrameDuration(250, 4, 1) == 1,
          "one 250 ms frame at 4 FPS needs one event");
    Check(CPV::EventStepsForFrameDuration(750, 4, 1) == 3,
          "coalesced 750 ms frame at 4 FPS needs three events");
    Check(CPV::EventStepsForFrameDuration(1000, 4, 1) == 4,
          "one second at 4 FPS needs four events");
    Check(CPV::EventStepsForFrameDuration(500, 8, 1) == 4,
          "duration conversion respects the CPV file FPS");
    Check(CPV::EventStepsForFrameDuration(417, 24, 1) == 10,
          "fractional source periods round to the nearest event count");
    Check(CPV::EventStepsForFrameDuration(1, 4, 1) == 1,
          "sub-frame durations still get at least one event");
    Check(CPV::EventStepsForFrameDuration(250, 0, 1) == 1 &&
          CPV::EventStepsForFrameDuration(250, 4, 0) == 1,
          "invalid FPS metadata falls back to one event");

    Check(CPV::EventStepsForFrameDuration(0xFFFFFFFFU, 30, 1) ==
              128849019U,
          "large valid duration converts without 64-bit runtime arithmetic");

    const CPV::UInt64Pair twoTo32 = {1U, 0U};
    Check(CPV::DivideU64PairByU32Saturating(twoTo32, 2U) ==
              0x80000000U,
          "pair division handles a quotient at the signed boundary");

    const CPV::UInt64Pair twoTo33 = {2U, 0U};
    Check(CPV::DivideU64PairByU32Saturating(twoTo33, 1U) ==
              0xFFFFFFFFU,
          "pair division saturates a quotient wider than 32 bits");

    const CPV::UInt64Pair ordinary = {0U, 123456789U};
    Check(CPV::DivideU64PairByU32Saturating(ordinary, 1000U) == 123456U,
          "pair division computes an ordinary quotient");

    const CPV::UInt64Pair carryCase = {0x80000000U, 0U};
    Check(CPV::DivideU64PairByU32Saturating(carryCase, 0xFFFFFFFFU) ==
              0x80000000U,
          "pair division handles a high-bit remainder without overflow");

    const CPV::UInt64Pair product =
        CPV::MultiplyU32ByU16(0xFFFFFFFFU, 30U);
    Check(product.high == 29U && product.low == 0xFFFFFFE2U,
          "32-by-16 multiplication returns exact high and low words");

    uint32_t remaining = 3;
    Check(CPV::ConsumePlaybackEventStep(&remaining, false, false) &&
          remaining == 2,
          "one event decrements the playback countdown exactly once");
    Check(!CPV::ConsumePlaybackEventStep(&remaining, true, false) &&
          remaining == 2,
          "paused playback does not consume timer steps");
    Check(!CPV::ConsumePlaybackEventStep(&remaining, false, true) &&
          remaining == 2,
          "ended playback does not consume timer steps");
    Check(CPV::ConsumePlaybackEventStep(&remaining, false, false) &&
          remaining == 1,
          "the next timer event consumes one more step");
    Check(CPV::ConsumePlaybackEventStep(&remaining, false, false) &&
          remaining == 0,
          "the countdown reaches zero after three events");
    Check(!CPV::ConsumePlaybackEventStep(&remaining, false, false) &&
          remaining == 0,
          "a zero countdown cannot underflow");

    printf("\n%u checks, %u failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
