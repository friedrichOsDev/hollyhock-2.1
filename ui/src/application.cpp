#include <sdk/calc/calc.hpp>
#include <ui/filesystem.hpp>
#include <sdk/os/gui.hpp>
#include <ui/framebuffer.hpp>
#include <ui/input.hpp>
#include <sdk/os/mem.hpp>
#include <stddef.h>
#include <stdint.h>
#include <ui/application.hpp>

inline void *operator new(size_t, void *memory) noexcept {
    return memory;
}

inline void operator delete(void *, void *) noexcept {}

namespace UI {
namespace {
enum { E_START = 1, E_FILES = 2, E_SETTINGS = 3, E_QUIT = 4,
       E_BACK = 5, E_PREV = 6, E_NEXT = 7, E_UP = 8, E_ROW = 20,
       ROWS = 5, MAX_ENTRIES = 64, NAME_LEN = 128, PATH_LEN = 256,
       MAX_PERSISTED_SETTINGS = 32, PERSISTENCE_BUFFER_SIZE = 512 };

class EventDialog : public GUIDialog {
    int buttonIDs[12];
    int buttonCount;
    int event;
public:
    explicit EventDialog(const char *title)
        : GUIDialog(Height95, AlignCenter, title, KeyboardStateNone),
          buttonCount(0),
          event(0) {}
    int EventID() const {
        return event;
    }

    void RegisterButton(int id) {
        if (buttonCount < 12)
            buttonIDs[buttonCount++] = id;
    }
    int OnEvent(GUIDialog_Wrapped *d, GUIDialog_OnEvent_Data *e) override {
        if (e) {
            int id = e->GetEventID();
            for (int i = 0; i < buttonCount; ++i) {
                if (buttonIDs[i] == id) {
                    event = id;
                    return GUIDialog::DialogResultOK;
                }
            }
        }
        return GUIDialog::OnEvent(d, e);
    }
};

class Elements {
    void *mem[24];
    int used;
    static char *CopyText(const char *t) {
        char *copy = static_cast<char *>(malloc(256));
        if (!copy)
            return 0;

        int length = 0;
        if (t) {
            while (t[length] && length < 255) {
                copy[length] = t[length];
                ++length;
            }
        }
        copy[length] = 0;
        return copy;
    }
public:
    Elements()
        : used(0) {}

