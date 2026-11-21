#include "demo.hpp"

namespace {

enum { DEMO_ROWS = 5, PREVIEW_BUFFER_SIZE = 65 };

const char *const kDashboardItems[DEMO_ROWS] = {
    "Storage diagnostics",
    "Input event monitor",
    "Framebuffer drawing",
    "Return to main menu",
    "Exit application"
};

int Min(int left, int right) {
    return left < right ? left : right;
}

void CopyText(char *destination, const char *source, int capacity) {
    if (!destination || capacity <= 0)
        return;
    int index = 0;
    if (source) {
        while (source[index] && index < capacity - 1) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = 0;
}

void AppendText(char *destination, const char *source, int capacity) {
    int length = 0;
    while (destination[length] && length < capacity)
        ++length;
    if (length < capacity - 1)
        CopyText(destination + length, source, capacity - length);
}

void FormatUnsigned(unsigned int value, char *output, int capacity) {
    if (!output || capacity <= 0)
        return;

    char reversed[12];
    int count = 0;
    do {
        // Fixed-width binary long division by ten. This avoids emitting
        // SH-4 libgcc divide helpers in the freestanding demo executable.
        unsigned int quotient = 0;
        unsigned int remainder = 0;
        for (int bit = 31; bit >= 0; --bit) {
            remainder = (remainder << 1) | ((value >> bit) & 1U);
            quotient <<= 1;
            if (remainder >= 10U) {
                remainder -= 10U;
                quotient |= 1U;
            }
        }
        reversed[count++] = static_cast<char>('0' + remainder);
        value = quotient;
    } while (value != 0U && count < (int)sizeof(reversed));

    int index = 0;
    while (count > 0 && index < capacity - 1)
        output[index++] = reversed[--count];
    output[index] = 0;
}

int DivideNonNegative(int value, int divisor) {
    if (value <= 0 || divisor <= 0)
        return 0;
    int quotient = 0;
    while (value >= divisor) {
        value -= divisor;
        ++quotient;
    }
    return quotient;
}

void FormatSigned(int value, char *output, int capacity) {
    if (!output || capacity <= 0)
        return;
    if (value >= 0) {
        FormatUnsigned(static_cast<unsigned int>(value), output, capacity);
        return;
    }

    output[0] = '-';
    const unsigned int magnitude =
        static_cast<unsigned int>(-(value + 1)) + 1U;
    FormatUnsigned(magnitude, output + 1, capacity - 1);
}

const char *KeyName(uint16_t key) {
    switch (key) {
    case UI::KeyUp: return "UP";
    case UI::KeyDown: return "DOWN";
    case UI::KeyLeft: return "LEFT";
    case UI::KeyRight: return "RIGHT";
    case UI::KeyEXE: return "EXE";
    case UI::KeyBackspace: return "BACKSPACE";
    case UI::KeyPowerClear: return "POWER/CLEAR";
    case UI::KeyShift: return "SHIFT";
    case UI::KeyKeyboard: return "KEYBOARD";
    default: return "OTHER KEY";
    }
}

bool IsBackEvent(const UI::Event &event) {
    if (event.type == UI::EventSystem &&
        event.systemEvent == UI::SystemActionBarEscape)
        return true;
    return event.type == UI::EventKey &&
           event.keyAction == UI::KeyPressed &&
           (event.keyCode == UI::KeyBackspace ||
            event.keyCode == UI::KeyPowerClear);
}

void DrawHeader(UI::Framebuffer &screen, const char *title,
                const char *subtitle) {
    screen.Clear(UI::RGB(244, 247, 251));
    screen.FillRect(0, 0, screen.Width(), 38, UI::RGB(24, 34, 52));
    screen.DrawText(10, 9, title, UI::Colors::White);
    if (subtitle)
        screen.DrawText(10, 47, subtitle, UI::RGB(73, 88, 108));
    screen.DrawLine(10, 60, screen.Width() - 10, 60,
                    UI::RGB(190, 199, 210));
}

void DrawFooter(UI::Framebuffer &screen, const char *text) {
    screen.DrawLine(8, screen.Height() - 30,
                    screen.Width() - 8, screen.Height() - 30,
                    UI::RGB(190, 199, 210));
    screen.DrawText(10, screen.Height() - 19, text,
                    UI::RGB(73, 88, 108));
    screen.Present();
}

bool WaitForDismiss(UI::Framebuffer &screen) {
    while (true) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0)
            return false;
        if (result == 0)
            continue;
        if (IsBackEvent(event))
            return true;
        if (event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed)
            return true;
        if (event.type == UI::EventTouch &&
            event.touchAction == UI::TouchDown)
            return true;
    }
}

