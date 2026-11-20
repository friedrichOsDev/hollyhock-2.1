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

static bool demoEnabled = true;
static int demoLevel = 2;
static int demoMode = 0;
static const char *const demoModes[] = {
    "Beginner",
    "Normal",
    "Advanced"
};

static void WaitForUser() {
    Debug_SetCursorPosition(0, 4);
    Debug_PrintString("Press a number key to return.", false);
    LCD_Refresh();
    Debug_WaitKey();
}

static void ShowStorageInfo(UI::Application &app) {
    (void)app;

    char normalizedPath[128];
    int pathResult = UI::FileSystem::NormalizePath(
        SAMPLE_PATH, normalizedPath, sizeof(normalizedPath)
    );

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
        Debug_PrintString("Path normalization: OK", false);
    } else {
        Debug_PrintString("Path normalization: ERROR", false);
    }

    Debug_SetCursorPosition(0, 2);
    if (directoryResult == 0) {
        Debug_Printf(0, 2, false, 0, "Root directory entries: %u",
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
        Debug_PrintString("Could not open sample file.", false);
        WaitForUser();
        return;
    }

    int written = file.Write(SAMPLE_CONTENT, sizeof(SAMPLE_CONTENT) - 1);
    if (written < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Writing sample file failed.", false);
        WaitForUser();
        return;
    }

    if (written != (int)(sizeof(SAMPLE_CONTENT) - 1)) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Only part of the file was written.", false);
        WaitForUser();
        return;
    }

    int closeResult = file.Close();
    Debug_SetCursorPosition(0, 0);
    if (closeResult == 0) {
        Debug_PrintString("Sample file written.", false);
        Debug_SetCursorPosition(0, 1);
        Debug_PrintString("Open Browse and select it.", false);
    } else {
        Debug_PrintString("File written, close failed.", false);
    }
    WaitForUser();
}

static void FileSelected(UI::Application &app, const char *path) {
    (void)app;

    UI::File file;
    int result = file.Open(path, UI::FileRead);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Could not open selected file.", false);
        WaitForUser();
        return;
    }

    uint32_t fileSize = 0;
    result = file.GetSize(&fileSize);
    if (result < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Could not read file size.", false);
        WaitForUser();
        return;
    }

    char preview[65];
    int bytesToRead = fileSize < sizeof(preview) - 1
        ? (int)fileSize
        : (int)sizeof(preview) - 1;

    int bytesRead = file.Read(preview, bytesToRead);
    if (bytesRead < 0) {
        Debug_SetCursorPosition(0, 0);
        Debug_PrintString("Could not read selected file.", false);
        WaitForUser();
        return;
    }

    preview[bytesRead] = '\0';
    for (int i = 0; i < bytesRead; ++i) {
        unsigned char c = (unsigned char)preview[i];
        if (c < 32 || c > 126) {
            preview[i] = ' ';
        }
    }

    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("File selected successfully", false);

    Debug_SetCursorPosition(0, 1);
    Debug_Printf(0, 1, false, 0, "Size: %u bytes",
                 (unsigned int)fileSize);

    Debug_SetCursorPosition(0, 2);
    Debug_PrintString("First bytes:", false);

    Debug_SetCursorPosition(0, 3);
    Debug_PrintString(preview, false);

    // The File destructor closes the descriptor when this callback returns.
    WaitForUser();
}

static void ResetSettings(UI::Application &app) {
    demoEnabled = true;
    demoLevel = 2;
    demoMode = 0;
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
        UI::Setting::Boolean(
            "Demo enabled", &demoEnabled, "A Boolean setting"
        ),
        UI::Setting::Integer(
            "Demo level", &demoLevel, 0, 10, 2, "An integer setting"
        ),
        UI::Setting::Enumeration(
            "Mode", &demoMode, demoModes, 3, "An enumeration setting"
        ),
        UI::Setting::Action(
            "Create sample", CreateSampleFile, "Write a sample text file"
        ),
        UI::Setting::Action(
            "Reset settings", ResetSettings, "Restore default values"
        )
    };
    config.showSettings = true;
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);

    // The UI library manages calcInit()/calcEnd() by default.
    // Keep this enabled unless the application manages VRAM itself.
    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
