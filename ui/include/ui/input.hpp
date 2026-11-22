#pragma once

#include <stdint.h>

namespace UI {

enum EventType {
    EventNone = 0,
    EventKey,
    EventTouch,
    EventSystem,
    EventUnknown,
    EventTimer
};

enum KeyAction {
    KeyActionUnknown = 0,
    KeyPressed,
    KeyHeld,
    KeyReleased
};

enum TouchAction {
    TouchActionUnknown = 0,
    TouchDown,
    TouchDrag,
    TouchUp,
    TouchActionBar
};

// Key codes are the stable values reported by the fx-CP400 input system.
enum KeyCode : uint16_t {
    KeyKeyboard = 0x00B3,
    KeyShift = 0x00A0,
    KeyBackspace = 0x0097,
    KeyPowerClear = 0x0080,
    KeyUp = 0x0090,
    KeyDown = 0x0091,
    KeyLeft = 0x0092,
    KeyRight = 0x0093,
    KeyEquals = 0x003D,
    KeyX = 0xEDB8,
    KeyY = 0xEDB9,
    KeyZ = 0xEDBA,
    KeyPower = 0x005E,
    KeyDivide = 0x002F,
    KeyOpenParenthesis = 0x0028,
    Key7 = 0x0037,
    Key8 = 0x0038,
    Key9 = 0x0039,
    KeyTimes = 0xEE10,
    KeyCloseParenthesis = 0x0029,
    Key4 = 0x0034,
    Key5 = 0x0035,
    Key6 = 0x0036,
    KeyMinus = 0x002D,
    KeyComma = 0x002C,
    Key1 = 0x0031,
    Key2 = 0x0032,
    Key3 = 0x0033,
    KeyPlus = 0x002B,
    KeyNegative = 0x001F,
    Key0 = 0x0030,
    KeyDot = 0x002E,
    KeyExp = 0x001D,
    KeyEXE = 0x0094
};

enum SystemEvent {
    SystemEventUnknown = 0,
    SystemActionBarResize,
    SystemActionBarSwap,
    SystemActionBarRotate,
    SystemActionBarEscape,
    SystemActionBarSettings
};

struct Event {
    EventType type;
    uint16_t rawType;

    uint16_t keyCode;
    KeyAction keyAction;

    int32_t x;
    int32_t y;
    TouchAction touchAction;

    SystemEvent systemEvent;

    // Original SDK direction value. Useful when the SDK reports a value
    // not yet represented by one of the normalized action enums.
    uint32_t rawAction;

    Event()
        : type(EventNone),
          rawType(0),
          keyCode(0),
          keyAction(KeyActionUnknown),
          x(0),
          y(0),
          touchAction(TouchActionUnknown),
          systemEvent(SystemEventUnknown),
          rawAction(0) {}
};

class Input {
public:
    // Polls one SDK event.
    // Returns 1 when an event was received, 0 when no event is available,
    // -1 for an invalid argument, or -2 when the SDK call fails.
    static int Poll(Event *event);
};

} // namespace UI
