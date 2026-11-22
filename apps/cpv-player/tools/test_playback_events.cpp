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

    CPV::EventPlaybackClock clock;
    Check(clock.Configure(20U,10U),"20 timer events over 10 seconds configure");
    clock.ResetFrame(250U); clock.AddTimerEvent(); unsigned frames=0;
    while(clock.FrameDue() && frames<8U) {
        Check(clock.ConsumeCurrentFrame(),"clock consumes due frame");
        ++frames; clock.SetNextFrameDuration(250U);
    }
    Check(frames==2U && clock.Credit()==0U,
          "2 events/s schedule 2 frames/event at 4 FPS");
    Check(clock.Configure(25U,10U),"fractional timer rate configures");
    clock.ResetFrame(250U);
    const uint32_t expected[5]={1U,2U,1U,2U,2U}; bool fractional=true;
    for(unsigned tick=0;tick<5U;++tick) {
        clock.AddTimerEvent(); frames=0;
        while(clock.FrameDue() && frames<8U) {
            if(!clock.ConsumeCurrentFrame()){fractional=false;break;}
            ++frames; clock.SetNextFrameDuration(250U);
        }
        if(frames!=expected[tick]) fractional=false;
    }
    Check(fractional,"fractional frame/event ratio has no accumulated rounding drift");
    Check(clock.Configure(40U,10U),"4 events/s calibration configures");
    clock.ResetFrame(250U); clock.AddTimerEvent(); frames=0;
    while(clock.FrameDue() && frames<8U) {
        clock.ConsumeCurrentFrame(); ++frames; clock.SetNextFrameDuration(250U);
    }
    Check(frames==1U,"4 events/s schedules one 250 ms frame per event");
    Check(clock.Configure(20U,10U),"coalesced-frame calibration configures");
    clock.ResetFrame(750U); clock.AddTimerEvent();
    Check(!clock.FrameDue(),"750 ms frame is not advanced early");
    clock.AddTimerEvent();
    Check(clock.FrameDue() && clock.ConsumeCurrentFrame(),
          "750 ms frame advances only after full media duration");
    Check(clock.Credit()==5000U,"coalesced frame preserves fractional credit");
    clock.ResetFrame(250U);
    Check(clock.Credit()==0U,"seek reset discards previous fractional credit");
    Check(!clock.Configure(0U,10U) && !clock.Configure(20U,0U) &&
          !clock.Configure(20U,61U),"invalid calibration values are rejected");
    printf("\n%u checks, %u failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
