#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>

APP_NAME("UI File Browser Test")
APP_DESCRIPTION("Read-only file browser and persistent settings API test")
APP_AUTHOR("Friedrich")
APP_VERSION("1.2")

namespace {
static bool showPreview = true;
static int previewBytes = 24;

void WaitForUser() {
    Debug_SetCursorPosition(0, 7);
    Debug_PrintString("Press any key...", false);
    LCD_Refresh();
    Debug_WaitKey();
}

void ShowBrowserDiagnostics(UI::Application &app) {
    char normalized[128];
    const int pathResult = UI::FileSystem::NormalizePath(
        "fls0/", normalized, sizeof(normalized));
    char alternate[128];
    const int alternateResult = UI::FileSystem::NormalizePath(
        "/fls0//", alternate, sizeof(alternate));
    char rejected[128];
    const int traversalResult = UI::FileSystem::NormalizePath(
        "fls0/../", rejected, sizeof(rejected));
    const int volumeResult = UI::FileSystem::NormalizePath(
        "other/file.txt", rejected, sizeof(rejected));

    int entries = 0;
    int directoryResult = 0;
    bool directoryOpenState = false;
    UI::Directory directory;
    directoryResult = directory.Open("fls0/");
    if (directoryResult == 0) {
        directoryOpenState = directory.IsOpen();
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

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Read-only browser diagnostics", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(pathResult == 0 && alternateResult == 0 ? "Path normalization: PASS" : "Path normalization: FAIL", false);
    Debug_SetCursorPosition(0, 2);
    Debug_PrintString(traversalResult == EACCES ? "Traversal protection: PASS" : "Traversal protection: FAIL", false);
    Debug_SetCursorPosition(0, 3);
    Debug_PrintString(volumeResult == ENOVOLUME ? "Volume validation: PASS" : "Volume validation: FAIL", false);
    Debug_SetCursorPosition(0, 4);
    if (directoryResult == 0)
        Debug_Printf(0, 4, false, 0, "Dir open:%s entries:%d", directoryOpenState ? "yes" : "no", entries);
    else
        Debug_Printf(0, 4, false, 0, "Directory error: %d", directoryResult);

    // This tests Calc Vars (MCS), not the fls0 file system. The UI file
    // wrapper remains read-only; PersistentStore is the separate save API.
    UI::PersistentStore store;
    int storeResult = store.Open("UITEST");
    int saveResult = storeResult;
    int loadResult = storeResult;
    bool roundTrip = false;
    const char expected[] = "UI MCS API OK";
    char actual[32] = {0};
    uint8_t storedType = 0;
    uint32_t storedSize = 0;
    if (storeResult == 0) {
        saveResult = store.Save("APITEST", VARTYPE_STR, expected, sizeof(expected));
        if (saveResult == 0) {
            loadResult = store.Load("APITEST", &storedType, actual,
                                    sizeof(actual), &storedSize);
            if (loadResult == 0 && storedType == VARTYPE_STR &&
                storedSize == sizeof(expected)) {
                roundTrip = true;
                for (unsigned int i = 0; i < sizeof(expected); ++i) {
                    if (actual[i] != expected[i]) {
                        roundTrip = false;
                        break;
                    }
                }
            }
        }
    }
    Debug_SetCursorPosition(0, 5);
    Debug_Printf(0, 5, false, 0, "MCS:%s App:%d", roundTrip ? "PASS" : "FAIL", app.SettingsPersistenceResult());
    Debug_SetCursorPosition(0, 6);
    Debug_Printf(0, 6, false, 0, "Store:%d Save:%d Load:%d", storeResult, saveResult, loadResult);
    LCD_Refresh();
    WaitForUser();
}

void ShowSelectedFile(UI::Application &app, const char *path) {
    (void)app;
    struct stat info;
    const int statResult = UI::FileSystem::Stat(path, &info);
    const bool exists = UI::FileSystem::Exists(path);

    UI::File file;
    const int openResult = file.Open(path);
    const bool openState = file.IsOpen();
    const int handle = file.Handle();
    uint32_t size = 0;
    int sizeResult = openResult;
    int seekResult = openResult;
    int bytesRead = -1;
    char preview[49] = {0};

    if (openResult >= 0) {
        sizeResult = file.GetSize(&size);
        seekResult = file.Seek(0, SEEK_SET);
        if (sizeResult == 0 && seekResult >= 0 && showPreview) {
            int requested = previewBytes;
            if (requested < 8) requested = 8;
            if (requested > 48) requested = 48;
            const int toRead = size < (uint32_t)requested ? (int)size : requested;
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
    const bool closedState = !file.IsOpen();

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Selected file (read-only)", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(path, false);
    Debug_SetCursorPosition(0, 2);
    if (statResult == 0)
        Debug_Printf(0, 2, false, 0, "Stat:%u Exists:%s", (unsigned int)info.fileSize, exists ? "yes" : "no");
    else
        Debug_Printf(0, 2, false, 0, "Stat error: %d", statResult);
    Debug_SetCursorPosition(0, 3);
    if (openResult < 0)
        Debug_Printf(0, 3, false, 0, "Open error: %d", openResult);
    else
        Debug_Printf(0, 3, false, 0, "Open:%s handle:%d", openState && closedState ? "PASS" : "FAIL", handle);
    Debug_SetCursorPosition(0, 4);
    if (sizeResult < 0 || seekResult < 0)
        Debug_Printf(0, 4, false, 0, "Size:%d Seek:%d", sizeResult, seekResult);
    else
        Debug_Printf(0, 4, false, 0, "Size:%u Seek:%d Close:%s", (unsigned int)size, seekResult, closedState ? "PASS" : "FAIL");
    Debug_SetCursorPosition(0, 5);
    if (!showPreview)
        Debug_PrintString("Preview disabled in Settings", false);
    else if (bytesRead < 0)
        Debug_Printf(0, 5, false, 0, "Read error: %d", bytesRead);
    else
        Debug_PrintString(preview, false);

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
    config.settingsFolder = "UITEST";

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
