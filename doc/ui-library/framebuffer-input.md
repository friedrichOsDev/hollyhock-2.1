# Framebuffer and Input API

The UI Library exposes a direct-to-VRAM drawing API and a normalized event API. Together they let an application own its entire screen without using Debug_Printf() or built-in GUI dialogs.

~~~cpp
#include <ui/ui.hpp>
~~~

## Display lifecycle

The SDK display must be initialized before a Framebuffer is attached. With the default Application lifecycle, create it inside an application callback after Application::Run() has called calcInit(). This includes onStart, onFileSelected, onCustomMainMenu, onCustomFileSelector, and onCustomSettings.

~~~cpp
static void Start(UI::Application &app) {
    (void)app;
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;

    screen.Clear(UI::Colors::White);
    screen.DrawText(8, 8, "Hello from the framebuffer",
                    UI::Colors::Black);
    screen.Present();
}
~~~

Framebuffer does not call calcInit(), calcEnd(), LCD_VRAMBackup(), or LCD_VRAMRestore(). It does not own VRAM or allocate a secondary image buffer. End() only detaches its pointer. Application remains responsible for restoring the display on exit.

Create Framebuffer at runtime, not as a global non-trivial object. Do not retain it beyond the LCD lifecycle that initialized it.

## Colors

Color is a 16-bit RGB565 pixel value. Use RGB() to convert 8-bit RGB components:

~~~cpp
UI::Color orange = UI::RGB(255, 128, 0);
~~~

Predefined values are available in UI::Colors: Black, Blue, Green, Cyan, Red, Magenta, Yellow, White, and Gray.

## Drawing

~~~cpp
UI::Framebuffer screen;
if (screen.Begin()) {
    screen.Clear(UI::Colors::White);
    screen.SetPixel(2, 2, UI::Colors::Red);
    screen.FillRect(10, 10, 80, 30, UI::Colors::Blue);
    screen.DrawRect(8, 8, 84, 34, UI::Colors::Black);
    screen.DrawLine(0, 0, screen.Width() - 1,
                    screen.Height() - 1, UI::Colors::Red);
    screen.DrawText(16, 20, "Hello", UI::Colors::White);
    screen.Present();
}
~~~

Drawing requires an active framebuffer. Methods return false for an inactive framebuffer or invalid dimensions/pointers; a valid rectangle that lies entirely outside the display is a successful no-op. Pixel writes outside the display are skipped. Rectangles are clipped to the visible area. DrawLine() accepts endpoints only in the range -16383 to 16383 on each axis; it rasterizes the bounded line and writes only pixels that lie inside the LCD. It does not allocate a temporary image or use a floating-point clipping step.

Drawing modifies VRAM immediately. BlitRGB565() copies a caller-owned RGB565 raster directly into VRAM, supports integer nearest-neighbour scaling, clips at the LCD edges, and performs no allocation. Source dimensions must be within 1..528 by 1..320; scale must be 1..64. Present() calls LCD_Refresh(); it does not copy from a second buffer.

## Built-in font

The font uses 5x7 glyphs and a one-pixel horizontal gap (six pixels per character cell). It supports printable ASCII 32-126. Lowercase letters use uppercase glyphs; unsupported bytes use the question-mark glyph.

DrawText() behavior:
- Newline starts a new 8-pixel line.
- Carriage return is ignored.
- Tab advances by four character cells.
- A call processes at most 512 input bytes.
- A null string is rejected.

## Input events

Input::Poll() retrieves one event from the SDK and maps it into UI::Event.

~~~cpp
UI::Event event;
int result = UI::Input::Poll(&event);

if (result == 1 && event.type == UI::EventKey &&
    event.keyCode == UI::KeyEXE &&
    event.keyAction == UI::KeyPressed) {
    // Confirm the current selection.
}
~~~

Return values:

| Value | Meaning |
| --- | --- |
| 1 | One event was received |
| 0 | No event was available |
| -1 | Output pointer was null |
| -2 | SDK call returned an error |

### Event types

| Type | Fields |
| --- | --- |
| EventKey | keyCode, keyAction, rawAction |
| EventTouch | x, y, touchAction, rawAction |
| EventSystem | systemEvent |
| EventUnknown | rawType |

Touch coordinates are passed through unchanged. They may be negative or outside the display; validate them before using them for drawing.

Key actions are KeyPressed, KeyHeld, and KeyReleased. Touch actions are TouchDown, TouchDrag, TouchUp, and TouchActionBar. System events include action-bar resize, swap, rotate, escape, and settings.

The SDK documents single-touch data in EVENT_TOUCH. Multi-touch is not exposed by this wrapper. Unknown SDK event types are retained as EventUnknown with their raw type.

Poll() does not wait when there is no event. Applications can poll from their own event loop and call Framebuffer::Present() after drawing changes.

## Custom screen without the built-in menu

Set Config::showMainMenu to false. The library initializes the display and settings persistence, then calls onStart directly.

~~~cpp
static void Start(UI::Application &app) {
    UI::Framebuffer screen;
    if (!screen.Begin()) {
        app.Close();
        return;
    }

    while (app.IsRunning()) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0) {
            app.Close();
            return;
        }
        if (result == 0)
            continue;

        if (event.type == UI::EventKey &&
            event.keyCode == UI::KeyEXE &&
            event.keyAction == UI::KeyPressed) {
            app.Close();
        }
    }
}

extern "C" void main() {
    UI::Config config;
    config.showMainMenu = false;
    config.onStart = Start;

    UI::Application app(config);
    app.Run();
}
~~~

In custom-screen mode, onStart is invoked again when the built-in file selector or built-in settings returns to the main application state. Use app.OpenFileSelector(), app.OpenSettings(), and app.Close() to request transitions.

Each menu can independently select Native, Framebuffer, or Custom rendering. A custom file browser can combine Framebuffer, Input, UI::Directory, and UI::FileSystem; report a selected file with Application::SelectFile(). See application.md for the complete renderer API.

## Safety notes

- Framebuffer is non-copyable and has no public raw VRAM pointer.
- Begin() validates the VRAM pointer and dynamic LCD dimensions. Each dimension must be positive and no greater than 16384; the total image is capped at 1,048,576 pixels.
- SetPixel() rejects out-of-range coordinates.
- Rectangle drawing clips to the visible display.
- DrawLine() accepts endpoints only in the range -16383 to 16383 on each axis. It rasterizes the line between those endpoints and writes only pixels that fall inside the LCD bounds.
- Drawing performs no dynamic allocation.
- End() is idempotent; drawing after detaching is rejected.
- Framebuffer does not manage the global LCD backup/restore lifecycle.
