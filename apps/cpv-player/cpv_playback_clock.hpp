#ifndef CPV_PLAYBACK_CLOCK_HPP
#define CPV_PLAYBACK_CLOCK_HPP
#include <stdint.h>
namespace CPV {
class EventPlaybackClock {
public:
    EventPlaybackClock() : m_timerEvents(0), m_measurementSeconds(0),
        m_eventBudget(0), m_credit(0), m_frameCost(0),
        m_configured(false), m_frameActive(false) {}
    bool Configure(uint32_t timerEvents, uint32_t measurementSeconds) {
        if (!timerEvents || timerEvents > 10000U ||
            !measurementSeconds || measurementSeconds > 60U)
            return false;
        m_timerEvents=timerEvents; m_measurementSeconds=measurementSeconds;
        m_eventBudget=measurementSeconds*1000U; m_credit=0; m_frameCost=0;
        m_configured=true; m_frameActive=false; return true;
    }
    void ResetFrame(uint32_t durationMs) {
        m_credit=0; SetNextFrameDuration(durationMs);
    }
    void SetNextFrameDuration(uint32_t durationMs) {
        if (!m_configured) { m_frameCost=0xFFFFFFFFU; m_frameActive=false; return; }
        if (!durationMs) durationMs = 1U;
        uint32_t multiplier = m_timerEvents;
        uint32_t multiplicand = durationMs;
        uint32_t product = 0;
        while (multiplier) {
            if (multiplier & 1U) {
                if (product > 0xFFFFFFFFU - multiplicand) {
                    product = 0xFFFFFFFFU;
                    break;
                }
                product += multiplicand;
            }
            multiplier >>= 1;
            if (multiplier) {
                if (multiplicand > 0x7FFFFFFFU)
                    multiplicand = 0xFFFFFFFFU;
                else
                    multiplicand <<= 1;
            }
        }
        m_frameCost = product ? product : 1U;
        m_frameActive=true;
    }
    void AddTimerEvent() {
        if (!m_configured) return;
        if (m_credit > 0xFFFFFFFFU-m_eventBudget) m_credit=0xFFFFFFFFU;
        else m_credit+=m_eventBudget;
    }
    bool FrameDue() const {
        return m_configured && m_frameActive && m_credit>=m_frameCost;
    }
    bool ConsumeCurrentFrame() {
        if (!FrameDue()) return false;
        m_credit-=m_frameCost; m_frameActive=false; return true;
    }
    void ClearCredit() { m_credit=0; }
    uint32_t Credit() const { return m_credit; }
    uint32_t TimerEvents() const { return m_timerEvents; }
    uint32_t MeasurementSeconds() const { return m_measurementSeconds; }
    uint32_t EventBudget() const { return m_eventBudget; }
private:
    uint32_t m_timerEvents,m_measurementSeconds,m_eventBudget;
    uint32_t m_credit,m_frameCost;
    bool m_configured,m_frameActive;
};
}
#endif
