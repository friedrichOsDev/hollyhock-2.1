#include <appdef.hpp>
#include <ui/ui.hpp>

#include "demo/demo.hpp"

APP_NAME("Hollyhock UI Library Showcase")
APP_DESCRIPTION("Native, framebuffer and custom menu renderer examples")
APP_AUTHOR("Friedrich")
APP_VERSION("2.0")

DemoSettings g_demoSettings = {
    true,   // showPreview
    32,     // previewBytes
    0,      // previewFormat
    true    // showFileSize
};

extern "C" void main() {
    UI::Config config;
    config.title = "UI Library Showcase";
    config.settingsFolder = "UIDEMO2";

    config.showMainMenu = true;
    config.showStart = true;
    config.showFileSelector = true;
    config.showSettings = true;
    config.showQuit = true;

    // The demo intentionally mixes all three menu renderer modes:
    // - Framebuffer: library-provided main menu
    // - Native: original Hollyhock file selector
    // - Custom: demo-owned settings screen
    config.mainMenuRenderer = UI::MenuRenderer::Framebuffer;
    config.fileSelectorRenderer = UI::MenuRenderer::Native;
    config.settingsRenderer = UI::MenuRenderer::Custom;

    config.startLabel = "Open dashboard";
    config.filesLabel = "Browse files";
    config.settingsLabel = "Demo settings";
    config.quitLabel = "Exit";

    config.onStart = DemoStart;
    config.onFileSelected = DemoFileSelected;
    config.onCustomSettings = DemoCustomSettings;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text files"),
        UI::FileFilter(".md", "Markdown files"),
        UI::FileFilter(".ini", "Configuration files"),
        UI::FileFilter(".cfg", "Configuration files")
    };
    config.filters = filters;
    config.filterCount = sizeof(filters) / sizeof(filters[0]);

    const char *const previewFormatOptions[] = {
        "Text",
        "Hexadecimal"
    };
    UI::Setting settings[] = {
        UI::Setting::Boolean(
            "demo.preview.enabled", "File preview",
            &g_demoSettings.showPreview,
            "Show a short text preview after selection"),
        UI::Setting::Integer(
            "demo.preview.bytes", "Preview bytes",
            &g_demoSettings.previewBytes,
            8, 64, 8, "Number of bytes read from the start of a file"),
        UI::Setting::Enumeration(
            "demo.preview.format", "Preview format",
            &g_demoSettings.previewFormat,
            previewFormatOptions, 2,
            "Display selected bytes as text or hexadecimal"),
        UI::Setting::Boolean(
            "demo.file-size", "Show file size",
            &g_demoSettings.showFileSize,
            "Display the selected file size"),
        UI::Setting::Action(
            "demo.reset", "Reset demo settings", DemoResetSettings,
            "Restore the initial values")
    };
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);

    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