    ~Elements() {
        for (int i = 0; i < used; ++i)
            free(mem[i]);
    }
    void AddButton(EventDialog &dialog, uint16_t x, uint16_t y,
                   uint16_t right, uint16_t bottom, const char *text,
                   int id, bool enabled = true) {
        if (used > 22)
            return;

        void *p = malloc(sizeof(GUIButton));
        if (!p)
            return;

        char *stable = CopyText(text);
        if (!stable) {
            free(p);
            return;
        }
        GUIButton *button = new (p) GUIButton(
            x, y, right, bottom, stable, (uint16_t)id,
            enabled ? GUIButton::FlagEnabled : 0);
        mem[used++] = p;
        mem[used++] = stable;
        dialog.AddElement(*button);
        dialog.RegisterButton(id);
    }
    void AddLabel(EventDialog &d, int x, int y, const char *t) {
        if (used > 22)
            return;

        void *p = malloc(sizeof(GUILabel));
        if (!p)
            return;

        char *stable = CopyText(t);
        if (!stable) {
            free(p);
            return;
        }
        GUILabel *label = new (p) GUILabel(x, y, stable);
        mem[used++] = p;
        mem[used++] = stable;
        d.AddElement(*label);
    }
};

int Len(const char *s) {
    int length = 0;
    if (s) {
        while (s[length])
            ++length;
    }
    return length;
}
void Copy(char *d, const char *s, int cap) {
    if (!d || cap <= 0)
        return;
    int i = 0;
    if (s) {
        while (s[i] && i < cap - 1) {
            d[i] = s[i];
            ++i;
        }
    }
    d[i] = 0;
}
void Append(char *d, const char *s, int cap) {
    const int length = Len(d);
    if (length < cap - 1)
        Copy(d + length, s, cap - length);
}
void Number(int v, char *out, int cap) {
    if (cap <= 0)
        return;

    char reversed[16];
    int count = 0;
    const bool negative = v < 0;
    const unsigned int magnitude = negative
        ? (unsigned int)(-(v + 1)) + 1U
        : (unsigned int)v;
    unsigned int remaining = magnitude;
    do {
        reversed[count++] = char('0' + remaining % 10U);
        remaining /= 10U;
    } while (remaining && count < (int)sizeof(reversed));

    int position = 0;
    if (negative && position < cap - 1)
        out[position++] = '-';
    while (count && position < cap - 1)
        out[position++] = reversed[--count];
    out[position] = 0;
}
char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c + 32) : c;
}
bool Equal(const char *a, const char *b) {
    if (!a || !b)
        return false;

    while (*a && *b) {
        if (Lower(*a) != Lower(*b))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}
struct Entry {
    char name[NAME_LEN];
    bool dir;
};
int Compare(const char *a, const char *b) {
    while (*a && *b) {
        const char left = Lower(*a);
        const char right = Lower(*b);
        if (left != right)
            return left < right ? -1 : 1;
        ++a;
        ++b;
    }
    return *a == *b ? 0 : (*a ? 1 : -1);
}
void Sort(Entry *e, int n) {
    for (int i = 1; i < n; ++i) {
        Entry value = e[i];
        int j = i;
        while (j > 0) {
            Entry &p = e[j - 1];
            if (p.dir && !value.dir)
                break;
            if (p.dir == value.dir &&
                Compare(p.name, value.name) <= 0) {
                break;
            }

            e[j] = e[j - 1];
            --j;
        }
        e[j] = value;
    }
}
void ShowLoadingMessage() {
    Framebuffer screen;
    if (!screen.Begin())
        return;

    screen.Clear(Colors::White);
    const int width = screen.Width();
    const int boxWidth = width > 32 ? width - 32 : width;
    const int left = (width - boxWidth) / 2;
    screen.DrawRect(left, 20, boxWidth, 48, Colors::Black);
    screen.DrawText(left + 8, 30, "Scanning files...", Colors::Blue);
    screen.DrawText(left + 8, 46, "Please wait.", Colors::Black);
    screen.Present();
}
void ClearLoadingMessage() {
    Framebuffer screen;
    if (!screen.Begin())
        return;
    screen.Clear(Colors::White);
    screen.Present();
}
void Scan(const char *dir, const Config &cfg, Entry *e, int *count) {
    *count = 0;
    Directory directory;
    if (directory.Open(dir) < 0)
        return;
    DirectoryEntry item;
    while (directory.Next(&item) == 0) {
        const bool skip =
            Equal(item.name, ".") || Equal(item.name, "..");
        const bool matchesFilter =
            item.isDirectory ||
            FileFilterMatches(item.name, cfg.filters, cfg.filterCount);
        if (!skip && matchesFilter && *count < MAX_ENTRIES) {
            Copy(e[*count].name, item.name, NAME_LEN);
            e[*count].dir = item.isDirectory;
            ++*count;
        }
    }
    directory.Close();
    Sort(e, *count);
}
void Bounds(GUIDialog &dialog, int row, uint16_t *left,
            uint16_t *top, uint16_t *right, uint16_t *bottom) {
    *left = dialog.GetLeftX() + 8;
    *right = dialog.GetRightX() - 8;
    *top = dialog.GetTopY() + 8 + row * 42;
    *bottom = *top + 35;
}
void Button(Elements &elements, EventDialog &dialog, int row,
            const char *text, int id, bool enabled = true) {
    uint16_t x, y, right, bottom;
    Bounds(dialog, row, &x, &y, &right, &bottom);
    elements.AddButton(dialog, x, y, right, bottom, text, id, enabled);
}
void Bottom(Elements &elements, EventDialog &dialog, int x, int width,
            const char *text, int id) {
    const int y = dialog.GetBottomY() - 40;
    elements.AddButton(dialog, dialog.GetLeftX() + x, y,
                       dialog.GetLeftX() + x + width, y + 35, text, id);
}
void PathLabel(Elements &elements, EventDialog &dialog, const char *path) {
    elements.AddLabel(dialog, dialog.GetLeftX() + 8,
                      dialog.GetTopY() + 2, path);
}
void Join(const char *directory, const char *name,
          char *output, int capacity) {
    Copy(output, directory, capacity);
    const int length = Len(output);
    if (length && output[length - 1] != '\\')
        Append(output, "\\", capacity);
    Append(output, name, capacity);
}
void Parent(char *p) {
    const char *root = "\\fls0\\";
    const int rootLength = Len(root);
    int length = Len(p);
    if (length <= rootLength)
        return;
    if (p[length - 1] == '\\')
        --length;
    while (length > rootLength && p[length - 1] != '\\')
        --length;
    if (length < rootLength)
        length = rootLength;
    p[length] = 0;
}
void Value(const Setting &s, char *out, int cap) {
    out[0] = 0;
    if (s.type == SettingAction) {
        Copy(out, "Run", cap);
        return;
    }
    if (!s.value) {
        Copy(out, "(unset)", cap);
        return;
    }
    if (s.type == SettingBoolean) {
        Copy(out, *static_cast<bool *>(s.value) ? "On" : "Off", cap);
    } else if (s.type == SettingInteger) {
        Number(*static_cast<int *>(s.value), out, cap);
    } else if (s.type == SettingEnum) {
        int v = *static_cast<int *>(s.value);
        if (s.options && v >= 0 && v < s.optionCount)
            Copy(out, s.options[v], cap);
        else
            Number(v, out, cap);
    } else {
        Copy(out, "Run", cap);
    }
}

void Adjust(Setting &s, Application &app, int direction = 1) {
    if (s.type == SettingAction) {
        if (s.action)
            s.action(app);
        return;
    }
    if (!s.value)
        return;
    if (s.type == SettingBoolean) {
        bool *value = static_cast<bool *>(s.value);
        *value = !*value;
    } else if (s.type == SettingInteger) {
        int *v = static_cast<int *>(s.value);
        if (s.minimum > s.maximum)
            return;

        if (*v < s.minimum || *v > s.maximum) {
            *v = s.minimum;
            return;
        }

        const int step = s.step > 0 ? s.step : 1;
        if (direction < 0) {
            const unsigned int distance =
                static_cast<unsigned int>(*v) -
                static_cast<unsigned int>(s.minimum);
            if (*v < s.minimum || *v > s.maximum ||
                static_cast<unsigned int>(step) > distance)
                *v = s.maximum;
            else
                *v -= step;
        } else {
            const unsigned int distance =
                static_cast<unsigned int>(s.maximum) -
                static_cast<unsigned int>(*v);
            if (static_cast<unsigned int>(step) > distance)
                *v = s.minimum;
            else
                *v += step;
        }
    } else if (s.type == SettingEnum &&
               s.options && s.optionCount > 0) {
        int *v = static_cast<int *>(s.value);
        if (direction < 0) {
            if (*v <= 0 || *v >= s.optionCount)
                *v = s.optionCount - 1;
            else
                --*v;
        } else {
            if (*v < 0 || *v >= s.optionCount || *v == s.optionCount - 1)
                *v = 0;
            else
                ++*v;
        }
    }
}

// Versioned ASCII storage format: UIST1;<count>;<type><value>;...
// Action entries are represented by A; because they have no persistent value.
bool AddChar(char *out, int *position, char c) {
    if (*position >= PERSISTENCE_BUFFER_SIZE - 1)
        return false;
    out[(*position)++] = c;
    out[*position] = 0;
    return true;
}
bool AddText(char *out, int *position, const char *text) {
    while (*text) {
        if (!AddChar(out, position, *text++))
            return false;
    }
    return true;
}
bool AddUnsigned(char *out, int *position, unsigned int value) {
    char digits[12];
    int count = 0;
    do {
        digits[count++] = char('0' + value % 10U);
        value /= 10U;
    } while (value && count < (int)sizeof(digits));
    while (count) {
        if (!AddChar(out, position, digits[--count]))
            return false;
    }
    return true;
}
bool AddSigned(char *out, int *position, int value) {
    const bool negative = value < 0;
    const unsigned int magnitude = negative
        ? (unsigned int)(-(value + 1)) + 1U
        : (unsigned int)value;
    if (negative && !AddChar(out, position, '-'))
        return false;
    return AddUnsigned(out, position, magnitude);
}

bool ValidSettingDefinition(const Setting &setting) {
    switch (setting.type) {
    case SettingBoolean:
        return setting.value != 0;
    case SettingInteger:
        return setting.value != 0 &&
               setting.minimum <= setting.maximum &&
               setting.step > 0;
    case SettingEnum:
        return setting.value != 0 &&
               setting.options != 0 &&
               setting.optionCount > 0;
    case SettingAction:
        return true;
    default:
        return false;
    }
}

bool ValidSettingValue(const Setting &setting) {
    if (!ValidSettingDefinition(setting))
        return false;

    if (setting.type == SettingInteger) {
        const int value = *static_cast<const int *>(setting.value);
        return value >= setting.minimum && value <= setting.maximum;
    }
    if (setting.type == SettingEnum) {
        const int value = *static_cast<const int *>(setting.value);
        return value >= 0 && value < setting.optionCount;
    }
    return true;
}

bool ValidSettingDefinitions(const Setting *settings, int count) {
    if (!settings || count <= 0 || count > MAX_PERSISTED_SETTINGS)
        return false;

    for (int i = 0; i < count; ++i) {
        if (!ValidSettingDefinition(settings[i]))
            return false;
    }
    return true;
}
bool SerializeSettings(const Setting *settings, int count,
                        char *out, uint32_t *size) {
    if (!ValidSettingDefinitions(settings, count) || !out || !size)
        return false;
    int position = 0;
    out[0] = 0;
    if (!AddText(out, &position, "UIST1;") ||
        !AddUnsigned(out, &position, (unsigned int)count) ||
        !AddChar(out, &position, ';')) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const Setting &setting = settings[i];
        if (!ValidSettingValue(setting))
            return false;
        if (setting.type == SettingAction) {
            if (!AddText(out, &position, "A;"))
                return false;
            continue;
        }
        if (!setting.value)
            return false;
        if (setting.type == SettingBoolean) {
            if (!AddChar(out, &position, 'B') ||
                !AddSigned(out, &position,
                           *static_cast<bool *>(setting.value) ? 1 : 0) ||
                !AddChar(out, &position, ';')) {
                return false;
            }
        } else if (setting.type == SettingInteger) {
            if (!AddChar(out, &position, 'I') ||
                !AddSigned(out, &position,
                           *static_cast<int *>(setting.value)) ||
                !AddChar(out, &position, ';')) {
                return false;
            }
        } else if (setting.type == SettingEnum) {
            if (!AddChar(out, &position, 'E') ||
                !AddSigned(out, &position,
                           *static_cast<int *>(setting.value)) ||
                !AddChar(out, &position, ';')) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!AddChar(out, &position, 0))
        return false;
    *size = (uint32_t)position;
    return true;
}
bool ParseSigned(const char **cursor, int *value) {
    const char *p = *cursor;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    if (*p < '0' || *p > '9')
        return false;
    unsigned int number = 0;
    const unsigned int limit = negative ? 2147483648U : 2147483647U;
    while (*p >= '0' && *p <= '9') {
        unsigned int digit = (unsigned int)(*p - '0');
        if (number > (limit - digit) / 10U)
            return false;
        number = number * 10U + digit;
        ++p;
    }
    if (*p != ';')
        return false;
    ++p;
    if (negative) {
        *value = number == 2147483648U
            ? (-2147483647 - 1)
            : -(int)number;
    } else {
        *value = (int)number;
    }
    *cursor = p;
    return true;
}
bool ParseSettings(const char *data, const Setting *settings, int count) {
    if (!data || !ValidSettingDefinitions(settings, count))
        return false;
    const char *p = data;
    const char prefix[] = "UIST1;";
    for (int i = 0; i < 6; ++i) {
        if (*p++ != prefix[i])
            return false;
    }
    int storedCount = 0;
    if (!ParseSigned(&p, &storedCount) || storedCount != count)
        return false;

    int values[MAX_PERSISTED_SETTINGS];
    for (int i = 0; i < count; ++i) {
        const Setting &setting = settings[i];
        char type = *p++;
        if (type == 0)
            return false;
        if (setting.type == SettingAction) {
            if (type != 'A' || *p++ != ';')
                return false;
            values[i] = 0;
            continue;
        }
        int value = 0;
        if (!ParseSigned(&p, &value))
            return false;
        if ((setting.type == SettingBoolean && (type != 'B' || (value != 0 && value != 1))) ||
            (setting.type == SettingInteger &&
             (type != 'I' || value < setting.minimum ||
              value > setting.maximum)) ||
            (setting.type == SettingEnum &&
             (type != 'E' || value < 0 ||
              value >= setting.optionCount)))
            return false;
        if (!setting.value)
            return false;
        values[i] = value;
    }
    if (*p != 0)
        return false;
    for (int i = 0; i < count; ++i) {
        if (settings[i].type == SettingBoolean)
            *static_cast<bool *>(settings[i].value) = values[i] != 0;
        else if (settings[i].type == SettingInteger ||
                 settings[i].type == SettingEnum) {
            *static_cast<int *>(settings[i].value) = values[i];
        }
    }
    return true;
}
} // namespace

