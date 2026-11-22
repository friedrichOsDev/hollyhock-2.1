#include <stdint.h>

#include <sdk/os/input.hpp>
#include <ui/framebuffer.hpp>
#include <ui/input.hpp>

// Avoid stdio.h because of the SDK's remove()/rename() declarations.
extern "C" int printf(const char *format, ...);

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char *name) {
    ++checks;
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

uint16_t pixels[8 * 12 + 2];
int lcdWidth = 8;
int lcdHeight = 12;
bool nullVram = false;
int refreshCount = 0;
InputEvent inputEvent = {};
int inputResult = 0;
uint32_t inputArg1 = 0;
uint32_t inputArg2 = 0;

void ResetPixels() {
    for (unsigned int i = 0; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        pixels[i] = 0;
}

uint16_t *VisiblePixels() {
    return pixels + 1;
}

} // namespace

extern "C" void LCD_GetSize(int *width, int *height) {
    if (width)
        *width = lcdWidth;
    if (height)
        *height = lcdHeight;
}

extern "C" uint16_t *LCD_GetVRAMAddress() {
    return nullVram ? 0 : VisiblePixels();
}

extern "C" void LCD_Refresh() {
    ++refreshCount;
}

extern "C" int GetInput(InputEvent *event, uint32_t first, uint32_t second) {
    inputArg1 = first;
    inputArg2 = second;
    if (event)
        *event = inputEvent;
    return inputResult;
}

int main() {
    ResetPixels();
    pixels[0] = 0x1234;
    pixels[sizeof(pixels) / sizeof(pixels[0]) - 1] = 0x5678;

    UI::Framebuffer screen;
    Check(!screen.IsActive() && screen.Width() == 0 && screen.Height() == 0,
          "framebuffer starts inactive");
    Check(!screen.Clear(UI::Colors::Black),
          "drawing is rejected before Begin");

    Check(UI::RGB(255, 0, 0) == 0xF800 &&
          UI::RGB(0, 255, 0) == 0x07E0 &&
          UI::RGB(0, 0, 255) == 0x001F,
          "RGB conversion uses RGB565");

    Check(screen.Begin() && screen.IsActive(),
          "Begin attaches to the LCD VRAM");
    Check(screen.Width() == 8 && screen.Height() == 12,
          "screen dimensions are retrieved dynamically");
    Check(!screen.Begin(), "duplicate Begin is rejected");

    Check(screen.Clear(UI::Colors::White), "clear active framebuffer");
    Check(VisiblePixels()[0] == UI::Colors::White &&
          VisiblePixels()[95] == UI::Colors::White,
          "clear fills the complete VRAM image");

    Check(screen.SetPixel(2, 3, UI::Colors::Red) &&
          VisiblePixels()[3 * 8 + 2] == UI::Colors::Red,
          "SetPixel writes an in-bounds pixel");
    Check(!screen.SetPixel(-1, 0, UI::Colors::Black) &&
          !screen.SetPixel(8, 0, UI::Colors::Black),
          "SetPixel rejects out-of-bounds coordinates");

    const uint16_t image[4] = {
        UI::Colors::Red, UI::Colors::Green,
        UI::Colors::Blue, UI::Colors::White
    };
    screen.Clear(UI::Colors::Black);
    Check(screen.BlitRGB565(1, 1, image, 2, 2, 2),
          "BlitRGB565 copies a scaled source image");
    Check(VisiblePixels()[1 * 8 + 1] == UI::Colors::Red &&
          VisiblePixels()[1 * 8 + 2] == UI::Colors::Red &&
          VisiblePixels()[2 * 8 + 1] == UI::Colors::Red &&
          VisiblePixels()[2 * 8 + 2] == UI::Colors::Red &&
          VisiblePixels()[1 * 8 + 3] == UI::Colors::Green &&
          VisiblePixels()[3 * 8 + 1] == UI::Colors::Blue &&
          VisiblePixels()[3 * 8 + 3] == UI::Colors::White,
          "BlitRGB565 scales and lays out pixels in row-major order");
    Check(screen.BlitRGB565(-1, -1, image, 2, 2, 2),
          "BlitRGB565 clips images at the LCD edges");
    Check(!screen.BlitRGB565(0, 0, image, 0, 2, 1) &&
          !screen.BlitRGB565(0, 0, image, 2, 2, 0) &&
          !screen.BlitRGB565(0, 0, 0, 2, 2, 1),
          "BlitRGB565 rejects invalid source dimensions and scale");

    screen.Clear(UI::Colors::Black);
    Check(screen.FillRect(-2, -1, 4, 3, UI::Colors::Green),
          "FillRect clips a partially visible rectangle");
    Check(VisiblePixels()[0] == UI::Colors::Green &&
          VisiblePixels()[1] == UI::Colors::Green &&
          VisiblePixels()[8] == UI::Colors::Green &&
          VisiblePixels()[9] == UI::Colors::Green &&
          VisiblePixels()[10] == UI::Colors::Black,
          "FillRect clips each edge correctly");
    Check(!screen.FillRect(0, 0, 0, 2, UI::Colors::Red) &&
          !screen.FillRect(0, 0, 2, -1, UI::Colors::Red),
          "FillRect rejects non-positive dimensions");

    screen.Clear(UI::Colors::Black);
    Check(screen.DrawRect(1, 1, 4, 3, UI::Colors::Yellow),
          "DrawRect draws an outline");
    Check(VisiblePixels()[1 * 8 + 1] == UI::Colors::Yellow &&
          VisiblePixels()[1 * 8 + 2] == UI::Colors::Yellow &&
          VisiblePixels()[2 * 8 + 1] == UI::Colors::Yellow &&
          VisiblePixels()[3 * 8 + 4] == UI::Colors::Yellow &&
          VisiblePixels()[2 * 8 + 2] == UI::Colors::Black,
          "DrawRect leaves the interior untouched");

    screen.Clear(UI::Colors::Black);
    Check(screen.DrawLine(-3, 1, 3, 1, UI::Colors::Blue),
          "DrawLine clips a horizontal line");
    Check(VisiblePixels()[8] == UI::Colors::Blue &&
          VisiblePixels()[9] == UI::Colors::Blue &&
          VisiblePixels()[10] == UI::Colors::Blue &&
          VisiblePixels()[11] == UI::Colors::Blue,
          "clipped line covers visible pixels");
    Check(screen.DrawLine(-16383, -16383, 16383, 16383,
                          UI::Colors::Red),
          "DrawLine handles maximum supported coordinates");
    Check(!screen.DrawLine(16384, 0, 0, 0, UI::Colors::Red),
          "DrawLine rejects pathological coordinates");

    screen.Clear(UI::Colors::Black);
    Check(screen.DrawChar(1, 1, 'A', UI::Colors::White),
          "DrawChar accepts printable glyphs");
    Check(VisiblePixels()[1 * 8 + 2] == UI::Colors::White &&
          VisiblePixels()[1 * 8 + 3] == UI::Colors::White &&
          VisiblePixels()[1 * 8 + 4] == UI::Colors::White,
          "DrawChar renders the expected 5x7 glyph");
    Check(screen.DrawChar(1, 1, 'a', UI::Colors::White),
          "lowercase glyphs are supported");

    screen.Clear(UI::Colors::Black);
    Check(screen.DrawText(0, 0, "Hi\nUI", UI::Colors::White),
          "DrawText supports text and newlines");
    Check(VisiblePixels()[0] != UI::Colors::Black &&
          VisiblePixels()[8 * 8] != UI::Colors::Black,
          "DrawText renders each line");
    Check(!screen.DrawText(0, 0, 0, UI::Colors::White),
          "DrawText rejects a null string");

    char longText[514];
    for (int i = 0; i < 513; ++i)
        longText[i] = 'A';
    longText[513] = 0;
    longText[512] = 0;
    Check(screen.DrawText(0, 0, longText, UI::Colors::White),
          "DrawText accepts exactly 512 bytes");
    longText[512] = 'B';
    Check(!screen.DrawText(0, 0, longText, UI::Colors::White),
          "DrawText enforces its 512-byte limit");

    Check(screen.Present() && refreshCount == 1,
          "Present refreshes the LCD");
    Check(pixels[0] == 0x1234 &&
          pixels[sizeof(pixels) / sizeof(pixels[0]) - 1] == 0x5678,
          "drawing never modifies VRAM guard words");

    screen.End();
    Check(!screen.IsActive() && screen.Width() == 0 &&
          !screen.Present() && !screen.SetPixel(0, 0, UI::Colors::White),
          "End detaches the framebuffer safely");

    lcdWidth = 0;
    Check(!screen.Begin(), "Begin rejects invalid LCD dimensions");
    lcdWidth = 2048;
    lcdHeight = 1024;
    Check(!screen.Begin(), "Begin rejects excessive framebuffer dimensions");
    lcdWidth = 8;
    lcdHeight = 12;
    nullVram = true;
    Check(!screen.Begin(), "Begin rejects a null VRAM pointer");
    nullVram = false;

    UI::Event event;
    Check(UI::Input::Poll(0) == -1, "Input rejects a null event pointer");

    inputEvent = {};
    inputResult = 0;
    Check(UI::Input::Poll(&event) == 0 && event.type == UI::EventNone,
          "Input reports an empty event queue");

    inputEvent = {};
    inputEvent.type = EVENT_KEY;
    inputEvent.data.key.keyCode = KEYCODE_EXE;
    inputEvent.data.key.direction = KEY_PRESSED;
    Check(UI::Input::Poll(&event) == 1 &&
          event.type == UI::EventKey &&
          event.keyCode == UI::KeyEXE &&
          event.keyAction == UI::KeyPressed &&
          event.rawType == EVENT_KEY,
          "Input maps pressed key events");
    Check(inputArg1 == 0xFFFFFFFFU && inputArg2 == 0x10U,
          "Input uses the documented SDK parameters");

    inputEvent = {};
    inputEvent.type = EVENT_KEY;
    inputEvent.data.key.keyCode = KEYCODE_UP;
    inputEvent.data.key.direction = KEY_HELD;
    Check(UI::Input::Poll(&event) == 1 &&
          event.keyCode == UI::KeyUp &&
          event.keyAction == UI::KeyHeld,
          "Input maps held key events");

    inputEvent = {};
    inputEvent.type = EVENT_TOUCH;
    inputEvent.data.touch_single.direction = TOUCH_DOWN;
    inputEvent.data.touch_single.p1_x = -4;
    inputEvent.data.touch_single.p1_y = 123;
    Check(UI::Input::Poll(&event) == 1 &&
          event.type == UI::EventTouch &&
          event.touchAction == UI::TouchDown &&
          event.x == -4 && event.y == 123,
          "Input maps touch coordinates without clipping");

    inputEvent = {};
    inputEvent.type = EVENT_TOUCH;
    inputEvent.data.touch_single.direction = TOUCH_HOLD_DRAG;
    Check(UI::Input::Poll(&event) == 1 &&
          event.touchAction == UI::TouchDrag,
          "Input maps touch drag events");

    inputEvent = {};
    inputEvent.type = EVENT_ACTBAR_ESC;
    Check(UI::Input::Poll(&event) == 1 &&
          event.type == UI::EventSystem &&
          event.systemEvent == UI::SystemActionBarEscape,
          "Input maps system action-bar events");

    inputEvent = {};
    inputEvent.type = 0x7777;
    Check(UI::Input::Poll(&event) == 1 &&
          event.type == UI::EventUnknown &&
          event.rawType == 0x7777,
          "Input preserves unknown event types");

    inputEvent = {};
    inputEvent.type = EVENT_TIMER;
    Check(UI::Input::Poll(&event) == 1 &&
          event.type == UI::EventTimer &&
          event.rawType == EVENT_TIMER,
          "Input maps the SDK timer event");

    inputResult = -1;
    Check(UI::Input::Poll(&event) == -2 && event.type == UI::EventNone,
          "Input resets output when the SDK call fails");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
