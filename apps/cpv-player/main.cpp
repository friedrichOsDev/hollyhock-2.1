#include <appdef.hpp>
#include <ui/ui.hpp>

APP_NAME("CPV Video Player")
APP_DESCRIPTION("A Video Player for the custom .cpv format")
APP_AUTHOR("friedrichOsDev")
APP_VERSION("2.0")

void CPVFileSelected(UI::Application &app, const char *path) {
    (void)app;
    (void)path;
    return;
}

extern "C" void main() {
    UI::Config config;
    config.title = "CPV Player";
    config.settingsFolder = "CPVPL";

    config.showMainMenu = true;
    config.showStart = false;
    config.showFileSelector = true;
    config.showSettings = false;
    config.showQuit = true;

    // The demo intentionally mixes all three menu renderer modes:
    // - Framebuffer: library-provided main menu
    // - Native: original Hollyhock file selector
    // - Custom: demo-owned settings screen
    config.mainMenuRenderer = UI::MenuRenderer::Framebuffer;
    config.fileSelectorRenderer = UI::MenuRenderer::Framebuffer;

    config.filesLabel = "Select Video";
    config.quitLabel = "Exit";

    config.onFileSelected = CPVFileSelected;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".cpv", "Video files (format version 1.0)"),
        UI::FileFilter(".cpv2", "Video files (format version 2.0)")
    };
    config.filters = filters;
    config.filterCount = sizeof(filters) / sizeof(filters[0]);

    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}