Application::Application(const Config &config)
    : m_config(config),
      m_state(StateMainMenu),
      m_lifecycleStarted(false),
      m_persistenceInitialized(false),
      m_persistenceReady(false),
      m_persistenceResult(0) {
    Copy(m_currentDirectory, "\\fls0\\", sizeof(m_currentDirectory));
}
Application::~Application() {
    if (m_lifecycleStarted && m_config.manageCalculatorLifecycle) {
        calcEnd();
        m_lifecycleStarted = false;
    }
}
void Application::InitializeSettingsPersistence() {
    m_persistenceInitialized = true;
    if (!m_config.settingsFolder || !m_config.settingsFolder[0])
        return;

    int result = m_settingsStore.Open(m_config.settingsFolder);
    // MCS errors are positive constants (unlike file-system errors).
    if (result != 0) {
        m_persistenceResult = result;
        return;
    }
    if (!m_config.settings || m_config.settingCount <= 0) {
        m_persistenceReady = true;
        return;
    }

    // Save() rounds data up to a four-byte boundary. A valid 511-byte
    // payload can therefore occupy 512 bytes in MCS, plus a local terminator.
    char buffer[PERSISTENCE_BUFFER_SIZE + 1];
    uint8_t type = 0;
    uint32_t size = 0;
    result = m_settingsStore.Load("SETTINGS", &type, buffer,
                                  PERSISTENCE_BUFFER_SIZE, &size);
    if (result == MCS_NO_VARIABLE) {
        // Persist the application's defaults immediately. This makes the
        // SETTINGS variable visible on the first launch and ensures that
        // a reset or power loss before the first settings interaction does
        // not leave the application without an initial saved state.
        m_persistenceReady = true;
        m_persistenceResult = 0;
        SaveSettingsToStorage();
        return;
    }
    if (result != 0) {
        m_persistenceResult = result;
        return;
    }
    if (type != VARTYPE_STR || size == 0 ||
        size > PERSISTENCE_BUFFER_SIZE) {
        m_persistenceResult = EUNFORMAT;
        return;
    }
    buffer[size] = 0;
    if (!ParseSettings(buffer, m_config.settings, m_config.settingCount)) {
        m_persistenceResult = EUNFORMAT;
        return;
    }
    m_persistenceReady = true;
    m_persistenceResult = 0;
}
void Application::LoadSettingsFromStorage() {
    InitializeSettingsPersistence();
}
void Application::SaveSettingsToStorage() {
    if (!m_settingsStore.IsOpen() || !m_config.settings ||
        m_config.settingCount <= 0) {
        return;
    }
    char buffer[PERSISTENCE_BUFFER_SIZE];
    uint32_t size = 0;
    if (!ValidSettingDefinitions(m_config.settings, m_config.settingCount)) {
        m_persistenceResult = EINVAL;
        return;
    }
    for (int i = 0; i < m_config.settingCount; ++i) {
        if (!ValidSettingValue(m_config.settings[i])) {
            m_persistenceResult = EINVAL;
            return;
        }
    }
    if (!SerializeSettings(m_config.settings, m_config.settingCount, buffer, &size)) {
        m_persistenceResult = EOUTOFBOUND;
        return;
    }
    m_persistenceResult = m_settingsStore.Save("SETTINGS", VARTYPE_STR, buffer, size);
}
int Application::SettingsPersistenceResult() const {
    return m_persistenceResult;
}