void ShowStorageDiagnostics() {
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;

    char normalized[128];
    const int normalizeResult = UI::FileSystem::NormalizePath(
        "fls0/", normalized, sizeof(normalized));
    const bool rootExists = UI::FileSystem::Exists("fls0/");

    int entryCount = 0;
    int directoryResult = 0;
    UI::Directory directory;
    directoryResult = directory.Open("fls0/");
    if (directoryResult == 0) {
        UI::DirectoryEntry entry;
        while (directory.Next(&entry) == 0) {
            if (entry.name[0] == '.' &&
                (entry.name[1] == 0 ||
                 (entry.name[1] == '.' && entry.name[2] == 0)))
                continue;
            ++entryCount;
        }
        directory.Close();
    }

    DrawHeader(screen, "STORAGE DIAGNOSTICS",
               "Read-only access through UI::FileSystem");
    screen.DrawText(12, 78,
        normalizeResult == 0 ? "Path normalization: OK" :
                               "Path normalization: FAILED",
        normalizeResult == 0 ? UI::Colors::Green : UI::Colors::Red);
    screen.DrawText(12, 102,
        rootExists ? "fls0 root: FOUND" : "fls0 root: NOT FOUND",
        rootExists ? UI::Colors::Green : UI::Colors::Red);

    screen.DrawText(12, 134, "Directory scan:", UI::Colors::Black);
    if (directoryResult == 0) {
        char count[16];
        FormatUnsigned(static_cast<unsigned int>(entryCount),
                       count, sizeof(count));
        screen.DrawText(150, 134, "OK", UI::Colors::Green);
        screen.DrawText(12, 156, "Visible entries:", UI::Colors::Black);
        screen.DrawText(150, 156, count, UI::Colors::Blue);
    } else {
        char error[20];
        FormatSigned(directoryResult, error, sizeof(error));
        screen.DrawText(12, 156, "SDK error:", UI::Colors::Black);
        screen.DrawText(150, 156, error, UI::Colors::Red);
    }

    screen.DrawText(12, 194, "The library does not modify files.",
                    UI::Colors::Black);
    DrawFooter(screen, "EXE / touch / BACK: return");
    WaitForDismiss(screen);
    screen.End();
}

void ShowInputMonitor() {
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;

    uint16_t lastKey = 0;
    int lastX = 0;
    int lastY = 0;
    int eventCount = 0;
    const char *lastEvent = "WAITING FOR INPUT";

    while (true) {
        DrawHeader(screen, "INPUT EVENT MONITOR",
                   "Press calculator keys or touch the display");

        screen.DrawText(12, 82, "Last event:", UI::Colors::Black);
        screen.DrawText(150, 82, lastEvent, UI::Colors::Blue);

        screen.DrawText(12, 112, "Key:", UI::Colors::Black);
        screen.DrawText(150, 112, KeyName(lastKey), UI::Colors::Blue);

        char xText[16];
        char yText[16];
        char countText[16];
        FormatSigned(lastX, xText, sizeof(xText));
        FormatSigned(lastY, yText, sizeof(yText));
        FormatUnsigned(static_cast<unsigned int>(eventCount),
                       countText, sizeof(countText));

        screen.DrawText(12, 142, "Touch X:", UI::Colors::Black);
        screen.DrawText(150, 142, xText, UI::Colors::Blue);
        screen.DrawText(12, 164, "Touch Y:", UI::Colors::Black);
        screen.DrawText(150, 164, yText, UI::Colors::Blue);
        screen.DrawText(12, 194, "Events:", UI::Colors::Black);
        screen.DrawText(150, 194, countText, UI::Colors::Blue);
        DrawFooter(screen, "BACK / action-bar Escape: return");

        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0 || IsBackEvent(event))
            break;
        if (result == 0)
            continue;

        ++eventCount;
        if (event.type == UI::EventKey) {
            lastKey = event.keyCode;
            lastEvent = "KEY";
        } else if (event.type == UI::EventTouch) {
            lastX = event.x;
            lastY = event.y;
            lastEvent = "TOUCH";
        } else if (event.type == UI::EventSystem) {
            lastEvent = "SYSTEM";
        } else {
            lastEvent = "UNKNOWN";
        }
    }

    screen.End();
}

