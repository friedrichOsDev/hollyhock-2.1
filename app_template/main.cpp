#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/file.hpp>
#include <sdk/os/input.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>

APP_NAME("Hollyhock UI API Test")
APP_DESCRIPTION("Temporary smoke test for the custom UI library API")
APP_AUTHOR("Friedrich")
APP_VERSION("0.2")

static bool testToggle = false;
static int testInteger = 2;
static int testEnum = 0;
static const char *testEnumOptions[] = {"First", "Second", "Third"};

static void WaitForKeyPress() {
    InputEvent event;
    for (;;) {
        event = {};
        GetInput(&event, 0xFFFFFFFF, 0x10);
        if (event.type == EVENT_KEY && event.data.key.direction == KEY_PRESSED)
            return;
    }
}

static void DrawStartScreen() {
    LCD_ClearScreen();
    int width = 0, height = 0;
    LCD_GetSize(&width, &height);
    const uint16_t black = RGB_TO_RGB565(0, 0, 0);
    // Simple inset frame and accent bars.
    for (int x = 12; x < width - 12; ++x) {
        LCD_SetPixel(x, 12, black);
        LCD_SetPixel(x, height - 13, black);
    }
    for (int y = 12; y < height - 12; ++y) {
        LCD_SetPixel(12, y, black);
        LCD_SetPixel(width - 13, y, black);
    }
    for (int x = 20; x < width - 20; ++x) {
        LCD_SetPixel(x, 40, black);
        LCD_SetPixel(x, 42, black);
    }
    LCD_Refresh();

    Debug_SetCursorPosition(4, 3);
    Debug_PrintString("HOLLYHOCK UI TEST", true);
    Debug_SetCursorPosition(4, 6);
    Debug_PrintString("Start callback works.", false);
    Debug_SetCursorPosition(4, 8);
    Debug_PrintString("Screen drawing test", false);
    Debug_SetCursorPosition(4, 11);
    Debug_PrintString("Press any key...", true);
    LCD_Refresh();
}

static void Start(UI::Application &app) {
    (void)app;
    DrawStartScreen();
    WaitForKeyPress();
    LCD_ClearScreen();
    LCD_Refresh();
}

static void ShowTextFile(const char *path) {
    LCD_ClearScreen();
    LCD_Refresh();

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("TEXT FILE VIEWER", true);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(path, false);
    Debug_SetCursorPosition(0, 3);

    int fd = open(path, OPEN_READ);
    if (fd < 0) {
        Debug_PrintString("Could not open file.", false);
        Debug_SetCursorPosition(0, 6);
        Debug_PrintString("Press any key to return.", false);
        LCD_Refresh();
        WaitForKeyPress();
        LCD_ClearScreen();
        LCD_Refresh();
        return;
    }

    // Read a bounded amount to avoid allocating memory for arbitrarily large files.
    static char content[2048];
    int bytes = read(fd, content, sizeof(content) - 1);
    close(fd);
    if (bytes < 0)
        bytes = 0;
    content[bytes] = '\0';

    const int maxCols = 30;
    const int maxRows = 20;
    int row = 3;
    int col = 0;
    for (int i = 0; i < bytes && row < maxRows; ++i) {
        char ch = content[i];
        if (ch == '\r')
            continue;
        if (ch == '\n' || col >= maxCols) {
            ++row;
            col = 0;
            if (ch == '\n')
                continue;
            if (row >= maxRows)
                break;
        }
        if (ch < 32 || ch > 126)
            ch = '?';
        Debug_SetCursorPosition(col, row);
        char one[2] = {ch, '\0'};
        Debug_PrintString(one, false);
        ++col;
    }

    if (bytes >= (int)sizeof(content) - 1 && row >= maxRows) {
        Debug_SetCursorPosition(0, maxRows - 1);
        Debug_PrintString("... file truncated ...", true);
    }
    Debug_SetCursorPosition(0, maxRows + 1);
    Debug_PrintString("Press any key to return", true);
    LCD_Refresh();
    WaitForKeyPress();
    LCD_ClearScreen();
    LCD_Refresh();
}

static void Selected(UI::Application &app, const char *path) {
    ShowTextFile(path);
    app.ReturnToMenu();
}

static void Reset(UI::Application &app) {
    testToggle = false;
    testInteger = 2;
    testEnum = 0;
    app.ReturnToMenu();
}

static void CustomSettings(UI::Application &app) {
    Debug_PrintString("Custom settings callback", 0);
    app.ReturnToMenu();
}

extern "C" void main() {
    UI::Config config;
    config.title = "UI API test";
    config.startLabel = "Test Start";
    config.onStart = Start;

    config.showFileSelector = true;
    config.filesLabel = "Browse";
    config.onFileSelected = Selected;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text"),
        UI::FileFilter("ui-test", "Test")};
    config.filters = filters;
    config.filterCount = 2;

    config.showSettings = true;
    UI::Setting settings[] = {
        UI::Setting::Boolean("Toggle", &testToggle, "Boolean value"),
        UI::Setting::Integer("Number", &testInteger, 0, 10, 2, "Increment and wrap"),
        UI::Setting::Enumeration("Mode", &testEnum, testEnumOptions, 3, "Cycle options"),
        UI::Setting::Action("Reset", Reset, "Reset all values")};
    config.settings = settings;
    config.settingCount = 4;
    config.onSettings = CustomSettings; // ignored when built-in settings are configured

    UI::Application app(config);
    app.Run();
}
