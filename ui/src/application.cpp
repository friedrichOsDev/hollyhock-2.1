#include <sdk/calc/calc.hpp>
#include <ui/filesystem.hpp>
#include <sdk/os/gui.hpp>
#include <sdk/os/debug.hpp>
#include <sdk/os/lcd.hpp>
#include <sdk/os/mem.hpp>
#include <stddef.h>
#include <stdint.h>
#include <ui/application.hpp>

inline void *operator new(size_t, void *memory) noexcept { return memory; }
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
    explicit EventDialog(const char *t) : GUIDialog(Height95, AlignCenter, t, KeyboardStateNone), buttonCount(0), event(0) {}
    int EventID() const { return event; }
    void RegisterButton(int id) { if (buttonCount < 12) buttonIDs[buttonCount++] = id; }
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
        char *p = (char *)malloc(256);
        if (!p) return 0;
        int i = 0;
        if (t) while (t[i] && i < 255) { p[i] = t[i]; ++i; }
        p[i] = 0;
        return p;
    }
public:
    Elements() : used(0) {}
    ~Elements() { for (int i = 0; i < used; ++i) free(mem[i]); }
    void AddButton(EventDialog &d, uint16_t x, uint16_t y, uint16_t r, uint16_t b, const char *t, int id, bool enabled = true) {
        if (used > 22) return;
        void *p = malloc(sizeof(GUIButton));
        if (!p) return;
        char *stable = CopyText(t);
        if (!stable) { free(p); return; }
        GUIButton *q = new (p) GUIButton(x, y, r, b, stable, (uint16_t)id, enabled ? GUIButton::FlagEnabled : 0);
        mem[used++] = p; mem[used++] = stable;
        d.AddElement(*q); d.RegisterButton(id);
    }
    void AddLabel(EventDialog &d, int x, int y, const char *t) {
        if (used > 22) return;
        void *p = malloc(sizeof(GUILabel));
        if (!p) return;
        char *stable = CopyText(t);
        if (!stable) { free(p); return; }
        GUILabel *q = new (p) GUILabel(x, y, stable);
        mem[used++] = p; mem[used++] = stable;
        d.AddElement(*q);
    }
};