void ShowDrawingShowcase() {
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;

    DrawHeader(screen, "FRAMEBUFFER DRAWING",
               "RGB565, rectangles, lines and the built-in 5x7 font");

    screen.FillRect(12, 78, 72, 42, UI::RGB(30, 105, 180));
    screen.DrawRect(12, 78, 72, 42, UI::Colors::Black);
    screen.FillRect(96, 78, 72, 42, UI::RGB(34, 150, 105));
    screen.DrawRect(96, 78, 72, 42, UI::Colors::Black);
    screen.FillRect(180, 78, 72, 42, UI::RGB(220, 145, 45));
    screen.DrawRect(180, 78, 72, 42, UI::Colors::Black);

    screen.DrawText(12, 142, "RGB565 COLOR BLOCKS", UI::Colors::Black);
    screen.DrawLine(12, 174, screen.Width() - 12, 174,
                    UI::RGB(30, 105, 180));
    screen.DrawLine(12, 184, screen.Width() - 12, 218,
                    UI::RGB(34, 150, 105));
    screen.DrawLine(screen.Width() - 12, 184, 12, 218,
                    UI::RGB(220, 75, 75));
    screen.DrawText(12, 236, "DRAWING IS DIRECTLY IN LCD VRAM",
                    UI::Colors::Black);
    screen.DrawText(12, 254, "PRESENT() REFRESHES THE DISPLAY",
                    UI::Colors::Blue);

    DrawFooter(screen, "EXE / touch / BACK: return");
    WaitForDismiss(screen);
    screen.End();
}

void DrawDashboard(UI::Framebuffer &screen, int selected) {
    DrawHeader(screen, "DEVELOPER DASHBOARD",
               "Custom screen callback using Framebuffer and Input");

    for (int index = 0; index < DEMO_ROWS; ++index) {
        const int y = 76 + index * 38;
        if (index == selected) {
            screen.FillRect(10, y, screen.Width() - 20, 32,
                            UI::RGB(39, 91, 151));
            screen.DrawText(18, y + 12, kDashboardItems[index],
                            UI::Colors::White);
        } else {
            screen.DrawRect(10, y, screen.Width() - 20, 32,
                            UI::RGB(178, 190, 204));
            screen.DrawText(18, y + 12, kDashboardItems[index],
                            UI::Colors::Black);
        }
    }

    DrawFooter(screen, "UP/DOWN + EXE or touch a row");
}

const char *SettingValue(const UI::Setting &setting,
                         char *output, int capacity) {
    CopyText(output, "", capacity);
    if (setting.type == UI::SettingAction) {
        CopyText(output, "RUN", capacity);
    } else if (!setting.value) {
        CopyText(output, "UNSET", capacity);
    } else if (setting.type == UI::SettingBoolean) {
        CopyText(output,
            *static_cast<const bool *>(setting.value) ? "ON" : "OFF",
            capacity);
    } else if (setting.type == UI::SettingInteger) {
        FormatSigned(*static_cast<const int *>(setting.value),
                     output, capacity);
    } else if (setting.type == UI::SettingEnum) {
        const int value = *static_cast<const int *>(setting.value);
        if (setting.options && value >= 0 &&
            value < setting.optionCount)
            CopyText(output, setting.options[value], capacity);
        else
            CopyText(output, "INVALID", capacity);
    }
    return output;
}

