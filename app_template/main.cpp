#include <appdef.hpp>
#include <ui/ui.hpp>

APP_NAME("Hollyhock UI Library Demo")
APP_DESCRIPTION("Custom framebuffer UI with touch, keys, files and settings")
APP_AUTHOR("Friedrich")
APP_VERSION("1.3")

static bool showPreview = true;
static int previewLength = 24;
static int textCase = 0;
static const char *const textCaseOptions[] = {
    "Original",
    "Uppercase",
    "Lowercase"
};

static const char *const menuItems[] = {
    "Storage check",
    "Browse files",
    "Settings",
    "Exit"
};

static bool WaitForDismiss() {
    while (true) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0)
            return false;
        if (result == 0)
            continue;

        if (event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed)
            return true;
        if (event.type == UI::EventTouch &&
            event.touchAction == UI::TouchDown)
            return true;
        if (event.type == UI::EventSystem &&
            event.systemEvent == UI::SystemActionBarEscape)
            return true;
    }
}

static void DrawPageHeader(UI::Framebuffer &screen, const char *title) {
    screen.Clear(UI::Colors::White);
    screen.FillRect(0, 0, screen.Width(), 28, UI::Colors::Black);
    screen.DrawText(8, 10, title, UI::Colors::White);
    screen.DrawLine(8, 34, screen.Width() - 8, 34, UI::Colors::Gray);
}

static void ShowStorageInfo(UI::Framebuffer &screen) {
    char normalizedPath[128];
    const int pathResult = UI::FileSystem::NormalizePath(
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
                   (entry.name[1] == '.' && entry.name[2] == 0))))
                ++entryCount;
        }
        const int closeResult = directory.Close();
        if (closeResult < 0)
            directoryResult = closeResult;
    }

    DrawPageHeader(screen, "Storage check");
    screen.DrawText(10, 48, pathResult == 0
        ? "Path normalization: OK" : "Path normalization: FAIL",
        pathResult == 0 ? UI::Colors::Green : UI::Colors::Red);
    screen.DrawText(10, 66, UI::FileSystem::Exists("fls0/")
        ? "fls0 root: FOUND" : "fls0 root: MISSING",
        UI::Colors::Black);

    if (directoryResult == 0) {
        screen.DrawText(10, 84, "Directory entries:", UI::Colors::Black);
        screen.DrawText(150, 84, "OK", UI::Colors::Green);
        screen.DrawText(10, 100, "Count:", UI::Colors::Black);
        char count[12];
        int value = entryCount;
        int digits = 0;
        do {
            count[digits++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value > 0 && digits < 10);
        for (int i = 0; i < digits / 2; ++i) {
            const char swap = count[i];
            count[i] = count[digits - i - 1];
            count[digits - i - 1] = swap;
        }
        count[digits] = 0;
        screen.DrawText(70, 100, count, UI::Colors::Blue);
    } else {
        screen.DrawText(10, 84, "Directory scan failed", UI::Colors::Red);
    }
    screen.DrawText(10, screen.Height() - 18,
                    "Press any key or touch to return", UI::Colors::Gray);
    screen.Present();
    WaitForDismiss();
}

