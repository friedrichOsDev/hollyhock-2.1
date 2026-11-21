#include <ui/types.hpp>
#include "../src/settings_codec.hpp"

// Avoid stdio.h because SDK headers use some libc names differently.
extern "C" int printf(const char *format, ...);

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char *name) {
    ++checks;
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

bool Equals(const char *left, const char *right) {
    if (!left || !right)
        return left == right;
    int i = 0;
    while (left[i] && right[i] && left[i] == right[i])
        ++i;
    return left[i] == right[i];
}

} // namespace

int main() {
    char buffer[UI::Detail::PersistenceBufferSize];
    uint32_t size = 0;
    bool legacy = false;

    bool preview = true;
    int bytes = 32;
    int mode = 2;
    const char *options[] = {"Text", "Hex"};

    UI::Setting original[] = {
        UI::Setting::Boolean("showPreview", "File preview", &preview),
        UI::Setting::Integer("previewBytes", "Preview bytes",
                             &bytes, 8, 64, 8),
        UI::Setting::Enumeration("previewFormat", "Preview format",
                                 &mode, options, 2)
    };

    Check(UI::Detail::SerializeSettings(original, 3, buffer, &size),
          "serialize UIST2 settings");
    Check(Equals(buffer,
          "UIST2;3;11:showPreviewB1;12:previewBytesI32;13:previewFormatE2;"),
          "serialized data contains stable keys and typed values");
    Check(size == 64U,
          "serialized size includes the terminating null byte");

    // Reverse the setting order. Keyed loading must preserve the values.
    bool loadedPreview = false;
    int loadedBytes = 8;
    int loadedMode = 0;
    UI::Setting reordered[] = {
        UI::Setting::Enumeration("previewFormat", "Format",
                                 &loadedMode, options, 2),
        UI::Setting::Boolean("showPreview", "Preview",
                             &loadedPreview),
        UI::Setting::Integer("previewBytes", "Bytes",
                             &loadedBytes, 8, 64, 8)
    };

    Check(UI::Detail::ParseSettings(buffer, reordered, 3, &legacy) &&
          !legacy, "load keyed data after reordering settings");
    Check(loadedPreview && loadedBytes == 32 && loadedMode == 2,
          "reordered settings retain the matching values");

    // New keys retain their defaults, and removed keys are ignored.
    bool added = true;
    UI::Setting withAdded[] = {
        UI::Setting::Boolean("showPreview", "Preview", &loadedPreview),
        UI::Setting::Integer("previewBytes", "Bytes", &loadedBytes,
                             8, 64, 8),
        UI::Setting::Enumeration("previewFormat", "Format",
                                 &loadedMode, options, 2),
        UI::Setting::Boolean("newSetting", "New setting", &added)
    };
    added = false;
    Check(UI::Detail::ParseSettings(buffer, withAdded, 4, &legacy) &&
          !legacy && !added,
          "new settings keep their defaults when no stored key exists");

    bool oldValue = true;
    int oldNumber = 12;
    UI::Setting removedVersion[] = {
        UI::Setting::Boolean("showPreview", "Preview", &oldValue),
        UI::Setting::Integer("someOtherKey", "Other", &oldNumber,
                             0, 100, 1)
    };
    Check(UI::Detail::ParseSettings(buffer, removedVersion, 2, &legacy) &&
          oldValue && oldNumber == 12,
          "unknown stored keys are ignored");

    // Existing UIST1 values are still loaded by position.
    bool legacyPreview = false;
    int legacyBytes = 8;
    UI::Setting legacySettings[] = {
        UI::Setting::Boolean("showPreview", "Preview", &legacyPreview),
        UI::Setting::Integer("previewBytes", "Bytes", &legacyBytes,
                             8, 64, 8)
    };
    Check(UI::Detail::ParseSettings("UIST1;2;B1;I32;",
                                    legacySettings, 2, &legacy) && legacy,
          "load legacy UIST1 settings");
    Check(legacyPreview && legacyBytes == 32,
          "legacy values are restored");

    // Legacy parsing remains strict and atomic on malformed input.
    legacyPreview = false;\n    legacyBytes = 8;
    legacyPreview = false;
    legacyBytes = 8;
    Check(!UI::Detail::ParseSettings("UIST1;2;B1;Ix;",
                                     legacySettings, 2, &legacy) &&
          !legacyPreview && legacyBytes == 8,
          "malformed legacy data does not partially update settings");

    // A malformed keyed payload is also rejected without partial updates.
    loadedPreview = false;
    loadedBytes = 8;
    loadedMode = 0;
    Check(!UI::Detail::ParseSettings(
              "UIST2;2;11:showPreviewB1;12:previewBytesI32",
              reordered, 3, &legacy) &&
          !loadedPreview && loadedBytes == 8 && loadedMode == 0,
          "truncated keyed data is rejected atomically");

    UI::Setting duplicateKeys[] = {
        UI::Setting::Boolean("same", "First", &preview),
        UI::Setting::Boolean("same", "Second", &loadedPreview)
    };
    Check(!UI::Detail::ValidSettingDefinitions(duplicateKeys, 2) &&
          !UI::Detail::SerializeSettings(duplicateKeys, 2, buffer, &size),
          "duplicate setting keys are rejected");

    // Explicit IDs are preferred; legacy factories use the setting name.
    UI::Setting named = UI::Setting::Boolean("Visible", &preview);
    Check(Equals(named.id, 0),
          "legacy factory overload remains source compatible");
    Check(UI::Detail::ValidSettingDefinitions(&named, 1),
          "setting name is accepted as a fallback storage key");
    UI::Setting identified =
        UI::Setting::Boolean("pref.visible", "Visible", &preview);
    Check(Equals(identified.id, "pref.visible") &&
          UI::Detail::ValidSettingDefinitions(&identified, 1),
          "explicit stable setting ID is supported");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