void Application::Run() {
    if (m_state == StateExit)
        return;

    if (m_config.manageCalculatorLifecycle && !m_lifecycleStarted) {
        calcInit();
        m_lifecycleStarted = true;
    }
    if (!m_persistenceInitialized) LoadSettingsFromStorage();

    while (m_state != StateExit) {
        switch (m_state) {
        case StateMainMenu:
            if (m_config.showMainMenu) {
                RunMainMenu();
            } else if (m_config.onStart) {
                // In custom-UI mode, onStart owns the screen until it
                // changes the application state or returns normally.
                m_config.onStart(*this);
                if (m_state == StateMainMenu)
                    m_state = StateExit;
            } else {
                m_state = StateExit;
            }
            break;
        case StateFileSelector:
            RunFileSelector();
            break;
        case StateSettings:
            RunSettings();
            break;
        default:
            m_state = StateExit;
            break;
        }
    }
    if (m_persistenceReady) SaveSettingsToStorage();
    if (m_lifecycleStarted && m_config.manageCalculatorLifecycle) {
        calcEnd();
        m_lifecycleStarted = false;
    }
}
void Application::Close() {
    m_state = StateExit;
}

void Application::ReturnToMenu() {
    m_state = StateMainMenu;
}

void Application::OpenFileSelector() {
    if (m_config.showFileSelector)
        m_state = StateFileSelector;
}

void Application::OpenSettings() {
    if (m_config.showSettings)
        m_state = StateSettings;
}

int Application::SelectFile(const char *path) {
    if (m_state != StateFileSelector)
        return EINVAL;

    char normalized[PATH_LEN];
    const int result =
        FileSystem::NormalizePath(path, normalized, sizeof(normalized));
    if (result < 0)
        return result;

    if (m_config.onFileSelected)
        m_config.onFileSelected(*this, normalized);

    // A callback may explicitly open another menu or close the application.
    if (m_state == StateFileSelector)
        ReturnToMenu();
    return 0;
}

const char *Application::CurrentDirectory() const {
    return m_currentDirectory;
}

int Application::SetCurrentDirectory(const char *path) {
    char normalized[PATH_LEN];
    const int result =
        FileSystem::NormalizePath(path, normalized, sizeof(normalized));
    if (result < 0)
        return result;

    const int length = Len(normalized);
    if (length > 0 && normalized[length - 1] != '\\') {
        if (length + 1 >= (int)sizeof(normalized))
            return ENAMETOOLONG;
        normalized[length] = '\\';
        normalized[length + 1] = 0;
    }
    Copy(m_currentDirectory, normalized, sizeof(m_currentDirectory));
    return 0;
}

Setting *Application::GetSettings() {
    return m_config.settings;
}

int Application::GetSettingCount() const {
    return m_config.settingCount;
}

int Application::AdjustSetting(int index, int direction) {
    if (!m_config.settings || index < 0 ||
        index >= m_config.settingCount)
        return EINVAL;

    Setting &setting = m_config.settings[index];
    if (!ValidSettingDefinition(setting) || direction == 0)
        return EINVAL;
    if (setting.type == SettingAction && direction < 0)
        return 0;

    Adjust(setting, *this, direction < 0 ? -1 : 1);
    if (m_settingsStore.IsOpen()) {
        m_persistenceReady = true;
        SaveSettingsToStorage();
    }
    return m_persistenceResult;
}

