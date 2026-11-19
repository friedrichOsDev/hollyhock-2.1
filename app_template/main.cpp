#include <appdef.hpp>
#include <ui/ui.hpp>
#include <sdk/os/debug.hpp>

APP_NAME("Hollyhock UI API Test")
APP_DESCRIPTION("Temporary smoke test for the custom UI library API")
APP_AUTHOR("Friedrich")
APP_VERSION("0.1")

static bool testToggle = false;
static int testInteger = 2;
static int testEnum = 0;
static const char *testEnumOptions[] = {"First", "Second", "Third"};

static void Start(UI::Application &app) {
    Debug_PrintString("Start callback invoked", 0);
    (void)app;
}

static void Selected(UI::Application &app, const char *path) {
    Debug_PrintString("File callback invoked", 0);
    Debug_SetCursorPosition(1, 1);
    Debug_PrintString(path, 0);
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
        UI::FileFilter("ui-test", "Test")
    };
    config.filters = filters;
    config.filterCount = 2;

    config.showSettings = true;
    UI::Setting settings[] = {
        UI::Setting::Boolean("Toggle", &testToggle, "Boolean value"),
        UI::Setting::Integer("Number", &testInteger, 0, 10, 2, "Increment and wrap"),
        UI::Setting::Enumeration("Mode", &testEnum, testEnumOptions, 3, "Cycle options"),
        UI::Setting::Action("Reset", Reset, "Reset all values")
    };
    config.settings = settings;
    config.settingCount = 4;
    config.onSettings = CustomSettings; // ignored when built-in settings are configured

    UI::Application app(config);
    app.Run();
}
