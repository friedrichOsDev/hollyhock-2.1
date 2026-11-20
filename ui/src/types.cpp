#include <ui/types.hpp>
namespace UI {
Setting::Setting() : name(0), description(0), type(SettingBoolean), value(0), minimum(0), maximum(1), step(1), options(0), optionCount(0), action(0) {}
Setting Setting::Boolean(const char *n, bool *v, const char *d) {
    Setting s;
    s.name = n; s.description = d; s.type = SettingBoolean; s.value = v;
    return s;
}
Setting Setting::Integer(const char *n, int *v, int min, int max, int st, const char *d) {
    Setting s;
    s.name = n; s.description = d; s.type = SettingInteger; s.value = v;
    s.minimum = min; s.maximum = max; s.step = st > 0 ? st : 1;
    return s;
}
Setting Setting::Enumeration(const char *n, int *v, const char *const *opts, int count, const char *d) {
    Setting s;
    s.name = n; s.description = d; s.type = SettingEnum; s.value = v;
    s.options = opts; s.optionCount = count; s.minimum = 0;
    s.maximum = count > 0 ? count - 1 : 0;
    return s;
}
Setting Setting::Action(const char *n, ActionCallback cb, const char *d) {
    Setting s;
    s.name = n; s.description = d; s.type = SettingAction; s.action = cb;
    return s;
}
Config::Config() : title("Application"), startLabel("Start"), filesLabel("Files"),
    settingsLabel("Settings"), quitLabel("Quit"), settingsFolder(0),
    showStart(true), showFileSelector(false), showSettings(false), showQuit(true),
    manageCalculatorLifecycle(true), onStart(0), onSettings(0), onFileSelected(0),
    filters(0), filterCount(0), settings(0), settingCount(0) {}
static char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
static bool Suffix(const char *n, const char *e) {
    if (!n || !e || !*e) return false;
    int a = 0, b = 0;
    while (n[a]) ++a;
    while (e[b]) ++b;
    if (b > a) return false;
    for (int i = 0; i < b; ++i)
        if (Lower(n[a - b + i]) != Lower(e[i])) return false;
    return true;
}
bool FileFilterMatches(const char *filename, const FileFilter *filters, int count) {
    if (!filename) return false;
    if (!filters || count <= 0) return true;
    for (int i = 0; i < count; ++i) {
        const char *ext = filters[i].extension;
        if (!ext || !*ext) continue;
        if (ext[0] == '.') {
            if (Suffix(filename, ext)) return true;
        } else {
            char dotted[64];
            int j = 0;
            dotted[j++] = '.';
            while (ext[j - 1] && j < 63) { dotted[j] = ext[j - 1]; ++j; }
            dotted[j] = 0;
            if (Suffix(filename, dotted)) return true;
        }
    }
    return false;
}
} // namespace UI