bool Application::FileMatchesFilter(const char *filename) const {
    return FileFilterMatches(filename, m_config.filters,
                             m_config.filterCount);
}

bool Application::IsRunning() const {
    return m_state != StateExit;
}

void Application::RunNativeMainMenu() {
    Elements elements;
    EventDialog dialog(m_config.title ? m_config.title : "Application");
    int row = 0;
    if (m_config.showStart && m_config.onStart) {
        Button(elements, dialog, row++,
               m_config.startLabel ? m_config.startLabel : "Start", E_START);
    }
    if (m_config.showFileSelector) {
        Button(elements, dialog, row++,
               m_config.filesLabel ? m_config.filesLabel : "Files", E_FILES);
    }
    if (m_config.showSettings &&
        (m_config.settingCount > 0 || m_config.onSettings ||
         m_config.onCustomSettings)) {
        Button(elements, dialog, row++,
               m_config.settingsLabel ? m_config.settingsLabel : "Settings",
               E_SETTINGS);
    }
    if (m_config.showQuit) {
        Button(elements, dialog, row++,
               m_config.quitLabel ? m_config.quitLabel : "Quit", E_QUIT);
    }
    if (!row)
        Button(elements, dialog, row++, "Quit", E_QUIT);

    dialog.ShowDialog();
    const int event = dialog.EventID();
    if (event == E_START && m_config.onStart) {
        m_config.onStart(*this);
    } else if (event == E_FILES) {
        OpenFileSelector();
    } else if (event == E_SETTINGS) {
        OpenSettings();
    } else if (event == E_QUIT) {
        Close();
    }
}
void Application::RunNativeFileSelector() {
    Entry *entries = (Entry *)malloc(sizeof(Entry) * MAX_ENTRIES);
    if (!entries) {
        ReturnToMenu();
        return;
    }
    int count = 0;
    ShowLoadingMessage();
    Scan(m_currentDirectory, m_config, entries, &count);
    ClearLoadingMessage();
    int page = 0;
    bool done = false;
    while (!done && m_state == StateFileSelector) {
        Elements elements;
        EventDialog dialog("Select file");
        PathLabel(elements, dialog, m_currentDirectory);
        int pages = (count + ROWS - 1) / ROWS;
        if (page >= pages && pages > 0) page = pages - 1;
        if (page < 0) page = 0;
        int visible = count - page * ROWS;
        if (visible > ROWS)
            visible = ROWS;
        for (int row = 0; row < visible; ++row) {
            int index = page * ROWS + row;
            char label[NAME_LEN + 3];
            Copy(label, entries[index].name, sizeof(label));
            if (entries[index].dir) Append(label, "/", sizeof(label));
            Button(elements, dialog, row + 1, label, E_ROW + row);
        }
        Bottom(elements, dialog, 8, 38, "<", E_PREV);
        Bottom(elements, dialog, 50, 38, ">", E_NEXT);
        Bottom(elements, dialog, 92, 56, "Up", E_UP);
        Bottom(elements, dialog, 152, 60, "Back", E_BACK);

        dialog.ShowDialog();
        const int event = dialog.EventID();
        if (event == E_BACK) {
            ReturnToMenu();
            done = true;
        }
        else if (event == E_UP) {
            Parent(m_currentDirectory);
            ShowLoadingMessage();
            Scan(m_currentDirectory, m_config, entries, &count);
            ClearLoadingMessage();
            page = 0;
        } else if (event == E_PREV) {
            if (page > 0)
                --page;
        } else if (event == E_NEXT) {
            if ((page + 1) * ROWS < count)
                ++page;
        }
        else if (event >= E_ROW && event < E_ROW + ROWS) {
            const int index = page * ROWS + event - E_ROW;
            if (index < count) {
                if (entries[index].dir) {
                    char next[PATH_LEN];
                    Join(m_currentDirectory, entries[index].name,
                         next, sizeof(next));
                    Copy(m_currentDirectory, next,
                         sizeof(m_currentDirectory));
                    Append(m_currentDirectory, "\\",
                           sizeof(m_currentDirectory));
                    ShowLoadingMessage();
                    Scan(m_currentDirectory, m_config, entries, &count);
                    ClearLoadingMessage();
                    page = 0;
                } else {
                    char selected[PATH_LEN];
                    Join(m_currentDirectory, entries[index].name,
                         selected, sizeof(selected));
                    SelectFile(selected);
                    done = true;
                }
            }
        }
    }
    free(entries);
}
void Application::RunNativeSettings() {
    if (m_config.onSettings &&
        (!m_config.settings || m_config.settingCount <= 0)) {
        m_config.onSettings(*this);
        if (m_state == StateSettings)
            ReturnToMenu();
        return;
    }
    if (m_state != StateSettings)
        return;
    if (!m_config.settings || m_config.settingCount <= 0) {
        ReturnToMenu();
        return;
    }

    int page = 0;
    bool done = false;
    while (!done && m_state == StateSettings) {
        Elements elements;
        EventDialog dialog("Settings");
        int pages = (m_config.settingCount + ROWS - 1) / ROWS;
        if (page >= pages && pages > 0) page = pages - 1;
        if (page < 0) page = 0;
        int visible = m_config.settingCount - page * ROWS;
        if (visible > ROWS)
            visible = ROWS;
        for (int row = 0; row < visible; ++row) {
            int index = page * ROWS + row;
            Setting &setting = m_config.settings[index];
            char label[96];
            char value[32];
            Copy(label, setting.name ? setting.name : "Setting",
                 sizeof(label));
            Append(label, ": ", sizeof(label));
            Value(setting, value, sizeof(value));
            Append(label, value, sizeof(label));
            Button(elements, dialog, row, label, E_ROW + row);
        }
        Bottom(elements, dialog, 8, 38, "<", E_PREV);
        Bottom(elements, dialog, 50, 38, ">", E_NEXT);
        Bottom(elements, dialog, 94, 116, "Back", E_BACK);
        dialog.ShowDialog();
        const int event = dialog.EventID();
        if (event == E_BACK) {
            ReturnToMenu();
            done = true;
        } else if (event == E_PREV) {
            if (page > 0)
                --page;
        } else if (event == E_NEXT) {
            if ((page + 1) * ROWS < m_config.settingCount)
                ++page;
        }
        else if (event >= E_ROW && event < E_ROW + ROWS) {
            const int index = page * ROWS + event - E_ROW;
            if (index < m_config.settingCount) {
                AdjustSetting(index, 1);
            }
            if (m_state != StateSettings) done = true;
        }
    }
}

