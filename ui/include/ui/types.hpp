#pragma once

namespace UI {

class Application;

enum class MenuRenderer {
    Native,
    Framebuffer,
    Custom
};

typedef void (*ActionCallback)(Application &);
typedef void (*FileCallback)(Application &, const char *);

struct FileFilter {
    const char *extension;
    const char *description;

    FileFilter(const char *ext = 0, const char *desc = 0)
        : extension(ext), description(desc) {}
};

enum SettingType {
    SettingBoolean,
    SettingInteger,
    SettingEnum,
    SettingAction
};

struct Setting {
    // Stable persistence key. If null, the setting name is used as a fallback.
    const char *id;
    const char *name;
    const char *description;
    SettingType type;
    void *value;

    int minimum;
    int maximum;
    int step;

    const char *const *options;
    int optionCount;
    ActionCallback action;

    Setting();

    static Setting Boolean(const char *name, bool *value,
                           const char *description = 0);
    static Setting Boolean(const char *id, const char *name, bool *value,
                           const char *description = 0);
    static Setting Integer(const char *name, int *value,
                           int minimum, int maximum, int step = 1,
                           const char *description = 0);
    static Setting Integer(const char *id, const char *name, int *value,
                           int minimum, int maximum, int step = 1,
                           const char *description = 0);
    static Setting Enumeration(const char *name, int *value,
                               const char *const *options, int optionCount,
                               const char *description = 0);
    static Setting Enumeration(const char *id, const char *name, int *value,
                               const char *const *options, int optionCount,
                               const char *description = 0);
    static Setting Action(const char *name, ActionCallback callback,
                          const char *description = 0);
    static Setting Action(const char *id, const char *name,
                          ActionCallback callback,
                          const char *description = 0);
};

struct Config {
    const char *title;
    const char *startLabel;
    const char *filesLabel;
    const char *settingsLabel;
    const char *quitLabel;

    // Optional 1-8 character MCS folder name for automatic settings storage.
    const char *settingsFolder;

    // When false, onStart is called directly instead of showing the menu.
    bool showMainMenu;
    bool showStart;
    bool showFileSelector;
    bool showSettings;
    bool showQuit;
    bool manageCalculatorLifecycle;

    // Select the presentation backend independently for each built-in menu.
    // Custom mode invokes the corresponding onCustom... callback.
    MenuRenderer mainMenuRenderer;
    MenuRenderer fileSelectorRenderer;
    MenuRenderer settingsRenderer;

    ActionCallback onStart;
    ActionCallback onSettings;
    FileCallback onFileSelected;

    // Custom renderers own both drawing and input handling. Application
    // continues to own lifecycle, persistence, and menu-state transitions.
    ActionCallback onCustomMainMenu;
    ActionCallback onCustomFileSelector;
    ActionCallback onCustomSettings;

    const FileFilter *filters;
    int filterCount;

    Setting *settings;
    int settingCount;

    Config();
};

bool FileFilterMatches(const char *filename, const FileFilter *filters,
                       int filterCount);

} // namespace UI
