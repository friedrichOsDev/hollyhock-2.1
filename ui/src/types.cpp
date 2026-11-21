#include <ui/types.hpp>

namespace UI {
namespace {

char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c;
}

int Length(const char *text) {
    int length = 0;
    if (text) {
        while (text[length])
            ++length;
    }
    return length;
}

bool HasSuffix(const char *filename, int filenameLength,
               const char *extension, int extensionLength) {
    if (extensionLength > filenameLength)
        return false;

    const int offset = filenameLength - extensionLength;
    for (int i = 0; i < extensionLength; ++i) {
        if (Lower(filename[offset + i]) != Lower(extension[i]))
            return false;
    }
    return true;
}

bool MatchesExtension(const char *filename, const char *extension) {
    if (!filename || !extension || !extension[0])
        return false;

    const int filenameLength = Length(filename);
    const int extensionLength = Length(extension);
    if (filenameLength == 0)
        return false;

    if (extension[0] == '.')
        return HasSuffix(filename, filenameLength,
                         extension, extensionLength);

    // Extensions without a leading dot must still start at a suffix
    // boundary, so "notxt" does not match "txt".
    if (filenameLength <= extensionLength)
        return false;

    const int dotOffset = filenameLength - extensionLength - 1;
    if (filename[dotOffset] != '.')
        return false;

    return HasSuffix(filename, filenameLength,
                     extension, extensionLength);
}

} // namespace

Setting::Setting()
    : name(0),
      description(0),
      type(SettingBoolean),
      value(0),
      minimum(0),
      maximum(1),
      step(1),
      options(0),
      optionCount(0),
      action(0) {}

Setting Setting::Boolean(const char *settingName, bool *settingValue,
                         const char *settingDescription) {
    Setting setting;
    setting.name = settingName;
    setting.description = settingDescription;
    setting.type = SettingBoolean;
    setting.value = settingValue;
    return setting;
}

Setting Setting::Integer(const char *settingName, int *settingValue,
                         int settingMinimum, int settingMaximum,
                         int settingStep, const char *settingDescription) {
    Setting setting;
    setting.name = settingName;
    setting.description = settingDescription;
    setting.type = SettingInteger;
    setting.value = settingValue;
    setting.minimum = settingMinimum;
    setting.maximum = settingMaximum;
    setting.step = settingStep > 0 ? settingStep : 1;
    return setting;
}

Setting Setting::Enumeration(const char *settingName, int *settingValue,
                             const char *const *settingOptions,
                             int settingOptionCount,
                             const char *settingDescription) {
    Setting setting;
    setting.name = settingName;
    setting.description = settingDescription;
    setting.type = SettingEnum;
    setting.value = settingValue;
    setting.options = settingOptions;
    setting.optionCount = settingOptionCount;
    setting.minimum = 0;
    setting.maximum = settingOptionCount > 0 ? settingOptionCount - 1 : 0;
    return setting;
}

Setting Setting::Action(const char *settingName, ActionCallback callback,
                        const char *settingDescription) {
    Setting setting;
    setting.name = settingName;
    setting.description = settingDescription;
    setting.type = SettingAction;
    setting.action = callback;
    return setting;
}

Config::Config()
    : title("Application"),
      startLabel("Start"),
      filesLabel("Files"),
      settingsLabel("Settings"),
      quitLabel("Quit"),
      settingsFolder(0),
      showMainMenu(true),
      showStart(true),
      showFileSelector(false),
      showSettings(false),
      showQuit(true),
      manageCalculatorLifecycle(true),
      onStart(0),
      onSettings(0),
      onFileSelected(0),
      filters(0),
      filterCount(0),
      settings(0),
      settingCount(0) {}

bool FileFilterMatches(const char *filename, const FileFilter *filters,
                       int filterCount) {
    if (!filename || !filename[0])
        return false;

    if (!filters || filterCount <= 0)
        return true;

    for (int i = 0; i < filterCount; ++i) {
        if (MatchesExtension(filename, filters[i].extension))
            return true;
    }

    return false;
}

} // namespace UI