void DrawCustomSettings(UI::Framebuffer &screen,
                        UI::Setting *settings, int count, int selected) {
    DrawHeader(screen, "CUSTOM SETTINGS",
               "This screen is drawn by the demo application");

    const int visible = Min(count, 5);
    for (int index = 0; index < visible; ++index) {
        const UI::Setting &setting = settings[index];
        char value[32];
        SettingValue(setting, value, sizeof(value));

        char label[96];
        CopyText(label, setting.name ? setting.name : "SETTING",
                 sizeof(label));
        AppendText(label, " : ", sizeof(label));
        AppendText(label, value, sizeof(label));

        const int y = 76 + index * 34;
        if (index == selected) {
            screen.FillRect(10, y, screen.Width() - 20, 29,
                            UI::RGB(39, 91, 151));
            screen.DrawText(16, y + 10, label, UI::Colors::White);
        } else {
            screen.DrawRect(10, y, screen.Width() - 20, 29,
                            UI::RGB(178, 190, 204));
            screen.DrawText(16, y + 10, label, UI::Colors::Black);
        }
    }

    const UI::Setting &current = settings[selected];
    if (current.description && current.description[0])
        screen.DrawText(10, screen.Height() - 56,
                        current.description, UI::RGB(73, 88, 108));
    DrawFooter(screen, "LEFT/RIGHT: change  EXE: select  BACK: menu");
}

} // namespace

void DemoResetSettings(UI::Application &app) {
    g_demoSettings.showPreview = true;
    g_demoSettings.previewBytes = 32;
    g_demoSettings.letterCase = 0;
    g_demoSettings.showFileSize = true;
    app.ReturnToMenu();
}

void DemoStart(UI::Application &app) {
    UI::Framebuffer screen;
    if (!screen.Begin()) {
        app.Close();
        return;
    }

    int selected = 0;
    DrawDashboard(screen, selected);

    while (app.IsRunning()) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0) {
            app.Close();
            break;
        }
        if (result == 0)
            continue;
        if (IsBackEvent(event)) {
            screen.End();
            return;
        }

        bool activate = false;
        if (event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed) {
            if (event.keyCode == UI::KeyUp) {
                selected = selected == 0 ? DEMO_ROWS - 1 : selected - 1;
            } else if (event.keyCode == UI::KeyDown) {
                ++selected;
                if (selected >= DEMO_ROWS)
                    selected = 0;
            } else if (event.keyCode == UI::KeyEXE) {
                activate = true;
            }
        } else if (event.type == UI::EventTouch &&
                   event.touchAction == UI::TouchDown) {
            const int row = DivideNonNegative(event.y - 76, 38);
            if (event.y >= 76 && row >= 0 && row < DEMO_ROWS) {
                selected = row;
                activate = true;
            }
        }

        if (!activate) {
            DrawDashboard(screen, selected);
            continue;
        }

        if (selected == 0) {
            screen.End();
            ShowStorageDiagnostics();
            if (!screen.Begin()) {
                app.Close();
                return;
            }
        } else if (selected == 1) {
            screen.End();
            ShowInputMonitor();
            if (!screen.Begin()) {
                app.Close();
                return;
            }
        } else if (selected == 2) {
            screen.End();
            ShowDrawingShowcase();
            if (!screen.Begin()) {
                app.Close();
                return;
            }
        } else if (selected == 3) {
            screen.End();
            return;
        } else {
            screen.End();
            app.Close();
            return;
        }

        DrawDashboard(screen, selected);
    }

    screen.End();
}

