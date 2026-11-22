#pragma once

#include <stdint.h>

namespace UI {

typedef uint16_t Color;

// Converts 8-bit RGB components to the LCD's RGB565 format.
Color RGB(uint8_t red, uint8_t green, uint8_t blue);

namespace Colors {
enum : Color {
    Black   = 0x0000,
    Blue    = 0x001F,
    Green   = 0x07E0,
    Cyan    = 0x07FF,
    Red     = 0xF800,
    Magenta = 0xF81F,
    Yellow  = 0xFFE0,
    White   = 0xFFFF,
    Gray    = 0x8410
};
}

// A bounded drawing interface for the calculator's existing LCD VRAM.
// Drawing operations update VRAM; call Present() to refresh the LCD.
class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;

    // Attaches to the LCD VRAM. The calculator display must already be
    // initialized (normally by UI::Application::Run()).
    // Returns false if the display is unavailable or already active.
    bool Begin();

    // Detaches from VRAM. Does not restore or refresh the display.
    void End();

    bool IsActive() const;
    int Width() const;
    int Height() const;

    bool Clear(Color color);
    bool SetPixel(int x, int y, Color color);
    bool FillRect(int x, int y, int width, int height, Color color);
    bool DrawRect(int x, int y, int width, int height, Color color);
    bool DrawLine(int x1, int y1, int x2, int y2, Color color);

    // Copy a bounded RGB565 image into VRAM with integer nearest-neighbour scaling.
    bool BlitRGB565(int x, int y, const uint16_t *source,
                    int sourceWidth, int sourceHeight, int scale = 1);

    // Uses a built-in 5x7 bitmap font with a one-pixel character gap.
    // Lowercase ASCII letters are rendered using their uppercase glyph.
    // Newline starts a new line; tab advances by four character cells.
    // Text is bounded to 512 input bytes per call.
    bool DrawChar(int x, int y, char character, Color color);
    bool DrawText(int x, int y, const char *text, Color color);

    // Pushes the current VRAM contents to the physical LCD.
    bool Present();

private:
    uint16_t *m_pixels;
    int m_width;
    int m_height;
    bool m_active;

    bool PutPixel(int x, int y, Color color);
};

} // namespace UI