int Len(const char *s) { int n = 0; if (s) while (s[n]) ++n; return n; }
void Copy(char *d, const char *s, int cap) {
    if (!d || cap <= 0) return;
    int i = 0;
    if (s) while (s[i] && i < cap - 1) { d[i] = s[i]; ++i; }
    d[i] = 0;
}
void Append(char *d, const char *s, int cap) {
    int n = Len(d); if (n < cap - 1) Copy(d + n, s, cap - n);
}
void Number(int v, char *out, int cap) {
    if (cap <= 0) return;
    char rev[16]; int n = 0;
    bool neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-(v + 1)) + 1U : (unsigned int)v;
    do { rev[n++] = char('0' + u % 10U); u /= 10U; } while (u && n < (int)sizeof(rev));
    int p = 0;
    if (neg && p < cap - 1) out[p++] = '-';
    while (n && p < cap - 1) out[p++] = rev[--n];
    out[p] = 0;
}
char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
bool Equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) { if (Lower(*a) != Lower(*b)) return false; ++a; ++b; }
    return *a == *b;
}
struct Entry { char name[NAME_LEN]; bool dir; };
int Compare(const char *a, const char *b) {
    while (*a && *b) { char x = Lower(*a), y = Lower(*b); if (x != y) return x < y ? -1 : 1; ++a; ++b; }
    return *a == *b ? 0 : (*a ? 1 : -1);
}
void Sort(Entry *e, int n) {
    for (int i = 1; i < n; ++i) {
        Entry v = e[i]; int j = i;
        while (j > 0) {
            Entry &p = e[j - 1];
            if (p.dir && !v.dir) break;
            if (p.dir == v.dir && Compare(p.name, v.name) <= 0) break;
            e[j] = e[j - 1]; --j;
        }
        e[j] = v;
    }
}
void ShowLoadingMessage() {
    const int x = 2;
    Debug_SetCursorPosition(x, 0); Debug_PrintString("+--------------------+", false);
    Debug_SetCursorPosition(x, 1); Debug_PrintString("| Scanning files...  |", false);
    Debug_SetCursorPosition(x, 2); Debug_PrintString("|   Please wait.     |", false);
    Debug_SetCursorPosition(x, 3); Debug_PrintString("+--------------------+", false);
    LCD_Refresh();
}
void ClearLoadingMessage() {
    const int x = 2;
    for (int row = 0; row < 4; ++row) { Debug_SetCursorPosition(x, row); Debug_PrintString("                      ", false); }
    LCD_Refresh();
}
void Scan(const char *dir, const Config &cfg, Entry *e, int *count) {
    *count = 0;
    Directory directory;
    if (directory.Open(dir) < 0) return;
    DirectoryEntry item;
    while (directory.Next(&item) == 0) {
        bool skip = Equal(item.name, ".") || Equal(item.name, "..");
        if (!skip && (item.isDirectory || FileFilterMatches(item.name, cfg.filters, cfg.filterCount)) && *count < MAX_ENTRIES) {
            Copy(e[*count].name, item.name, NAME_LEN);
            e[*count].dir = item.isDirectory; ++*count;
        }
    }
    directory.Close(); Sort(e, *count);
}
void Bounds(GUIDialog &d, int row, uint16_t *x, uint16_t *y, uint16_t *r, uint16_t *b) {
    *x = d.GetLeftX() + 8; *r = d.GetRightX() - 8;
    *y = d.GetTopY() + 8 + row * 42; *b = *y + 35;
}
void Button(Elements &el, EventDialog &d, int row, const char *text, int id, bool enabled = true) {
    uint16_t x, y, r, b; Bounds(d, row, &x, &y, &r, &b); el.AddButton(d, x, y, r, b, text, id, enabled);
}
void Bottom(Elements &el, EventDialog &d, int x, int w, const char *t, int id) {
    int y = d.GetBottomY() - 40; el.AddButton(d, d.GetLeftX() + x, y, d.GetLeftX() + x + w, y + 35, t, id);
}
void PathLabel(Elements &el, EventDialog &d, const char *p) { el.AddLabel(d, d.GetLeftX() + 8, d.GetTopY() + 2, p); }
void Join(const char *dir, const char *name, char *out, int cap) {
    Copy(out, dir, cap); int n = Len(out);
    if (n && out[n - 1] != '\\') Append(out, "\\", cap);
    Append(out, name, cap);
}
void Parent(char *p) {
    const char *root = "\\fls0\\"; int rootn = Len(root), n = Len(p);
    if (n <= rootn) return;
    if (p[n - 1] == '\\') --n;
    while (n > rootn && p[n - 1] != '\\') --n;
    if (n < rootn) n = rootn;
    p[n] = 0;
}
void Value(const Setting &s, char *out, int cap) {
    out[0] = 0;
    if (s.type == SettingAction) { Copy(out, "Run", cap); return; }
    if (!s.value) { Copy(out, "(unset)", cap); return; }
    if (s.type == SettingBoolean) Copy(out, *static_cast<bool *>(s.value) ? "On" : "Off", cap);
    else if (s.type == SettingInteger) Number(*static_cast<int *>(s.value), out, cap);
    else if (s.type == SettingEnum) {
        int v = *static_cast<int *>(s.value);
        if (s.options && v >= 0 && v < s.optionCount) Copy(out, s.options[v], cap);
        else Number(v, out, cap);
    } else Copy(out, "Run", cap);
}
void Adjust(Setting &s, Application &a) {
    if (s.type == SettingAction) { if (s.action) s.action(a); return; }
    if (!s.value) return;
    if (s.type == SettingBoolean) {
        bool *v = (bool *)s.value; *v = !*v;
    } else if (s.type == SettingInteger) {
        int *v = (int *)s.value; int next = *v + s.step;
        if (next > s.maximum) next = s.minimum;
        if (next < s.minimum) next = s.minimum;
        *v = next;
    } else if (s.type == SettingEnum && s.optionCount > 0) {
        int *v = (int *)s.value; ++*v;
        if (*v >= s.optionCount || *v < 0) *v = 0;
    }
}

