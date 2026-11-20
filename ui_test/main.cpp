#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>
#include <stddef.h>

APP_NAME("UI File Browser Test")
APP_DESCRIPTION("Read-only file browser test for the Hollyhock UI library")
APP_AUTHOR("Friedrich")
APP_VERSION("1.1")

namespace {
static bool showPreview = true;
static int previewBytes = 24;

void WaitForUser() {
    Debug_SetCursorPosition(0, 6);
    Debug_PrintString("Press any key...", false);
    LCD_Refresh();
    Debug_WaitKey();
}

void ShowHeader(const char *title) {
    Debug_SetCursorPosition(0, 0);
    Debug_PrintString(title, false);
}

void ShowBrowserDiagnostics(UI::Application &app) {
    (void)app;

    char normalized[128];
    const int pathResult = UI::FileSystem::NormalizePath(
        "fls0/", normalized, sizeof(normalized));

    int entries = 0;
    int directoryResult = 0;
    UI::Directory directory;
    directoryResult = directory.Open("fls0/");
    if (directoryResult == 0) {
        UI::DirectoryEntry entry;
        while (directory.Next(&entry) == 0) {
            if (entry.name[0] == '.' &&
                (entry.name[1] == '\0' ||
                 (entry.name[1] == '.' && entry.name[2] == '\0')))
                continue;
            ++entries;
        }
        const int closeResult = directory.Close();
        if (closeResult < 0)
            directoryResult = closeResult;
    }

    const bool rootExists = UI::FileSystem::Exists("fls0/");
    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Read-only browser test", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(pathResult == 0 ? "Path normalization: PASS" : "Path normalization: FAIL", false);
    Debug_SetCursorPosition(0, 2);
    Debug_PrintString(rootExists ? "fls0 root: FOUND" : "fls0 root: NOT FOUND", false);
    Debug_SetCursorPosition(0, 3);
    if (directoryResult == 0)
        Debug_Printf(0, 3, false, 0, "Root entries: %d", entries);
    else
        Debug_Printf(0, 3, false, 0, "Directory error: %d", directoryResult);
    Debug_SetCursorPosition(0, 4);
    if (pathResult == 0)
        Debug_PrintString(normalized, false);
    LCD_Refresh();
    WaitForUser();
}

void ShowSelectedFile(UI::Application &app, const char *path) {
    (void)app;

    struct stat info;
    const int statResult = UI::FileSystem::Stat(path, &info);

    UI::File file;
    const int openResult = file.Open(path);
    uint32_t size = 0;
    int sizeResult = openResult;
    int bytesRead = 0;
    char preview[49];

    if (openResult >= 0) {
        sizeResult = file.GetSize(&size);
        if (sizeResult == 0 && showPreview) {
            int requested = previewBytes;
            if (requested < 8) requested = 8;
            if (requested > 48) requested = 48;
            int toRead = size < (uint32_t)requested ? (int)size : requested;
            bytesRead = file.Read(preview, toRead);
            if (bytesRead >= 0) {
                preview[bytesRead] = '\0';
                for (int i = 0; i < bytesRead; ++i) {
                    const unsigned char c = (unsigned char)preview[i];
                    if (c < 32 || c > 126)
                        preview[i] = '.';
                }
            }
        }
        file.Close();
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Selected file (read-only)", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(path, false);
    Debug_SetCursorPosition(0, 2);
    if (statResult == 0)
        Debug_Printf(0, 2, false, 0, "Stat size: %u", (unsigned int)info.fileSize);
    else
        Debug_Printf(0, 2, false, 0, "Stat error: %d", statResult);
    Debug_SetCursorPosition(0, 3);
    if (openResult < 0)
        Debug_Printf(0, 3, false, 0, "Open error: %d", openResult);
    else if (sizeResult < 0)
        Debug_Printf(0, 3, false, 0, "Size error: %d", sizeResult);
    else
        Debug_Printf(0, 3, false, 0, "Opened OK, size: %u", (unsigned int)size);

    Debug_SetCursorPosition(0, 4);
    if (!showPreview)
        Debug_PrintString("Preview disabled in Settings", false);
    else if (bytesRead < 0)
        Debug_Printf(0, 4, false, 0, "Read error: %d", bytesRead);
    else
        Debug_PrintString("Preview:", false);
    if (showPreview && bytesRead >= 0) {
        Debug_SetCursorPosition(0, 5);
        Debug_PrintString(preview, false);
    }

    LCD_Refresh();
    WaitForUser();
}

void ResetBrowserSettings(UI::Application &app) {
    showPreview = true;
    previewBytes = 24;
    app.ReturnToMenu();
}
} // namespace

extern "C" void main() {
    UI::Config config;
    config.title = "UI File Browser Test";
    config.startLabel = "Run read-only checks";
    config.filesLabel = "Browse files";
    config.settingsLabel = "Browser settings";
    config.quitLabel = "Exit";

    config.showStart = true;
    config.onStart = ShowBrowserDiagnostics;
    config.showFileSelector = true;
    config.onFileSelected = ShowSelectedFile;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text files"),
        UI::FileFilter(".md", "Markdown files")
    };
    config.filters = filters;
    config.filterCount = sizeof(filters) / sizeof(filters[0]);

    UI::Setting settings[] = {
        UI::Setting::Boolean("Show preview", &showPreview, "Read a preview of selected files"),
        UI::Setting::Integer("Preview bytes", &previewBytes, 8, 48, 8, "Maximum preview length"),
        UI::Setting::Action("Reset settings", ResetBrowserSettings, "Restore browser defaults")
    };
    config.showSettings = true;
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);
    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