void Application::RunMainMenu() {
    if (m_config.mainMenuRenderer == MenuRenderer::Framebuffer) {
        RunFramebufferMainMenu();
        return;
    }
    if (m_config.mainMenuRenderer == MenuRenderer::Custom &&
        m_config.onCustomMainMenu) {
        m_config.onCustomMainMenu(*this);
        // Like onStart in legacy custom-screen mode, a custom main menu owns
        // its event loop. Returning without requesting a transition exits.
        if (m_state == StateMainMenu)
            m_state = StateExit;
        return;
    }
    // Custom without a callback safely falls back to the native menu.
    RunNativeMainMenu();
}

void Application::RunFileSelector() {
    if (m_config.fileSelectorRenderer == MenuRenderer::Framebuffer) {
        RunFramebufferFileSelector();
        return;
    }
    if (m_config.fileSelectorRenderer == MenuRenderer::Custom &&
        m_config.onCustomFileSelector) {
        m_config.onCustomFileSelector(*this);
        if (m_state == StateFileSelector)
            ReturnToMenu();
        if (m_settingsStore.IsOpen()) {
            m_persistenceReady = true;
            SaveSettingsToStorage();
        }
        return;
    }
    RunNativeFileSelector();
}

void Application::RunSettings() {
    if (m_config.settingsRenderer == MenuRenderer::Framebuffer) {
        RunFramebufferSettings();
        return;
    }
    if (m_config.settingsRenderer == MenuRenderer::Custom &&
        m_config.onCustomSettings) {
        m_config.onCustomSettings(*this);
        if (m_state == StateSettings)
            ReturnToMenu();
        if (m_settingsStore.IsOpen()) {
            m_persistenceReady = true;
            SaveSettingsToStorage();
        }
        return;
    }
    RunNativeSettings();
}

bool Application::RunFramebufferMainMenu() {
    Framebuffer screen;
    if (!screen.Begin()) {
        Close();
        return false;
    }

    enum { ACTION_START = 1, ACTION_FILES, ACTION_SETTINGS, ACTION_QUIT };
    const char *labels[4];
    int actions[4];
    int count = 0;

    if (m_config.showStart && m_config.onStart) {
        labels[count] = m_config.startLabel ? m_config.startLabel : "Start";
        actions[count++] = ACTION_START;
    }
    if (m_config.showFileSelector) {
        labels[count] = m_config.filesLabel ? m_config.filesLabel : "Files";
        actions[count++] = ACTION_FILES;
    }
    if (m_config.showSettings &&
        (m_config.settingCount > 0 || m_config.onSettings ||
         m_config.onCustomSettings)) {
        labels[count] = m_config.settingsLabel
            ? m_config.settingsLabel : "Settings";
        actions[count++] = ACTION_SETTINGS;
    }
    if (m_config.showQuit) {
        labels[count] = m_config.quitLabel ? m_config.quitLabel : "Quit";
        actions[count++] = ACTION_QUIT;
    }
    if (count == 0) {
        labels[count] = "Quit";
        actions[count++] = ACTION_QUIT;
    }

    int selected = 0;
    const int top = 54;
    while (m_state == StateMainMenu) {
        screen.Clear(Colors::White);
        screen.FillRect(0, 0, screen.Width(), 34, Colors::Black);
        screen.DrawText(8, 12,
            m_config.title ? m_config.title : "Application", Colors::White);
        screen.DrawLine(8, 42, screen.Width() - 8, 42, Colors::Gray);

        int rowHeight = 38;
        const int available = screen.Height() - top - 30;
        if (count > 0 && available / count < rowHeight)
            rowHeight = available / count;
        if (rowHeight < 18)
            rowHeight = 18;

        for (int i = 0; i < count; ++i) {
            const int y = top + i * rowHeight;
            if (i == selected) {
                screen.FillRect(8, y, screen.Width() - 16,
                                rowHeight - 3, Colors::Blue);
                screen.DrawText(16, y + (rowHeight - 7) / 2,
                                labels[i], Colors::White);
            } else {
                screen.DrawRect(8, y, screen.Width() - 16,
                                rowHeight - 3, Colors::Gray);
                screen.DrawText(16, y + (rowHeight - 7) / 2,
                                labels[i], Colors::Black);
            }
        }

        screen.DrawText(8, screen.Height() - 16,
                        "UP/DOWN + EXE or touch", Colors::Gray);
        screen.Present();

        bool activate = false;
        UI::Event event;
        while (m_state == StateMainMenu) {
            const int result = Input::Poll(&event);
            if (result < 0) {
                Close();
                break;
            }
            if (result == 0)
                continue;

            if (event.type == EventSystem &&
                event.systemEvent == SystemActionBarEscape) {
                Close();
                break;
            }
            if (event.type == EventKey &&
                event.keyAction == KeyPressed) {
                if (event.keyCode == KeyUp) {
                    selected = (selected + count - 1) % count;
                    break;
                }
                if (event.keyCode == KeyDown) {
                    selected = (selected + 1) % count;
                    break;
                }
                if (event.keyCode == KeyEXE) {
                    activate = true;
                    break;
                }
                if (event.keyCode == KeyPowerClear) {
                    Close();
                    break;
                }
            } else if (event.type == EventTouch &&
                       event.touchAction == TouchDown) {
                const int y = event.y;
                const int row = (y - top) / rowHeight;
                if (y >= top && row >= 0 && row < count) {
                    selected = row;
                    activate = true;
                }
                break;
            }
        }

        if (!activate || m_state != StateMainMenu)
            continue;

        switch (actions[selected]) {
        case ACTION_START:
            m_config.onStart(*this);
            break;
        case ACTION_FILES:
            OpenFileSelector();
            break;
        case ACTION_SETTINGS:
            OpenSettings();
            break;
        case ACTION_QUIT:
            Close();
            break;
        default:
            break;
        }
    }

    screen.End();
    return true;
}

