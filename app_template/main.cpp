#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <ui/filesystem.hpp>
#include <sdk/os/input.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>

APP_NAME("Hollyhock UI Library Demo")
APP_DESCRIPTION("Demo application for the Hollyhock UI library")
APP_AUTHOR("Friedrich")
APP_VERSION("0.2")

static bool testToggle = false;
static int testInteger = 2;
static int testEnum = 0;
static const char *testEnumOptions[] = {"First", "Second", "Third"};

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
    config.showStart = false;

    config.showFileSelector = true;
    config.filesLabel = "Browse";

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