static void FileSelected(UI::Application &app, const char *path) {
    (void)app;
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;

    struct stat info;
    const int statResult = UI::FileSystem::Stat(path, &info);

    UI::File file;
    const int openResult = file.Open(path);
    uint32_t fileSize = 0;
    int sizeResult = openResult;
    int seekResult = openResult;
    char preview[49];
    int bytesRead = -1;
    preview[0] = 0;

    if (openResult >= 0) {
        sizeResult = file.GetSize(&fileSize);
        seekResult = file.Seek(0, SEEK_SET);
        if (sizeResult == 0 && seekResult >= 0 && showPreview) {
            int requested = previewLength;
            if (requested < 8)
                requested = 8;
            if (requested > 48)
                requested = 48;
            const int bytesToRead =
                fileSize < static_cast<uint32_t>(requested)
                    ? static_cast<int>(fileSize) : requested;
            bytesRead = file.Read(preview, bytesToRead);
            if (bytesRead >= 0 && bytesRead <= bytesToRead) {
                preview[bytesRead] = 0;
                for (int i = 0; i < bytesRead; ++i) {
                    const unsigned char c =
                        static_cast<unsigned char>(preview[i]);
                    if (c < 32 || c > 126)
                        preview[i] = ' ';
                    else if (textCase == 1 && c >= 'a' && c <= 'z')
                        preview[i] = static_cast<char>(c - ('a' - 'A'));
                    else if (textCase == 2 && c >= 'A' && c <= 'Z')
                        preview[i] = static_cast<char>(c + ('a' - 'A'));
                }
            } else {
                bytesRead = -1;
            }
        }
        file.Close();
    }

    DrawPageHeader(screen, "Selected file");
    screen.DrawText(8, 42, path, UI::Colors::Blue);
    if (statResult == 0)
        screen.DrawText(8, 58, "File metadata: available", UI::Colors::Green);
    else
        screen.DrawText(8, 58, "File metadata: error", UI::Colors::Red);

    if (openResult < 0) {
        screen.DrawText(8, 76, "Open failed", UI::Colors::Red);
    } else if (sizeResult < 0 || seekResult < 0) {
        screen.DrawText(8, 76, "Could not inspect file", UI::Colors::Red);
    } else {
        screen.DrawText(8, 76, "Read-only open: OK", UI::Colors::Green);
        screen.DrawText(8, 92, "Preview:", UI::Colors::Black);
        if (!showPreview)
            screen.DrawText(8, 108, "Preview disabled", UI::Colors::Gray);
        else if (bytesRead < 0)
            screen.DrawText(8, 108, "Read failed", UI::Colors::Red);
        else
            screen.DrawText(8, 108, preview, UI::Colors::Black);
    }

    screen.DrawText(8, screen.Height() - 18,
                    "Press any key or touch to return", UI::Colors::Gray);
    screen.Present();
    WaitForDismiss();
}

static void DrawMainMenu(UI::Framebuffer &screen, int selected) {
    DrawPageHeader(screen, "Hollyhock UI Demo");
    const int top = 48;
    const int rowHeight = 26;

    for (int i = 0; i < 4; ++i) {
        const int y = top + i * rowHeight;
        if (i == selected) {
            screen.FillRect(8, y, screen.Width() - 16, rowHeight - 2,
                            UI::Colors::Blue);
            screen.DrawText(16, y + 8, menuItems[i], UI::Colors::White);
        } else {
            screen.DrawRect(8, y, screen.Width() - 16, rowHeight - 2,
                            UI::Colors::Gray);
            screen.DrawText(16, y + 8, menuItems[i], UI::Colors::Black);
        }
    }

    screen.DrawText(8, screen.Height() - 18,
                    "UP/DOWN + EXE or touch a row", UI::Colors::Gray);
    screen.Present();
}

static void StartApplication(UI::Application &app) {
    UI::Framebuffer screen;
    if (!screen.Begin()) {
        app.Close();
        return;
    }

    int selected = 0;
    DrawMainMenu(screen, selected);

    while (app.IsRunning()) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0) {
            app.Close();
            return;
        }
        if (result == 0)
            continue;

        if (event.type == UI::EventSystem &&
            event.systemEvent == UI::SystemActionBarEscape) {
            app.Close();
            return;
        }

        bool activate = false;
        if (event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed) {
            if (event.keyCode == UI::KeyUp) {
                selected = (selected + 3) % 4;
            } else if (event.keyCode == UI::KeyDown) {
                selected = (selected + 1) % 4;
            } else if (event.keyCode == UI::KeyEXE) {
                activate = true;
            } else if (event.keyCode == UI::KeyPowerClear) {
                app.Close();
                return;
            }
        } else if (event.type == UI::EventTouch &&
                   event.touchAction == UI::TouchDown) {
            const int top = 48;
            const int rowHeight = 26;
            if (event.y >= top && event.y < top + 4 * rowHeight) {
                selected = (event.y - top) / rowHeight;
                activate = true;
            }
        }

        if (activate) {
            if (selected == 0) {
                ShowStorageInfo(screen);
            } else if (selected == 1) {
                app.OpenFileSelector();
                return;
            } else if (selected == 2) {
                app.OpenSettings();
                return;
            } else {
                app.Close();
                return;
            }
        }

        DrawMainMenu(screen, selected);
    }
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
    config.settingsFolder = "UIDEMO";

    // Use the library's lifecycle and persistence, but own the whole screen.
    config.showMainMenu = false;
    config.showStart = false;
    config.showFileSelector = true;
    config.showSettings = true;
    config.showQuit = false;

    config.onStart = StartApplication;
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
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);
    config.manageCalculatorLifecycle = true;

    UI::Application app(config);
    app.Run();
}
