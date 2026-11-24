#include <sdk/os/input.hpp>
#include <ui/input.hpp>

namespace UI {
namespace {

KeyAction ConvertKeyAction(uint32_t direction) {
    if (direction == KEY_PRESSED)
        return KeyPressed;
    if (direction == KEY_HELD)
        return KeyHeld;
    if (direction == KEY_RELEASED)
        return KeyReleased;
    return KeyActionUnknown;
}

TouchAction ConvertTouchAction(uint32_t direction) {
    if (direction == TOUCH_DOWN)
        return TouchDown;
    if (direction == TOUCH_HOLD_DRAG)
        return TouchDrag;
    if (direction == TOUCH_UP)
        return TouchUp;
    if (direction == TOUCH_ACT_BAR)
        return TouchActionBar;
    return TouchActionUnknown;
}

SystemEvent ConvertSystemEvent(uint16_t type) {
    if (type == EVENT_ACTBAR_RESIZE)
        return SystemActionBarResize;
    if (type == EVENT_ACTBAR_SWAP)
        return SystemActionBarSwap;
    if (type == EVENT_ACTBAR_ROTATE)
        return SystemActionBarRotate;
    if (type == EVENT_ACTBAR_ESC)
        return SystemActionBarEscape;
    if (type == EVENT_ACTBAR_SETTINGS)
        return SystemActionBarSettings;
    return SystemEventUnknown;
}

} // namespace

int Input::Poll(Event *event) {
    if (!event)
        return -1;

    *event = Event();

    InputEvent raw = {};
    const int result = GetInput(&raw, 0xFFFFFFFFU, 0x10U);
    if (result != 0)
        return -2;

    if (raw.type == 0)
        return 0;

    event->rawType = raw.type;

    // The SDK emits this system event as the application's periodic timer tick.
    // It has no payload in InputEvent; only rawType is meaningful.
    if (raw.type == EVENT_TIMER) {
        event->type = EventTimer;
        return 1;
    }

    if (raw.type == EVENT_KEY) {
        event->type = EventKey;
        event->keyCode = raw.data.key.keyCode;
        event->keyAction = ConvertKeyAction(raw.data.key.direction);
        event->rawAction = raw.data.key.direction;
        return 1;
    }

    if (raw.type == EVENT_TOUCH) {
        event->type = EventTouch;
        event->x = raw.data.touch_single.p1_x;
        event->y = raw.data.touch_single.p1_y;
        event->touchAction =
            ConvertTouchAction(raw.data.touch_single.direction);
        event->rawAction = raw.data.touch_single.direction;
        return 1;
    }

    const SystemEvent systemEvent = ConvertSystemEvent(raw.type);
    if (systemEvent != SystemEventUnknown) {
        event->type = EventSystem;
        event->systemEvent = systemEvent;
        return 1;
    }

    event->type = EventUnknown;
    return 1;
}

} // namespace UI