// Versioned ASCII storage format: UIST1;<count>;<type><value>;...
// Action entries are represented by A; because they have no persistent value.
bool AddChar(char *out, int *position, char c) {
    if (*position >= PERSISTENCE_BUFFER_SIZE - 1) return false;
    out[(*position)++] = c; out[*position] = 0; return true;
}
bool AddText(char *out, int *position, const char *text) {
    while (*text) if (!AddChar(out, position, *text++)) return false;
    return true;
}
bool AddUnsigned(char *out, int *position, unsigned int value) {
    char digits[12]; int count = 0;
    do { digits[count++] = char('0' + value % 10U); value /= 10U; } while (value && count < (int)sizeof(digits));
    while (count) if (!AddChar(out, position, digits[--count])) return false;
    return true;
}
bool AddSigned(char *out, int *position, int value) {
    bool negative = value < 0;
    unsigned int magnitude = negative ? (unsigned int)(-(value + 1)) + 1U : (unsigned int)value;
    if (negative && !AddChar(out, position, '-')) return false;
    return AddUnsigned(out, position, magnitude);
}
bool SerializeSettings(const Setting *settings, int count, char *out, uint32_t *size) {
    if (!settings || count <= 0 || count > MAX_PERSISTED_SETTINGS || !out || !size) return false;
    int position = 0; out[0] = 0;
    if (!AddText(out, &position, "UIST1;") ||
        !AddUnsigned(out, &position, (unsigned int)count) ||
        !AddChar(out, &position, ';')) return false;
    for (int i = 0; i < count; ++i) {
        const Setting &setting = settings[i];
        if (setting.type == SettingAction) {
            if (!AddText(out, &position, "A;")) return false;
            continue;
        }
        if (!setting.value) return false;
        if (setting.type == SettingBoolean) {
            if (!AddChar(out, &position, 'B') ||
                !AddSigned(out, &position, *static_cast<bool *>(setting.value) ? 1 : 0) ||
                !AddChar(out, &position, ';')) return false;
        } else if (setting.type == SettingInteger) {
            if (!AddChar(out, &position, 'I') ||
                !AddSigned(out, &position, *static_cast<int *>(setting.value)) ||
                !AddChar(out, &position, ';')) return false;
        } else if (setting.type == SettingEnum) {
            if (!AddChar(out, &position, 'E') ||
                !AddSigned(out, &position, *static_cast<int *>(setting.value)) ||
                !AddChar(out, &position, ';')) return false;
        } else return false;
    }
    if (!AddChar(out, &position, 0)) return false;
    *size = (uint32_t)position; return true;
}
bool ParseSigned(const char **cursor, int *value) {
    const char *p = *cursor; bool negative = false;
    if (*p == '-') { negative = true; ++p; }
    if (*p < '0' || *p > '9') return false;
    unsigned int number = 0;
    const unsigned int limit = negative ? 2147483648U : 2147483647U;
    while (*p >= '0' && *p <= '9') {
        unsigned int digit = (unsigned int)(*p - '0');
        if (number > (limit - digit) / 10U) return false;
        number = number * 10U + digit; ++p;
    }
    if (*p != ';') return false;
    ++p;
    if (negative) *value = number == 2147483648U ? (-2147483647 - 1) : -(int)number;
    else *value = (int)number;
    *cursor = p; return true;
}
bool ParseSettings(const char *data, const Setting *settings, int count) {
    if (!data || !settings || count <= 0 || count > MAX_PERSISTED_SETTINGS) return false;
    const char *p = data;
    const char prefix[] = "UIST1;";
    for (int i = 0; i < 6; ++i) if (*p++ != prefix[i]) return false;
    int storedCount = 0;
    if (!ParseSigned(&p, &storedCount) || storedCount != count) return false;

    int values[MAX_PERSISTED_SETTINGS];
    for (int i = 0; i < count; ++i) {
        const Setting &setting = settings[i];
        char type = *p++;
        if (type == 0) return false;
        if (setting.type == SettingAction) {
            if (type != 'A' || *p++ != ';') return false;
            values[i] = 0; continue;
        }
        int value = 0;
        if (!ParseSigned(&p, &value)) return false;
        if ((setting.type == SettingBoolean && (type != 'B' || (value != 0 && value != 1))) ||
            (setting.type == SettingInteger && (type != 'I' || value < setting.minimum || value > setting.maximum)) ||
            (setting.type == SettingEnum && (type != 'E' || value < 0 || value >= setting.optionCount)))
            return false;
        if (!setting.value) return false;
        values[i] = value;
    }
    if (*p != 0) return false;
    for (int i = 0; i < count; ++i) {
        if (settings[i].type == SettingBoolean)
            *static_cast<bool *>(settings[i].value) = values[i] != 0;
        else if (settings[i].type == SettingInteger || settings[i].type == SettingEnum)
            *static_cast<int *>(settings[i].value) = values[i];
    }
    return true;
}
} // namespace

