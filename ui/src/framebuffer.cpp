#include <sdk/os/lcd.hpp>
#include <ui/framebuffer.hpp>

namespace UI {
namespace {

const int kMaximumDimension = 16384;
const uint32_t kMaximumPixels = 1048576U;
const int kMaximumCoordinate = 16383;
const int kIntMaximum = 2147483647;
const int kIntMinimum = (-2147483647 - 1);
const int kMaximumTextBytes = 512;

int SaturatingAddPositive(int value, int amount) {
    if (amount <= 0)
        return value;
    if (value > kIntMaximum - amount)
        return kIntMaximum;
    return value + amount;
}

int ClipCode(int x, int y, int width, int height) {
    int code = 0;
    if (x < 0)
        code |= 1;
    else if (x >= width)
        code |= 2;

    if (y < 0)
        code |= 4;
    else if (y >= height)
        code |= 8;
    return code;
}

bool ClipLine(int *x0, int *y0, int *x1, int *y1,
              int width, int height) {
    int code0 = ClipCode(*x0, *y0, width, height);
    int code1 = ClipCode(*x1, *y1, width, height);
    int iterations = 0;

    while (true) {
        // Cohen-Sutherland clipping should converge in a few passes.
        // This guard prevents malformed geometry from looping indefinitely.
        if (++iterations > 8)
            return false;
        if ((code0 | code1) == 0)
            return true;
        if ((code0 & code1) != 0)
            return false;

        const int outside = code0 ? code0 : code1;
        const int dx = *x1 - *x0;
        const int dy = *y1 - *y0;
        int x = 0;
        int y = 0;

        if (outside & 4) {
            if (dy == 0)
                return false;
            y = 0;
            x = *x0 + (dx * (0 - *y0)) / dy;
        } else if (outside & 8) {
            if (dy == 0)
                return false;
            y = height - 1;
            x = *x0 + (dx * (y - *y0)) / dy;
        } else if (outside & 2) {
            if (dx == 0)
                return false;
            x = width - 1;
            y = *y0 + (dy * (x - *x0)) / dx;
        } else {
            if (dx == 0)
                return false;
            x = 0;
            y = *y0 + (dy * (0 - *x0)) / dx;
        }

        if (outside == code0) {
            *x0 = x;
            *y0 = y;
            code0 = ClipCode(*x0, *y0, width, height);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = ClipCode(*x1, *y1, width, height);
        }
    }
}

void CopyGlyph(const uint8_t source[7], uint8_t destination[7]) {
    for (int row = 0; row < 7; ++row)
        destination[row] = source[row];
}

void GetGlyphRows(unsigned char character, uint8_t rows[7]) {
    for (int row = 0; row < 7; ++row)
        rows[row] = 0;

    if (character >= 'a' && character <= 'z')
        character = static_cast<unsigned char>(character - ('a' - 'A'));

    static const uint8_t alphaNumeric[36][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
        {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, // C
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
        {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}, // G
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
        {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, // J
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
        {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, // 3
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
        {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, // 5
        {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, // 6
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
        {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}  // 9
    };

    if (character >= 'A' && character <= 'Z') {
        CopyGlyph(alphaNumeric[character - 'A'], rows);
        return;
    }
    if (character >= '0' && character <= '9') {
        CopyGlyph(alphaNumeric[26 + character - '0'], rows);
        return;
    }

    switch (character) {
    case ' ': break;
    case '!': { const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04}; CopyGlyph(g, rows); break; }
    case '"': { const uint8_t g[7] = {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '#': { const uint8_t g[7] = {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}; CopyGlyph(g, rows); break; }
    case '$': { const uint8_t g[7] = {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}; CopyGlyph(g, rows); break; }
    case '%': { const uint8_t g[7] = {0x19,0x1A,0x04,0x08,0x16,0x06,0x00}; CopyGlyph(g, rows); break; }
    case '&': { const uint8_t g[7] = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}; CopyGlyph(g, rows); break; }
    case '\'': { const uint8_t g[7] = {0x04,0x04,0x08,0x00,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '(': { const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; CopyGlyph(g, rows); break; }
    case ')': { const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; CopyGlyph(g, rows); break; }
    case '*': { const uint8_t g[7] = {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}; CopyGlyph(g, rows); break; }
    case '+': { const uint8_t g[7] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; CopyGlyph(g, rows); break; }
    case ',': { const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x04,0x08}; CopyGlyph(g, rows); break; }
    case '-': { const uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '.': { const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x04,0x04}; CopyGlyph(g, rows); break; }
    case '/': { const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0x00,0x00}; CopyGlyph(g, rows); break; }
    case ':': { const uint8_t g[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00}; CopyGlyph(g, rows); break; }
    case ';': { const uint8_t g[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x08}; CopyGlyph(g, rows); break; }
    case '<': { const uint8_t g[7] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02}; CopyGlyph(g, rows); break; }
    case '=': { const uint8_t g[7] = {0x00,0x1F,0x00,0x1F,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '>': { const uint8_t g[7] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08}; CopyGlyph(g, rows); break; }
    case '?': { const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; CopyGlyph(g, rows); break; }
    case '@': { const uint8_t g[7] = {0x0E,0x11,0x17,0x15,0x17,0x10,0x0F}; CopyGlyph(g, rows); break; }
    case '[': { const uint8_t g[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; CopyGlyph(g, rows); break; }
    case '\\': { const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0x00,0x00}; CopyGlyph(g, rows); break; }
    case ']': { const uint8_t g[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; CopyGlyph(g, rows); break; }
    case '^': { const uint8_t g[7] = {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '_': { const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}; CopyGlyph(g, rows); break; }
    case '`': { const uint8_t g[7] = {0x08,0x04,0x02,0x00,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    case '{': { const uint8_t g[7] = {0x02,0x04,0x04,0x08,0x04,0x04,0x02}; CopyGlyph(g, rows); break; }
    case '|': { const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04}; CopyGlyph(g, rows); break; }
    case '}': { const uint8_t g[7] = {0x08,0x04,0x04,0x02,0x04,0x04,0x08}; CopyGlyph(g, rows); break; }
    case '~': { const uint8_t g[7] = {0x00,0x00,0x09,0x16,0x00,0x00,0x00}; CopyGlyph(g, rows); break; }
    default: {
        const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
        CopyGlyph(g, rows);
        break;
    }
    }
}

} // namespace

Color RGB(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<Color>(
        ((static_cast<uint16_t>(red) >> 3) << 11) |
        ((static_cast<uint16_t>(green) >> 2) << 5) |
        (static_cast<uint16_t>(blue) >> 3));
}

Framebuffer::Framebuffer()
    : m_pixels(0), m_width(0), m_height(0), m_active(false) {}

Framebuffer::~Framebuffer() {
    End();
}

bool Framebuffer::Begin() {
    if (m_active)
        return false;

    int width = 0;
    int height = 0;
    LCD_GetSize(&width, &height);
    uint16_t *pixels = LCD_GetVRAMAddress();

    if (!pixels || width <= 0 || height <= 0 ||
        width > kMaximumDimension || height > kMaximumDimension) {
        End();
        return false;
    }

    const uint32_t pixelCount =
        static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    if (pixelCount > kMaximumPixels) {
        End();
        return false;
    }

    // Dimension and pixel-count limits keep row-major indexing bounded.
    m_pixels = pixels;
    m_width = width;
    m_height = height;
    m_active = true;
    return true;
}

void Framebuffer::End() {
    m_pixels = 0;
    m_width = 0;
    m_height = 0;
    m_active = false;
}

bool Framebuffer::IsActive() const {
    return m_active;
}

int Framebuffer::Width() const {
    return m_active ? m_width : 0;
}

int Framebuffer::Height() const {
    return m_active ? m_height : 0;
}

bool Framebuffer::PutPixel(int x, int y, Color color) {
    if (!m_active || !m_pixels ||
        x < 0 || y < 0 || x >= m_width || y >= m_height)
        return false;

    const uint32_t index =
        static_cast<uint32_t>(y) * static_cast<uint32_t>(m_width) +
        static_cast<uint32_t>(x);
    m_pixels[index] = color;
    return true;
}

bool Framebuffer::Clear(Color color) {
    if (!m_active || !m_pixels)
        return false;

    const uint32_t count =
        static_cast<uint32_t>(m_width) * static_cast<uint32_t>(m_height);
    for (uint32_t i = 0; i < count; ++i)
        m_pixels[i] = color;
    return true;
}

bool Framebuffer::SetPixel(int x, int y, Color color) {
    return PutPixel(x, y, color);
}

bool Framebuffer::FillRect(int x, int y, int width, int height, Color color) {
    if (!m_active || !m_pixels || width <= 0 || height <= 0)
        return false;

    int left = x;
    int top = y;
    int visibleWidth = width;
    int visibleHeight = height;

    if (left < 0) {
        if (left == kIntMinimum)
            return true;
        const int skipped = -left;
        if (visibleWidth <= skipped)
            return true;
        visibleWidth -= skipped;
        left = 0;
    }
    if (top < 0) {
        if (top == kIntMinimum)
            return true;
        const int skipped = -top;
        if (visibleHeight <= skipped)
            return true;
        visibleHeight -= skipped;
        top = 0;
    }

    if (left >= m_width || top >= m_height)
        return true;

    const int availableWidth = m_width - left;
    const int availableHeight = m_height - top;
    if (visibleWidth > availableWidth)
        visibleWidth = availableWidth;
    if (visibleHeight > availableHeight)
        visibleHeight = availableHeight;

    for (int row = 0; row < visibleHeight; ++row) {
        const uint32_t start =
            static_cast<uint32_t>(top + row) *
            static_cast<uint32_t>(m_width) +
            static_cast<uint32_t>(left);
        for (int column = 0; column < visibleWidth; ++column)
            m_pixels[start + static_cast<uint32_t>(column)] = color;
    }
    return true;
}

bool Framebuffer::DrawRect(int x, int y, int width, int height, Color color) {
    if (!m_active || width <= 0 || height <= 0)
        return false;

    FillRect(x, y, width, 1, color);
    if (height > 1)
        FillRect(x, SaturatingAddPositive(y, height - 1), width, 1, color);

    if (height > 2) {
        const int innerY = SaturatingAddPositive(y, 1);
        const int innerHeight = height - 2;
        FillRect(x, innerY, 1, innerHeight, color);
        if (width > 1)
            FillRect(SaturatingAddPositive(x, width - 1), innerY,
                     1, innerHeight, color);
    }
    return true;
}

bool Framebuffer::DrawLine(int x1, int y1, int x2, int y2, Color color) {
    if (!m_active || !m_pixels)
        return false;

    // Reject pathological coordinates before clipping. These bounds keep
    // every clipping product inside signed 32-bit arithmetic on SH-4.
    if (x1 < -kMaximumCoordinate || x1 > kMaximumCoordinate ||
        x2 < -kMaximumCoordinate || x2 > kMaximumCoordinate ||
        y1 < -kMaximumCoordinate || y1 > kMaximumCoordinate ||
        y2 < -kMaximumCoordinate || y2 > kMaximumCoordinate)
        return false;

    if (!ClipLine(&x1, &y1, &x2, &y2, m_width, m_height))
        return true;

    const int dx = x2 >= x1 ? x2 - x1 : x1 - x2;
    const int sx = x1 < x2 ? 1 : -1;
    const int dyMagnitude = y2 >= y1 ? y2 - y1 : y1 - y2;
    const int dy = -dyMagnitude;
    const int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        PutPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;

        const int twiceError = error * 2;
        if (twiceError >= dy) {
            error += dy;
            x1 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y1 += sy;
        }
    }
    return true;
}

bool Framebuffer::DrawChar(int x, int y, char character, Color color) {
    if (!m_active || !m_pixels)
        return false;

    uint8_t rows[7];
    GetGlyphRows(static_cast<unsigned char>(character), rows);

    for (int row = 0; row < 7; ++row) {
        if (y > kIntMaximum - row)
            continue;
        const int pixelY = y + row;
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (1U << (4 - column))) == 0 ||
                x > kIntMaximum - column)
                continue;
            PutPixel(x + column, pixelY, color);
        }
    }
    return true;
}

bool Framebuffer::DrawText(int x, int y, const char *text, Color color) {
    if (!m_active || !m_pixels || !text)
        return false;

    int cursorX = x;
    int cursorY = y;
    int length = 0;

    while (length < kMaximumTextBytes && text[length]) {
        const unsigned char character =
            static_cast<unsigned char>(text[length]);

        if (character == '\n') {
            cursorX = x;
            cursorY = SaturatingAddPositive(cursorY, 8);
        } else if (character == '\r') {
            // Ignore carriage returns.
        } else if (character == '\t') {
            cursorX = SaturatingAddPositive(cursorX, 24);
        } else {
            DrawChar(cursorX, cursorY, static_cast<char>(character), color);
            cursorX = SaturatingAddPositive(cursorX, 6);
        }
        ++length;
    }

    // The input is a null-terminated C string. Accept exactly 512 bytes,
    // but reject any longer string without scanning beyond the bounded limit.
    return length < kMaximumTextBytes || text[length] == 0;
}

bool Framebuffer::Present() {
    if (!m_active || !m_pixels)
        return false;
    LCD_Refresh();
    return true;
}

} // namespace UI