void DemoFileSelected(UI::Application &app, const char *path) {
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
    int bytesRead = -1;
    char preview[PREVIEW_BUFFER_SIZE];
    preview[0] = 0;

    if (openResult >= 0) {
        sizeResult = file.GetSize(&fileSize);
        if (sizeResult == 0 && g_demoSettings.showPreview) {
            int requested = g_demoSettings.previewBytes;
            if (requested < 8)
                requested = 8;
            if (requested > PREVIEW_BUFFER_SIZE - 1)
                requested = PREVIEW_BUFFER_SIZE - 1;
            int bytesToRead = requested;
            if (fileSize < static_cast<uint32_t>(requested))
                bytesToRead = static_cast<int>(fileSize);
            bytesRead = file.Read(preview, bytesToRead);
            if (bytesRead >= 0 && bytesRead <= bytesToRead) {
                preview[bytesRead] = 0;
                for (int index = 0; index < bytesRead; ++index) {
                    const unsigned char character =
                        static_cast<unsigned char>(preview[index]);
                    if (character < 32 || character > 126) {
                        preview[index] = ' ';
                    } else if (g_demoSettings.letterCase == 1 &&
                               character >= 'a' && character <= 'z') {
                        preview[index] =
                            static_cast<char>(character - ('a' - 'A'));
                    } else if (g_demoSettings.letterCase == 2 &&
                               character >= 'A' && character <= 'Z') {
                        preview[index] =
                            static_cast<char>(character + ('a' - 'A'));
                    }
                }
            }
        }
        file.Close();
    }

    DrawHeader(screen, "FILE SELECTION",
               "Native file selector -> custom framebuffer result view");
    screen.DrawText(10, 78, path, UI::Colors::Blue);

    if (statResult == 0)
        screen.DrawText(10, 104, "File metadata: OK", UI::Colors::Green);
    else
        screen.DrawText(10, 104, "File metadata: ERROR", UI::Colors::Red);

    if (openResult < 0) {
        screen.DrawText(10, 132, "Could not open selected file",
                        UI::Colors::Red);
    } else if (sizeResult < 0) {
        screen.DrawText(10, 132, "Could not read file size",
                        UI::Colors::Red);
    } else {
        screen.DrawText(10, 132, "Read-only open: OK", UI::Colors::Green);
        if (g_demoSettings.showFileSize) {
            char sizeText[20];
            FormatUnsigned(fileSize, sizeText, sizeof(sizeText));
            screen.DrawText(10, 158, "Size in bytes:", UI::Colors::Black);
            screen.DrawText(150, 158, sizeText, UI::Colors::Blue);
        }

        if (!g_demoSettings.showPreview) {
            screen.DrawText(10, 190, "Preview disabled in settings",
                            UI::Colors::Gray);
        } else if (bytesRead < 0) {
            screen.DrawText(10, 190, "Preview read failed",
                            UI::Colors::Red);
        } else {
            screen.DrawText(10, 190, "Preview:", UI::Colors::Black);
            screen.DrawText(10, 210, preview, UI::Colors::Black);
        }
    }

    DrawFooter(screen, "EXE / touch / BACK: return to menu");
    WaitForDismiss(screen);
    screen.End();
}

void DemoCustomSettings(UI::Application &app) {
    UI::Setting *settings = app.GetSettings();
    const int count = app.GetSettingCount();
    if (!settings || count <= 0) {
        app.ReturnToMenu();
        return;
    }

    UI::Framebuffer screen;
    if (!screen.Begin()) {
        app.ReturnToMenu();
        return;
    }

    int selected = 0;
    while (app.IsRunning()) {
        if (selected < 0)
            selected = 0;
        if (selected >= count)
            selected = count - 1;

        DrawCustomSettings(screen, settings, count, selected);

        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0 || IsBackEvent(event)) {
            screen.End();
            app.ReturnToMenu();
            return;
        }
        if (result == 0)
            continue;

        bool activate = false;
        int adjustment = 0;
        if (event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed) {
            if (event.keyCode == UI::KeyUp) {
                selected = selected == 0 ? count - 1 : selected - 1;
            } else if (event.keyCode == UI::KeyDown) {
                ++selected;
                if (selected >= count)
                    selected = 0;
            } else if (event.keyCode == UI::KeyLeft) {
                adjustment = -1;
            } else if (event.keyCode == UI::KeyRight) {
                adjustment = 1;
            } else if (event.keyCode == UI::KeyEXE) {
                activate = true;
            }
        } else if (event.type == UI::EventTouch &&
                   event.touchAction == UI::TouchDown) {
            const int visible = Min(count, 5);
            if (event.y >= 76 && event.y < 76 + visible * 34) {
                const int row = DivideNonNegative(event.y - 76, 34);
                if (row >= 0 && row < visible) {
                    selected = row;
                    activate = true;
                }
            } else if (event.y >= screen.Height() - 30) {
                screen.End();
                app.ReturnToMenu();
                return;
            }
        }

        if (activate && settings[selected].type == UI::SettingAction) {
            screen.End();
            app.AdjustSetting(selected, 1);
            return;
        }

        if (adjustment != 0 || activate)
            app.AdjustSetting(selected, adjustment == 0 ? 1 : adjustment);
    }

    screen.End();
}