Application::Application(const Config &c)
    : m_config(c), m_state(StateMainMenu), m_lifecycleStarted(false),
      m_persistenceInitialized(false), m_persistenceReady(false), m_persistenceResult(0) {
    Copy(m_currentDirectory, "\\fls0\\", sizeof(m_currentDirectory));
}
Application::~Application() {
    if (m_lifecycleStarted && m_config.manageCalculatorLifecycle) {
        calcEnd(); m_lifecycleStarted = false;
    }
}
void Application::InitializeSettingsPersistence() {
    m_persistenceInitialized = true;
    if (!m_config.settingsFolder || !m_config.settingsFolder[0]) return;
    int result = m_settingsStore.Open(m_config.settingsFolder);
    // MCS errors are positive constants (unlike file-system errors).
    if (result != 0) { m_persistenceResult = result; return; }
    if (!m_config.settings || m_config.settingCount <= 0) { m_persistenceReady = true; return; }

    char buffer[PERSISTENCE_BUFFER_SIZE];
    uint8_t type = 0; uint32_t size = 0;
    result = m_settingsStore.Load("SETTINGS", &type, buffer, sizeof(buffer) - 1, &size);
    if (result == MCS_NO_VARIABLE) {
        m_persistenceReady = true; m_persistenceResult = 0; return;
    }
    if (result != 0) { m_persistenceResult = result; return; }
    if (type != VARTYPE_STR || size == 0 || size >= sizeof(buffer)) {
        m_persistenceResult = EUNFORMAT; return;
    }
    buffer[size] = 0;
    if (!ParseSettings(buffer, m_config.settings, m_config.settingCount)) {
        m_persistenceResult = EUNFORMAT; return;
    }
    m_persistenceReady = true; m_persistenceResult = 0;
}
void Application::LoadSettingsFromStorage() { InitializeSettingsPersistence(); }
void Application::SaveSettingsToStorage() {
    if (!m_settingsStore.IsOpen() || !m_config.settings || m_config.settingCount <= 0) return;
    char buffer[PERSISTENCE_BUFFER_SIZE]; uint32_t size = 0;
    if (!SerializeSettings(m_config.settings, m_config.settingCount, buffer, &size)) {
        m_persistenceResult = EOUTOFBOUND; return;
    }
    m_persistenceResult = m_settingsStore.Save("SETTINGS", VARTYPE_STR, buffer, size);
}
int Application::SettingsPersistenceResult() const { return m_persistenceResult; }

void Application::Run() {
    if (m_state == StateExit) return;
    if (m_config.manageCalculatorLifecycle && !m_lifecycleStarted) {
        calcInit(); m_lifecycleStarted = true;
    }
    if (!m_persistenceInitialized) LoadSettingsFromStorage();

    while (m_state != StateExit) {
        switch (m_state) {
        case StateMainMenu: RunMainMenu(); break;
        case StateFileSelector: RunFileSelector(); break;
        case StateSettings: RunSettings(); break;
        default: m_state = StateExit; break;
        }
    }
    if (m_persistenceReady) SaveSettingsToStorage();
    if (m_lifecycleStarted && m_config.manageCalculatorLifecycle) {
        calcEnd(); m_lifecycleStarted = false;
    }
}
void Application::Close() { m_state = StateExit; }
void Application::ReturnToMenu() { m_state = StateMainMenu; }
void Application::OpenFileSelector() { if (m_config.showFileSelector) m_state = StateFileSelector; }
void Application::OpenSettings() { if (m_config.showSettings) m_state = StateSettings; }
bool Application::IsRunning() const { return m_state != StateExit; }

