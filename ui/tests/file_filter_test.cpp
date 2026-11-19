#include <stdio.h>
#include <ui/types.hpp>

static int failures = 0;
static void Check(bool condition, const char *name) {
    if (condition)
        printf("PASS: %s\n", name);
    else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}
static void DummyAction(UI::Application &) {}

int main() {
    const UI::FileFilter filters[] = {UI::FileFilter(".txt", "Text"), UI::FileFilter("CPV2", "Video")};
    Check(UI::FileFilterMatches("readme.txt", filters, 2), "leading-dot extension");
    Check(UI::FileFilterMatches("VIDEO.CPV2", filters, 2), "case-insensitive extension without dot");
    Check(!UI::FileFilterMatches("readme.md", filters, 2), "reject unmatched extension");
    Check(!UI::FileFilterMatches("notxt", filters, 2), "extension must match suffix");
    Check(UI::FileFilterMatches("any.bin", 0, 0), "null filter list means allow all");
    Check(UI::FileFilterMatches("any.bin", filters, 0), "zero filter count means allow all");
    Check(!UI::FileFilterMatches(0, filters, 2), "null filename rejected");
    const UI::FileFilter empty[] = {UI::FileFilter("", "ignored")};
    Check(!UI::FileFilterMatches("file", empty, 1), "empty extension ignored");
    bool enabled = false;
    int amount = 7;
    int mode = 1;
    const char *options[] = {"A", "B", "C"};
    UI::Setting b = UI::Setting::Boolean("Enabled", &enabled);
    UI::Setting i = UI::Setting::Integer("Amount", &amount, 0, 10, 2);
    UI::Setting e = UI::Setting::Enumeration("Mode", &mode, options, 3);
    UI::Setting a = UI::Setting::Action("Action", DummyAction);
    Check(b.type == UI::SettingBoolean && b.value == &enabled, "boolean setting API");
    Check(i.type == UI::SettingInteger && i.minimum == 0 && i.maximum == 10 && i.step == 2 && i.value == &amount, "integer setting API");
    Check(e.type == UI::SettingEnum && e.optionCount == 3 && e.options == options && e.value == &mode, "enum setting API");
    Check(a.type == UI::SettingAction && a.action == DummyAction, "action setting API");
    UI::Config config;
    Check(config.showStart && config.showQuit && config.manageCalculatorLifecycle, "config defaults");
    Check(!config.showFileSelector && !config.showSettings && !config.onStart, "optional entries default off");
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
