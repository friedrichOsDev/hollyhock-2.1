#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>

APP_NAME("Hollyhock UI Library Demo")
APP_DESCRIPTION("Read-only file browser and settings example")
APP_AUTHOR("Friedrich")
APP_VERSION("1.1")

static bool showPreview = true;
static int previewLength = 24;
static int textCase = 0;
static const char *const textCaseOptions[] = {
    "Original",
    "Uppercase",
    "Lowercase"
};

static void WaitForUser() {
    Debug_SetCursorPosition(0, 5);
    Debug_PrintString("Press a number key.", false);
    LCD_Refresh();
    Debug_WaitKey();
}

static void ShowStorageInfo(UI::Application &app) {
    (void)app;
    char normalizedPath[128];
    int pathResult = UI::FileSystem::NormalizePath(
        "fls0/", normalizedPath, sizeof(normalizedPath));

    int entryCount = 0;
    int directoryResult = 0;
    UI::Directory directory;
    directoryResult = directory.Open("fls0/");
    if (directoryResult == 0) {
        UI::DirectoryEntry entry;
        while (directory.Next(&entry) == 0) {
            if (!(entry.name[0] == '.' &&
                  (entry.name[1] == 0 ||
                   (entry.name[1] == '.' && entry.name[2] == 0)))) {
                ++entryCount;
            }
        }
        int closeResult = directory.Close();
        if (closeResult < 0)
            directoryResult = closeResult;
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Read-only storage check", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(pathResult == 0 ? "Path normalization: OK" : "Path normalization: FAIL", false);
    Debug_SetCursorPosition(0, 2);
    Debug_PrintString(UI::FileSystem::Exists("fls0/") ? "fls0 root: FOUND" : "fls0 root: MISSING", false);
    Debug_SetCursorPosition(0, 3);
    if (directoryResult == 0)
        Debug_Printf(0, 3, false, 0, "Directory entries: %d", entryCount);
    else
        Debug_Printf(0, 3, false, 0, "Directory error: %d", directoryResult);
    Debug_SetCursorPosition(0, 4);
    if (pathResult == 0)
        Debug_PrintString(normalizedPath, false);
    WaitForUser();
}

static void FileSelected(UI::Application &app, const char *path) {
    (void)app;
    struct stat info;
    int statResult = UI::FileSystem::Stat(path, &info);

    UI::File file;
    int result = file.Open(path);
    uint32_t fileSize = 0;
    int sizeResult = result;
    char preview[49];
    int bytesRead = 0;

    if (result >= 0) {
        sizeResult = file.GetSize(&fileSize);
        if (sizeResult == 0 && showPreview) {
            int requested = previewLength;
            if (requested < 8) requested = 8;
            if (requested > 48) requested = 48;
            int bytesToRead = fileSize < (uint32_t)requested
                                  ? (int)fileSize
                                  : requested;
            bytesRead = file.Read(preview, bytesToRead);
            if (bytesRead >= 0) {
                preview[bytesRead] = '\0';
                for (int i = 0; i < bytesRead; ++i) {
                    unsigned char c = (unsigned char)preview[i];
                    if (c < 32 || c > 126)
                        preview[i] = ' ';
                    else if (textCase == 1 && c >= 'a' && c <= 'z')
                        preview[i] = (char)(c - ('a' - 'A'));
                    else if (textCase == 2 && c >= 'A' && c <= 'Z')
                        preview[i] = (char)(c + ('a' - 'A'));
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
        Debug_Printf(0, 2, false, 0, "Stat size: %u bytes", (unsigned int)info.fileSize);
    else
        Debug_Printf(0, 2, false, 0, "Stat error: %d", statResult);
    Debug_SetCursorPosition(0, 3);
    if (result < 0)
        Debug_Printf(0, 3, false, 0, "Open error: %d", result);
    else if (sizeResult < 0)
        Debug_Printf(0, 3, false, 0, "Size error: %d", sizeResult);
    else
        Debug_Printf(0, 3, false, 0, "File size: %u bytes", (unsigned int)fileSize);
    Debug_SetCursorPosition(0, 4);
    if (!showPreview)
        Debug_PrintString("Preview disabled.", false);
    else if (bytesRead < 0)
        Debug_Printf(0, 4, false, 0, "Read error: %d", bytesRead);
    else {
        Debug_PrintString("Text preview:", false);
        Debug_SetCursorPosition(0, 5);
        Debug_PrintString(preview, false);
    }
    WaitForUser();
}

static void ResetSettings(UI::Application &app) {
    showPreview = true;
    previewLength = 24;
    textCase = 0;
    app.ReturnToMenu();
}

extern "C" void main() {
    UI::Config config;
    config.title = "Hollyhock UI Demo";
    config.startLabel = "Storage check";
    config.filesLabel = "Browse files";
    config.settingsLabel = "Settings";
    config.quitLabel = "Quit";

    config.showStart = true;
    config.onStart = ShowStorageInfo;
    config.showFileSelector = true;
    config.onFileSelected = FileSelected;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text files")
    };
    config.filters = filters;
    config.filterCount = 1;

    UI::Setting settings[] = {
        UI::Setting::Boolean("Show preview", &showPreview, "Enable file content preview"),
        UI::Setting::Integer("Preview bytes", &previewLength, 8, 48, 8, "Number of file bytes to read"),
        UI::Setting::Enumeration("Text case", &textCase, textCaseOptions, 3, "Preview letter case"),
        UI::Setting::Action("Reset settings", ResetSettings, "Restore default values")
    };
    config.showSettings = true;
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);
    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