void Application::RunMainMenu() {
    Elements el; EventDialog d(m_config.title ? m_config.title : "Application"); int row = 0;
    if (m_config.showStart && m_config.onStart) Button(el, d, row++, m_config.startLabel ? m_config.startLabel : "Start", E_START);
    if (m_config.showFileSelector) Button(el, d, row++, m_config.filesLabel ? m_config.filesLabel : "Files", E_FILES);
    if (m_config.showSettings && (m_config.settingCount > 0 || m_config.onSettings)) Button(el, d, row++, m_config.settingsLabel ? m_config.settingsLabel : "Settings", E_SETTINGS);
    if (m_config.showQuit) Button(el, d, row++, m_config.quitLabel ? m_config.quitLabel : "Quit", E_QUIT);
    if (!row) Button(el, d, row++, "Quit", E_QUIT);
    d.ShowDialog(); int event = d.EventID();
    if (event == E_START && m_config.onStart) m_config.onStart(*this);
    else if (event == E_FILES) OpenFileSelector();
    else if (event == E_SETTINGS) OpenSettings();
    else if (event == E_QUIT) Close();
}
void Application::RunFileSelector() {
    Entry *entries = (Entry *)malloc(sizeof(Entry) * MAX_ENTRIES);
    if (!entries) { ReturnToMenu(); return; }
    int count = 0; ShowLoadingMessage(); Scan(m_currentDirectory, m_config, entries, &count); ClearLoadingMessage();
    int page = 0; bool done = false;
    while (!done && m_state == StateFileSelector) {
        Elements el; EventDialog d("Select file"); PathLabel(el, d, m_currentDirectory);
        int pages = (count + ROWS - 1) / ROWS;
        if (page >= pages && pages > 0) page = pages - 1;
        if (page < 0) page = 0;
        int visible = count - page * ROWS; if (visible > ROWS) visible = ROWS;
        for (int row = 0; row < visible; ++row) {
            int index = page * ROWS + row; char label[NAME_LEN + 3];
            Copy(label, entries[index].name, sizeof(label));
            if (entries[index].dir) Append(label, "/", sizeof(label));
            Button(el, d, row + 1, label, E_ROW + row);
        }
        Bottom(el, d, 8, 38, "<", E_PREV); Bottom(el, d, 50, 38, ">", E_NEXT);
        Bottom(el, d, 92, 56, "Up", E_UP); Bottom(el, d, 152, 60, "Back", E_BACK);
        d.ShowDialog(); int event = d.EventID();
        if (event == E_BACK) { ReturnToMenu(); done = true; }
        else if (event == E_UP) {
            Parent(m_currentDirectory); ShowLoadingMessage(); Scan(m_currentDirectory, m_config, entries, &count); ClearLoadingMessage(); page = 0;
        } else if (event == E_PREV) { if (page > 0) --page; }
        else if (event == E_NEXT) { if ((page + 1) * ROWS < count) ++page; }
        else if (event >= E_ROW && event < E_ROW + ROWS) {
            int index = page * ROWS + event - E_ROW;
            if (index < count) {
                if (entries[index].dir) {
                    char next[PATH_LEN]; Join(m_currentDirectory, entries[index].name, next, sizeof(next));
                    Copy(m_currentDirectory, next, sizeof(m_currentDirectory)); Append(m_currentDirectory, "\\", sizeof(m_currentDirectory));
                    ShowLoadingMessage(); Scan(m_currentDirectory, m_config, entries, &count); ClearLoadingMessage(); page = 0;
                } else {
                    char selected[PATH_LEN]; Join(m_currentDirectory, entries[index].name, selected, sizeof(selected));
                    if (m_config.onFileSelected) m_config.onFileSelected(*this, selected);
                    if (m_state == StateFileSelector) ReturnToMenu();
                    done = true;
                }
            }
        }
    }
    free(entries);
}
void Application::RunSettings() {
    if (m_config.onSettings && (!m_config.settings || m_config.settingCount <= 0)) {
        m_config.onSettings(*this); if (m_state == StateSettings) ReturnToMenu(); return;
    }
    if (m_state != StateSettings) return;
    if (!m_config.settings || m_config.settingCount <= 0) { ReturnToMenu(); return; }
    int page = 0; bool done = false;
    while (!done && m_state == StateSettings) {
        Elements el; EventDialog d("Settings");
        int pages = (m_config.settingCount + ROWS - 1) / ROWS;
        if (page >= pages && pages > 0) page = pages - 1;
        if (page < 0) page = 0;
        int visible = m_config.settingCount - page * ROWS; if (visible > ROWS) visible = ROWS;
        for (int row = 0; row < visible; ++row) {
            int index = page * ROWS + row; Setting &s = m_config.settings[index];
            char label[96], value[32]; Copy(label, s.name ? s.name : "Setting", sizeof(label));
            Append(label, ": ", sizeof(label)); Value(s, value, sizeof(value)); Append(label, value, sizeof(label));
            Button(el, d, row, label, E_ROW + row);
        }
        Bottom(el, d, 8, 38, "<", E_PREV); Bottom(el, d, 50, 38, ">", E_NEXT); Bottom(el, d, 94, 116, "Back", E_BACK);
        d.ShowDialog(); int event = d.EventID();
        if (event == E_BACK) { ReturnToMenu(); done = true; }
        else if (event == E_PREV) { if (page > 0) --page; }
        else if (event == E_NEXT) { if ((page + 1) * ROWS < m_config.settingCount) ++page; }
        else if (event >= E_ROW && event < E_ROW + ROWS) {
            int index = page * ROWS + event - E_ROW;
            if (index < m_config.settingCount) {
                Adjust(m_config.settings[index], *this);
                if (m_settingsStore.IsOpen()) {
                    m_persistenceReady = true;
                    SaveSettingsToStorage();
                }
            }
            if (m_state != StateSettings) done = true;
        }
    }
}
} // namespace UI
