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