bool Application::RunFramebufferFileSelector() {
    Entry *entries = static_cast<Entry *>(
        malloc(sizeof(Entry) * MAX_ENTRIES));
    if (!entries) {
        ReturnToMenu();
        return false;
    }

    Framebuffer screen;
    if (!screen.Begin()) {
        free(entries);
        ReturnToMenu();
        return false;
    }

    int count = 0;
    int selected = 0;
    bool done = false;

    screen.Clear(Colors::White);
    screen.DrawText(8, 12, "Scanning files...", Colors::Blue);
    screen.Present();
    Scan(m_currentDirectory, m_config, entries, &count);

    while (!done && m_state == StateFileSelector) {
        int pages = (count + ROWS - 1) / ROWS;
        if (pages < 1)
            pages = 1;
        if (selected < 0)
            selected = 0;
        if (selected >= count && count > 0)
            selected = count - 1;
        int page = selected / ROWS;
        if (page >= pages)
            page = pages - 1;
        const int first = page * ROWS;
        int visible = count - first;
        if (visible > ROWS)
            visible = ROWS;

        screen.Clear(Colors::White);
        screen.FillRect(0, 0, screen.Width(), 32, Colors::Black);
        screen.DrawText(8, 10, "Select file", Colors::White);
        screen.DrawText(8, 40, m_currentDirectory, Colors::Blue);
        screen.DrawLine(8, 54, screen.Width() - 8, 54, Colors::Gray);

        const int rowTop = 64;
        const int rowHeight = 32;
        for (int row = 0; row < visible; ++row) {
            const int index = first + row;
            const int y = rowTop + row * rowHeight;
            char label[NAME_LEN + 4];
            if (entries[index].dir) {
                Copy(label, "[D] ", sizeof(label));
                Append(label, entries[index].name, sizeof(label));
                Append(label, "/", sizeof(label));
            } else {
                Copy(label, "    ", sizeof(label));
                Append(label, entries[index].name, sizeof(label));
            }

            if (index == selected) {
                screen.FillRect(8, y, screen.Width() - 16,
                                rowHeight - 2, Colors::Blue);
                screen.DrawText(14, y + 10, label, Colors::White);
            } else {
                screen.DrawRect(8, y, screen.Width() - 16,
                                rowHeight - 2, Colors::Gray);
                screen.DrawText(14, y + 10, label, Colors::Black);
            }
        }
        if (count == 0)
            screen.DrawText(12, rowTop + 8, "No matching entries",
                            Colors::Gray);

        const int footerY = screen.Height() - 36;
        int buttonWidth = screen.Width() / 4;
        if (buttonWidth < 1)
            buttonWidth = 1;
        const char *footer[4] = {"<", ">", "Up", "Back"};
        for (int i = 0; i < 4; ++i) {
            const int x = i * buttonWidth;
            screen.DrawRect(x + 2, footerY, buttonWidth - 4, 28,
                            Colors::Gray);
            screen.DrawText(x + 8, footerY + 10, footer[i], Colors::Black);
        }
        screen.Present();

        bool activate = false;
        bool goParent = false;
        int footerAction = -1;
        UI::Event event;
        while (m_state == StateFileSelector && !activate &&
               !goParent && footerAction < 0) {
            const int result = Input::Poll(&event);
            if (result < 0) {
                ReturnToMenu();
                break;
            }
            if (result == 0)
                continue;

            if (event.type == EventSystem &&
                event.systemEvent == SystemActionBarEscape) {
                ReturnToMenu();
                break;
            }
            if (event.type == EventKey &&
                event.keyAction == KeyPressed) {
                if (event.keyCode == KeyUp) {
                    if (count > 0 && selected > 0)
                        --selected;
                    break;
                }
                if (event.keyCode == KeyDown) {
                    if (count > 0 && selected + 1 < count)
                        ++selected;
                    break;
                }
                if (event.keyCode == KeyLeft) {
                    if (page > 0)
                        selected = (page - 1) * ROWS;
                    break;
                }
                if (event.keyCode == KeyRight) {
                    if ((page + 1) * ROWS < count)
                        selected = (page + 1) * ROWS;
                    break;
                }
                if (event.keyCode == KeyBackspace) {
                    goParent = true;
                    break;
                }
                if (event.keyCode == KeyEXE) {
                    activate = count > 0;
                    break;
                }
                if (event.keyCode == KeyPowerClear) {
                    ReturnToMenu();
                    break;
                }
            } else if (event.type == EventTouch &&
                       event.touchAction == TouchDown) {
                const int y = event.y;
                if (y >= rowTop && y < rowTop + ROWS * rowHeight) {
                    const int row = (y - rowTop) / rowHeight;
                    const int index = first + row;
                    if (row < visible && index < count) {
                        selected = index;
                        activate = true;
                    }
                } else if (y >= footerY) {
                    const int action = event.x / buttonWidth;
                    if (action >= 0 && action < 4)
                        footerAction = action;
                }
                break;
            }
        }

        if (m_state != StateFileSelector)
            break;

        if (footerAction == 0) {
            if (page > 0)
                selected = (page - 1) * ROWS;
        } else if (footerAction == 1) {
            if ((page + 1) * ROWS < count)
                selected = (page + 1) * ROWS;
        } else if (footerAction == 2) {
            goParent = true;
        } else if (footerAction == 3) {
            ReturnToMenu();
            done = true;
        }

        if (goParent) {
            Parent(m_currentDirectory);
            screen.Clear(Colors::White);
            screen.DrawText(8, 12, "Scanning files...", Colors::Blue);
            screen.Present();
            Scan(m_currentDirectory, m_config, entries, &count);
            selected = 0;
            continue;
        }

        if (activate && count > 0 && selected < count) {
            if (entries[selected].dir) {
                char next[PATH_LEN];
                Join(m_currentDirectory, entries[selected].name,
                     next, sizeof(next));
                Copy(m_currentDirectory, next,
                     sizeof(m_currentDirectory));
                Append(m_currentDirectory, "\\",
                       sizeof(m_currentDirectory));
                screen.Clear(Colors::White);
                screen.DrawText(8, 12, "Scanning files...", Colors::Blue);
                screen.Present();
                Scan(m_currentDirectory, m_config, entries, &count);
                selected = 0;
            } else {
                char selectedPath[PATH_LEN];
                Join(m_currentDirectory, entries[selected].name,
                     selectedPath, sizeof(selectedPath));
                screen.End();
                SelectFile(selectedPath);
                done = true;
            }
        }
    }

    screen.End();
    free(entries);
    return true;
}

