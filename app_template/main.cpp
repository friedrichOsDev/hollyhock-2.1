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

    UI::Directory directory;
    int directoryResult = directory.Open("fls0/");
    int entryCount = 0;

    if (directoryResult == 0) {
        UI::DirectoryEntry entry;
        while (directory.Next(&entry) == 0) {
            ++entryCount;
        }
        directory.Close();
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("UI Library storage check", false);

    Debug_SetCursorPosition(0, 1);
    if (pathResult == 0) {
        Debug_PrintString(normalizedPath, false);
    } else {
        Debug_PrintString("Normalize path failed.", false);
    }

    Debug_SetCursorPosition(0, 2);
    if (directoryResult == 0) {
        Debug_Printf(0, 2, false, 0, "Entries found: %u",
                     (unsigned int)entryCount);
    } else {
        Debug_PrintString("Could not open fls0.", false);
    }

    Debug_SetCursorPosition(0, 3);
    Debug_PrintString("Directory handle closed.", false);
    WaitForUser();
}

static void CreateSampleFile(UI::Application &app) {
    (void)app;

    UI::File file;
    int result = file.Open(SAMPLE_PATH, UI::FileCreate);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Sample open failed.", false);
        WaitForUser();
        return;
    }

    int written = file.Write(SAMPLE_CONTENT, sizeof(SAMPLE_CONTENT) - 1);
    if (written < 0) {
        file.Close();
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Sample write failed.", false);
        WaitForUser();
        return;
    }

    if (written != (int)(sizeof(SAMPLE_CONTENT) - 1)) {
        file.Close();
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Short write occurred.", false);
        WaitForUser();
        return;
    }

    int closeResult = file.Close();
    Debug_SetCursorPosition(0, 0);
    if (closeResult == 0) {
        Debug_PrintString("Sample file written.", false);
        Debug_SetCursorPosition(0, 1);
        Debug_PrintString("Select it in Browse.", false);
    } else {
        Debug_PrintString("File close failed.", false);
    }
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

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 1", true);
    LCD_Refresh();
    Debug_WaitKey();

    UI::Config config;
    config.title = "Hollyhock UI Demo";
    config.startLabel = "Storage check";
    config.filesLabel = "Browse files";
    config.settingsLabel = "Settings";
    config.quitLabel = "Quit";

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 2", true);
    LCD_Refresh();
    Debug_WaitKey();

    config.showStart = true;
    config.onStart = ShowStorageInfo;

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 3", true);
    LCD_Refresh();
    Debug_WaitKey();

    config.showFileSelector = true;
    config.onFileSelected = FileSelected;

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 4", true);
    LCD_Refresh();
    Debug_WaitKey();

    const UI::FileFilter filters[] = {
        UI::FileFilter(".txt", "Text files")};
    config.filters = filters;
    config.filterCount = 1;

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 5", true);
    LCD_Refresh();
    Debug_WaitKey();

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

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Test String 6", true);
    LCD_Refresh();
    Debug_WaitKey();


    // The UI library manages calcInit()/calcEnd() by default.
    // Keep this enabled unless the application manages VRAM itself.
    config.manageCalculatorLifecycle = true;

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("Wait", true);
    LCD_Refresh();
    Debug_WaitKey();


    UI::Application app(config);
    app.Run();
}
