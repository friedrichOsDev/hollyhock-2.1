#include <appdef.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <ui/ui.hpp>

APP_NAME("Hollyhock UI Library Demo")
APP_DESCRIPTION("Example application for the Hollyhock UI library")
APP_AUTHOR("Friedrich")
APP_VERSION("1.0")

static const char SAMPLE_PATH[] = "fls0/ui_library_demo.txt";
static const char SAMPLE_CONTENT[] =
    "Hollyhock UI Library demo file.\r\n"
    "This file was created by the demo application.\r\n"
    "Select it in the file browser to test UI::File.\r\n";

static bool showPreview = true;
static int previewLength = 16;
static int textCase = 0;
static const char *const textCaseOptions[] = {
    "Original",
    "Uppercase",
    "Lowercase"};

static void ShowProgress(const char *message) {
    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Create sample diagnostic", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(message, false);
    LCD_Refresh();
}

static void WaitForUser() {
    Debug_SetCursorPosition(0, 4);
    Debug_PrintString("Press a number key.", false);
    LCD_Refresh();
    Debug_WaitKey();
}

static void ShowStorageInfo(UI::Application &app) {
    (void)app;

    char normalizedPath[128];
    int pathResult = UI::FileSystem::NormalizePath(
        SAMPLE_PATH, normalizedPath, sizeof(normalizedPath));

    int entryCount = -1;
    int directoryResult = 0;
    UI::Directory directory;
    directoryResult = directory.Open("fls0/");
    if (directoryResult == 0) {
        entryCount = 0;
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

    bool fileExists = UI::FileSystem::Exists(SAMPLE_PATH);
    uint32_t fileSize = 0;
    int readResult = -1;
    bool contentMatches = false;

    if (fileExists) {
        UI::File file;
        int openResult = file.Open(SAMPLE_PATH, UI::FileRead);
        if (openResult >= 0) {
            int sizeResult = file.GetSize(&fileSize);
            if (sizeResult == 0 && fileSize == sizeof(SAMPLE_CONTENT) - 1) {
                char buffer[sizeof(SAMPLE_CONTENT)];
                readResult = file.Read(buffer, (int)(sizeof(SAMPLE_CONTENT) - 1));
                if (readResult == (int)(sizeof(SAMPLE_CONTENT) - 1)) {
                    buffer[readResult] = 0;
                    contentMatches = true;
                    for (unsigned int i = 0; i < sizeof(SAMPLE_CONTENT) - 1; ++i) {
                        if (buffer[i] != SAMPLE_CONTENT[i]) {
                            contentMatches = false;
                            break;
                        }
                    }
                } else {
                    readResult = readResult < 0 ? readResult : EINVAL;
                }
            } else {
                readResult = sizeResult < 0 ? sizeResult : EINVAL;
            }
            file.Close();
        } else {
            readResult = openResult;
        }
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Storage check", false);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString(pathResult == 0 ? "Path normalization: OK" : "Path normalization: FAIL", false);
    Debug_SetCursorPosition(0, 2);
    if (fileExists)
        Debug_Printf(0, 2, false, 0, "Sample: FOUND (%u bytes)", (unsigned int)fileSize);
    else
        Debug_PrintString("Sample: MISSING", false);
    Debug_SetCursorPosition(0, 3);
    if (fileExists && contentMatches)
        Debug_PrintString("Readback: OK", false);
    else if (fileExists)
        Debug_Printf(0, 3, false, 0, "Readback: FAIL (%d)", readResult);
    else
        Debug_PrintString("Readback: not available", false);
    Debug_SetCursorPosition(0, 4);
    if (directoryResult == 0)
        Debug_Printf(0, 4, false, 0, "Directory entries: %d", entryCount);
    else
        Debug_Printf(0, 4, false, 0, "Directory error: %d", directoryResult);

    WaitForUser();
}

static void CreateSampleFile(UI::Application &app) {
    (void)app;

    ShowProgress("Stage 1: checking existence");
    // Remove an old copy first: OPEN_CREATE does not promise truncation.
    if (UI::FileSystem::Exists(SAMPLE_PATH)) {
        ShowProgress("Stage 2: removing old file");
        int removeResult = ::remove("\\fls0\\ui_library_demo.txt");
        if (removeResult < 0) {
            Debug_SetCursorPosition(0, 0);
            Debug_PrintString("Could not replace sample.", false);
            Debug_Printf(0, 1, false, 0, "Remove error: %d", removeResult);
            WaitForUser();
            return;
        }
    }

    ShowProgress("Stage 3: opening for create");
    UI::File file;
    int result = file.Open(SAMPLE_PATH, UI::FileCreate);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Sample open failed.", false);
        Debug_Printf(0, 1, false, 0, "Open error: %d", result);
        WaitForUser();
        return;
    }

    ShowProgress("Stage 4: writing data");
    int written = file.Write(SAMPLE_CONTENT, (int)(sizeof(SAMPLE_CONTENT) - 1));
    ShowProgress("Stage 5: closing file");
    int closeResult = file.Close();
    if (written != (int)(sizeof(SAMPLE_CONTENT) - 1) || closeResult < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Sample write failed.", false);
        Debug_Printf(0, 1, false, 0, "Write: %d Close: %d", written, closeResult);
        WaitForUser();
        return;
    }

    // Re-open and verify that the bytes were actually stored.
    ShowProgress("Stage 6: reopening for verify");
    result = file.Open(SAMPLE_PATH, UI::FileRead);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Sample reopen failed.", false);
        Debug_Printf(0, 1, false, 0, "Open error: %d", result);
        WaitForUser();
        return;
    }

    ShowProgress("Stage 7: getting file size");
    uint32_t size = 0;
    result = file.GetSize(&size);
    char verify[sizeof(SAMPLE_CONTENT)];
    ShowProgress("Stage 8: reading verify data");
    int bytesRead = result == 0
                        ? file.Read(verify, (int)(sizeof(SAMPLE_CONTENT) - 1))
                        : result;
    file.Close();

    bool verified = result == 0 &&
                    size == sizeof(SAMPLE_CONTENT) - 1 &&
                    bytesRead == (int)(sizeof(SAMPLE_CONTENT) - 1);
    if (verified) {
        verify[bytesRead] = 0;
        for (unsigned int i = 0; i < sizeof(SAMPLE_CONTENT) - 1; ++i) {
            if (verify[i] != SAMPLE_CONTENT[i]) {
                verified = false;
                break;
            }
        }
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString(verified ? "Sample created & verified." : "Sample verify FAILED.", false);
    Debug_SetCursorPosition(0, 1);
    Debug_Printf(0, 1, false, 0, "Size: %u bytes", (unsigned int)size);
    Debug_SetCursorPosition(0, 2);
    Debug_Printf(0, 2, false, 0, "Read: %d Expected: %u", bytesRead,
                 (unsigned int)(sizeof(SAMPLE_CONTENT) - 1));
    Debug_SetCursorPosition(0, 3);
    Debug_PrintString(verified ? "Browse to view the text." : "Use Storage check for status.", false);
    WaitForUser();
}

static void FileSelected(UI::Application &app, const char *path) {
    (void)app;

    UI::File file;
    int result = file.Open(path, UI::FileRead);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("File open failed.", false);
        WaitForUser();
        return;
    }

    uint32_t fileSize = 0;
    result = file.GetSize(&fileSize);
    if (result < 0) {
        file.Close();
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Could not read file size.", false);
        WaitForUser();
        return;
    }

    char preview[25];
    int bytesRead = 0;

    if (showPreview) {
        int requested = previewLength;
        if (requested < 4)
            requested = 4;
        if (requested > 24)
            requested = 24;

        int bytesToRead = fileSize < (uint32_t)requested
                              ? (int)fileSize
                              : requested;

        bytesRead = file.Read(preview, bytesToRead);
        if (bytesRead < 0) {
            file.Close();
            Debug_SetCursorPosition(0, 0);
            Debug_PrintString("File read failed.", false);
            WaitForUser();
            return;
        }

        preview[bytesRead] = '\0';
        for (int i = 0; i < bytesRead; ++i) {
            unsigned char c = (unsigned char)preview[i];
            if (c < 32 || c > 126) {
                preview[i] = ' ';
            } else if (textCase == 1 && c >= 'a' && c <= 'z') {
                preview[i] = (char)(c - ('a' - 'A'));
            } else if (textCase == 2 && c >= 'A' && c <= 'Z') {
                preview[i] = (char)(c + ('a' - 'A'));
            }
        }
    }

    // Close the file before waiting for input or drawing the result screen.
    file.Close();

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("File selected.", false);

    Debug_SetCursorPosition(0, 1);
    Debug_Printf(0, 1, false, 0, "Size: %u bytes",
                 (unsigned int)fileSize);

    Debug_SetCursorPosition(0, 2);
    if (showPreview) {
        Debug_PrintString("Text preview:", false);
        Debug_SetCursorPosition(0, 3);
        Debug_PrintString(preview, false);
    } else {
        Debug_PrintString("Preview disabled.", false);
    }

    WaitForUser();
}

static void ResetSettings(UI::Application &app) {
    showPreview = true;
    previewLength = 16;
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
        UI::FileFilter(".txt", "Text files")};
    config.filters = filters;
    config.filterCount = 1;

    UI::Setting settings[] = {
        UI::Setting::Boolean(
            "Show preview", &showPreview, "Enable file content preview"),
        UI::Setting::Integer(
            "Preview bytes", &previewLength, 4, 24, 4,
            "Number of file bytes to read"),
        UI::Setting::Enumeration(
            "Text case", &textCase, textCaseOptions, 3,
            "Preview letter case"),
        UI::Setting::Action(
            "Create sample", CreateSampleFile, "Write a sample text file"),
        UI::Setting::Action(
            "Reset settings", ResetSettings, "Restore default values")};
    config.showSettings = true;
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);


    // The UI library manages calcInit()/calcEnd() by default.
    // Keep this enabled unless the application manages VRAM itself.
    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