bool Application::RunFramebufferSettings() {
    if (m_config.onSettings &&
        (!m_config.settings || m_config.settingCount <= 0)) {
        m_config.onSettings(*this);
        if (m_state == StateSettings)
            ReturnToMenu();
        return true;
    }
    if (m_state != StateSettings)
        return true;
    if (!m_config.settings || m_config.settingCount <= 0) {
        ReturnToMenu();
        return true;
    }

    Framebuffer screen;
    if (!screen.Begin()) {
        ReturnToMenu();
        return false;
    }

    int selected = 0;
    bool done = false;
    while (!done && m_state == StateSettings) {
        int pages = (m_config.settingCount + ROWS - 1) / ROWS;
        if (pages < 1)
            pages = 1;
        if (selected < 0)
            selected = 0;
        if (selected >= m_config.settingCount)
            selected = m_config.settingCount - 1;
        const int page = selected / ROWS;
        const int first = page * ROWS;
        int visible = m_config.settingCount - first;
        if (visible > ROWS)
            visible = ROWS;

        screen.Clear(Colors::White);
        screen.FillRect(0, 0, screen.Width(), 32, Colors::Black);
        screen.DrawText(8, 10, "Settings", Colors::White);
        screen.DrawLine(8, 42, screen.Width() - 8, 42, Colors::Gray);

        const int rowTop = 50;
        const int rowHeight = 34;
        for (int row = 0; row < visible; ++row) {
            const int index = first + row;
            const Setting &setting = m_config.settings[index];
            char label[96];
            char value[32];
            Copy(label, setting.name ? setting.name : "Setting",
                 sizeof(label));
            Append(label, ": ", sizeof(label));
            Value(setting, value, sizeof(value));
            Append(label, value, sizeof(label));

            const int y = rowTop + row * rowHeight;
            if (index == selected) {
                screen.FillRect(8, y, screen.Width() - 16,
                                rowHeight - 2, Colors::Blue);
                screen.DrawText(14, y + 11, label, Colors::White);
            } else {
                screen.DrawRect(8, y, screen.Width() - 16,
                                rowHeight - 2, Colors::Gray);
                screen.DrawText(14, y + 11, label, Colors::Black);
            }
        }

        const int footerY = screen.Height() - 34;
        const Setting &current = m_config.settings[selected];
        if (current.description && current.description[0])
            screen.DrawText(8, footerY - 10, current.description, Colors::Gray);
        int buttonWidth = screen.Width() / 3;
        if (buttonWidth < 1)
            buttonWidth = 1;
        const char *footer[3] = {"<", ">", "Back"};
        for (int i = 0; i < 3; ++i) {
            const int x = i * buttonWidth;
            screen.DrawRect(x + 2, footerY, buttonWidth - 4, 26,
                            Colors::Gray);
            screen.DrawText(x + 8, footerY + 9, footer[i], Colors::Black);
        }
        screen.Present();

        int footerAction = -1;
        int adjustment = 0;
        bool activate = false;
        UI::Event event;
        while (m_state == StateSettings && footerAction < 0 &&
               adjustment == 0 && !activate) {
            const int result = Input::Poll(&event);
            if (result < 0) {
                ReturnToMenu();
                break;
            }
            if (result == 0)
                continue;
            if (event.type == EventSystem &&
                event.systemEvent == SystemActionBarEscape) {
                ReturnToMenu();
                break;
            }
            if (event.type == EventKey &&
                event.keyAction == KeyPressed) {
                if (event.keyCode == KeyUp) {
                    selected = (selected + m_config.settingCount - 1) %
                               m_config.settingCount;
                    break;
                }
                if (event.keyCode == KeyDown) {
                    selected = (selected + 1) % m_config.settingCount;
                    break;
                }
                if (event.keyCode == KeyLeft) {
                    adjustment = -1;
                    break;
                }
                if (event.keyCode == KeyRight) {
                    adjustment = 1;
                    break;
                }
                if (event.keyCode == KeyEXE) {
                    activate = true;
                    break;
                }
                if (event.keyCode == KeyPowerClear) {
                    ReturnToMenu();
                    break;
                }
            } else if (event.type == EventTouch &&
                       event.touchAction == TouchDown) {
                if (event.y >= rowTop &&
                    event.y < rowTop + ROWS * rowHeight) {
                    const int row = (event.y - rowTop) / rowHeight;
                    if (row < visible) {
                        selected = first + row;
                        activate = true;
                    }
                } else if (event.y >= footerY) {
                    const int action = event.x / buttonWidth;
                    if (action >= 0 && action < 3)
                        footerAction = action;
                }
                break;
            }
        }

        if (m_state != StateSettings)
            break;
        if (footerAction == 0) {
            if (page > 0)
                selected = (page - 1) * ROWS;
            continue;
        }
        if (footerAction == 1) {
            if ((page + 1) * ROWS < m_config.settingCount)
                selected = (page + 1) * ROWS;
            continue;
        }
        if (footerAction == 2) {
            ReturnToMenu();
            done = true;
            continue;
        }

        Setting &setting = m_config.settings[selected];
        if (adjustment != 0 && setting.type != SettingAction)
            AdjustSetting(selected, adjustment);
        else if (activate)
            AdjustSetting(selected, 1);

        if ((adjustment != 0 || activate) &&
            m_state != StateSettings)
            done = true;
    }

    screen.End();
    return true;
}

} // namespace UI
