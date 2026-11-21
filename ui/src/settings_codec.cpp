#include "settings_codec.hpp"

namespace UI {
namespace Detail {
namespace {

int Length(const char *text) {
    int length = 0;
    if (text) {
        while (text[length])
            ++length;
    }
    return length;
}

const char *SettingKey(const Setting &setting) {
    return setting.id ? setting.id : setting.name;
}

bool ValidKey(const char *key, int *keyLength) {
    if (!key || !key[0] || !keyLength)
        return false;

    const int length = Length(key);
    if (length > MaxSettingKeyLength)
        return false;

    for (int i = 0; i < length; ++i) {
        const unsigned char character =
            static_cast<unsigned char>(key[i]);
        if (character < 32 || character > 126)
            return false;
    }

    *keyLength = length;
    return true;
}

bool EqualKey(const char *left, const char *right) {
    if (!left || !right)
        return false;

    int i = 0;
    while (left[i] && right[i] && left[i] == right[i])
        ++i;
    return left[i] == right[i];
}

bool AddChar(char *output, int *position, char value) {
    if (*position >= PersistenceBufferSize - 1)
        return false;
    output[(*position)++] = value;
    output[*position] = 0;
    return true;
}

bool AddText(char *output, int *position, const char *value) {
    while (*value) {
        if (!AddChar(output, position, *value++))
            return false;
    }
    return true;
}

bool AddUnsigned(char *output, int *position, unsigned int value) {
    char digits[12];
    int count = 0;
    do {
        digits[count++] = char('0' + value % 10U);
        value /= 10U;
    } while (value && count < static_cast<int>(sizeof(digits)));

    while (count) {
        if (!AddChar(output, position, digits[--count]))
            return false;
    }
    return true;
}

bool AddSigned(char *output, int *position, int value) {
    const bool negative = value < 0;
    const unsigned int magnitude = negative
        ? static_cast<unsigned int>(-(value + 1)) + 1U
        : static_cast<unsigned int>(value);

    if (negative && !AddChar(output, position, '-'))
        return false;
    return AddUnsigned(output, position, magnitude);
}

bool ParseUnsigned(const char **cursor, unsigned int *value,
                   char terminator) {
    const char *position = *cursor;
    if (*position < '0' || *position > '9')
        return false;

    unsigned int number = 0;
    while (*position >= '0' && *position <= '9') {
        const unsigned int digit =
            static_cast<unsigned int>(*position - '0');
        if (number > (0xFFFFFFFFU - digit) / 10U)
            return false;
        number = number * 10U + digit;
        ++position;
    }

    if (*position != terminator)
        return false;

    *value = number;
    *cursor = position + 1;
    return true;
}

bool ParseSigned(const char **cursor, int *value) {
    const char *position = *cursor;
    bool negative = false;
    if (*position == '-') {
        negative = true;
        ++position;
    }

    if (*position < '0' || *position > '9')
        return false;

    unsigned int number = 0;
    const unsigned int limit = negative ? 2147483648U : 2147483647U;
    while (*position >= '0' && *position <= '9') {
        const unsigned int digit =
            static_cast<unsigned int>(*position - '0');
        if (number > (limit - digit) / 10U)
            return false;
        number = number * 10U + digit;
        ++position;
    }

    if (*position != ';')
        return false;
    ++position;

    if (negative) {
        *value = number == 2147483648U
            ? (-2147483647 - 1)
            : -static_cast<int>(number);
    } else {
        *value = static_cast<int>(number);
    }

    *cursor = position;
    return true;
}

bool ValidDefinitionsWithoutKeys(const Setting *settings, int count) {
    if (!settings || count <= 0 || count > MaxPersistentSettings)
        return false;

    for (int i = 0; i < count; ++i) {
        if (!ValidSettingDefinition(settings[i]))
            return false;
    }
    return true;
}

bool ParseLegacySettings(const char *data, const Setting *settings,
                         int count) {
    if (!data || !ValidDefinitionsWithoutKeys(settings, count))
        return false;

    const char *position = data;
    const char prefix[] = "UIST1;";
    for (int i = 0; i < 6; ++i) {
        if (*position++ != prefix[i])
            return false;
    }

    unsigned int storedCount = 0;
    if (!ParseUnsigned(&position, &storedCount, ';') ||
        storedCount != static_cast<unsigned int>(count)) {
        return false;
    }

    int values[MaxPersistentSettings];
    for (int i = 0; i < count; ++i) {
        const char type = *position++;
        if (!type)
            return false;

        const Setting &setting = settings[i];
        if (setting.type == SettingAction) {
            if (type != 'A' || *position++ != ';')
                return false;
            values[i] = 0;
            continue;
        }

        int value = 0;
        if (!ParseSigned(&position, &value))
            return false;

        if ((setting.type == SettingBoolean &&
             (type != 'B' || (value != 0 && value != 1))) ||
            (setting.type == SettingInteger &&
             (type != 'I' || value < setting.minimum ||
              value > setting.maximum)) ||
            (setting.type == SettingEnum &&
             (type != 'E' || value < 0 ||
              value >= setting.optionCount)) ||
            !setting.value) {
            return false;
        }

        values[i] = value;
    }

    if (*position != 0)
        return false;

    for (int i = 0; i < count; ++i) {
        if (settings[i].type == SettingBoolean) {
            *static_cast<bool *>(settings[i].value) = values[i] != 0;
        } else if (settings[i].type == SettingInteger ||
                   settings[i].type == SettingEnum) {
            *static_cast<int *>(settings[i].value) = values[i];
        }
    }
    return true;
}

int FindSetting(const char *key, const Setting *settings, int count) {
    for (int i = 0; i < count; ++i) {
        if (EqualKey(key, SettingKey(settings[i])))
            return i;
    }
    return -1;
}

bool ValueMatchesSetting(const Setting &setting, char storedType,
                         int value) {
    if (setting.type == SettingBoolean)
        return storedType == 'B' && (value == 0 || value == 1);
    if (setting.type == SettingInteger)
        return storedType == 'I' &&
               value >= setting.minimum && value <= setting.maximum;
    if (setting.type == SettingEnum)
        return storedType == 'E' &&
               value >= 0 && value < setting.optionCount;
    return false;
}

bool ParseKeyedSettings(const char *data, const Setting *settings,
                        int count) {
    if (!data || !ValidSettingDefinitions(settings, count))
        return false;

    const char *position = data;
    const char prefix[] = "UIST2;";
    for (int i = 0; i < 6; ++i) {
        if (*position++ != prefix[i])
            return false;
    }

    unsigned int storedCount = 0;
    if (!ParseUnsigned(&position, &storedCount, ';') ||
        storedCount > MaxPersistentSettings) {
        return false;
    }

    char seenKeys[MaxPersistentSettings][MaxSettingKeyLength + 1];
    unsigned int seenLengths[MaxPersistentSettings];
    int values[MaxPersistentSettings];
    bool hasValue[MaxPersistentSettings];
    for (int i = 0; i < count; ++i)
        hasValue[i] = false;

    for (unsigned int entry = 0; entry < storedCount; ++entry) {
        unsigned int keyLength = 0;
        if (!ParseUnsigned(&position, &keyLength, ':') ||
            keyLength == 0 || keyLength > MaxSettingKeyLength) {
            return false;
        }

        char key[MaxSettingKeyLength + 1];
        for (unsigned int i = 0; i < keyLength; ++i) {
            if (!position[i])
                return false;
            const unsigned char character =
                static_cast<unsigned char>(position[i]);
            if (character < 32 || character > 126)
                return false;
            key[i] = position[i];
        }
        key[keyLength] = 0;
        position += keyLength;

        for (unsigned int previous = 0; previous < entry; ++previous) {
            if (seenLengths[previous] == keyLength &&
                EqualKey(seenKeys[previous], key)) {
                return false;
            }
        }
        for (unsigned int i = 0; i <= keyLength; ++i)
            seenKeys[entry][i] = key[i];
        seenLengths[entry] = keyLength;

        const char storedType = *position++;
        if (!storedType)
            return false;

        int value = 0;
        if (storedType == 'A') {
            if (*position++ != ';')
                return false;
        } else if (storedType == 'B' || storedType == 'I' ||
                   storedType == 'E') {
            if (!ParseSigned(&position, &value))
                return false;
            if (storedType == 'B' && value != 0 && value != 1)
                return false;
        } else {
            return false;
        }

        const int settingIndex = FindSetting(key, settings, count);
        if (settingIndex < 0)
            continue;

        const Setting &setting = settings[settingIndex];
        if (setting.type == SettingAction) {
            // An old value with this key is harmless after changing the
            // setting into an action. Leave the action untouched.
            continue;
        }

        if (!ValueMatchesSetting(setting, storedType, value))
            continue;

        values[settingIndex] = value;
        hasValue[settingIndex] = true;
    }

    if (*position != 0)
        return false;

    for (int i = 0; i < count; ++i) {
        if (!hasValue[i])
            continue;

        if (settings[i].type == SettingBoolean) {
            *static_cast<bool *>(settings[i].value) = values[i] != 0;
        } else if (settings[i].type == SettingInteger ||
                   settings[i].type == SettingEnum) {
            *static_cast<int *>(settings[i].value) = values[i];
        }
    }
    return true;
}

} // namespace

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
    if (!ValidDefinitionsWithoutKeys(settings, count))
        return false;

    for (int i = 0; i < count; ++i) {
        int keyLength = 0;
        if (!ValidKey(SettingKey(settings[i]), &keyLength))
            return false;

        for (int j = 0; j < i; ++j) {
            if (EqualKey(SettingKey(settings[i]), SettingKey(settings[j])))
                return false;
        }
    }
    return true;
}

bool SerializeSettings(const Setting *settings, int count,
                       char *output, uint32_t *size) {
    if (!ValidSettingDefinitions(settings, count) || !output || !size)
        return false;

    int position = 0;
    output[0] = 0;
    if (!AddText(output, &position, "UIST2;") ||
        !AddUnsigned(output, &position, static_cast<unsigned int>(count)) ||
        !AddChar(output, &position, ';')) {
        return false;
    }

    for (int i = 0; i < count; ++i) {
        const Setting &setting = settings[i];
        if (!ValidSettingValue(setting))
            return false;

        int keyLength = 0;
        const char *key = SettingKey(setting);
        if (!ValidKey(key, &keyLength) ||
            !AddUnsigned(output, &position,
                         static_cast<unsigned int>(keyLength)) ||
            !AddChar(output, &position, ':') ||
            !AddText(output, &position, key)) {
            return false;
        }

        if (setting.type == SettingAction) {
            if (!AddText(output, &position, "A;"))
                return false;
        } else if (setting.type == SettingBoolean) {
            if (!AddChar(output, &position, 'B') ||
                !AddSigned(output, &position,
                           *static_cast<bool *>(setting.value) ? 1 : 0) ||
                !AddChar(output, &position, ';')) {
                return false;
            }
        } else if (setting.type == SettingInteger) {
            if (!AddChar(output, &position, 'I') ||
                !AddSigned(output, &position,
                           *static_cast<int *>(setting.value)) ||
                !AddChar(output, &position, ';')) {
                return false;
            }
        } else if (setting.type == SettingEnum) {
            if (!AddChar(output, &position, 'E') ||
                !AddSigned(output, &position,
                           *static_cast<int *>(setting.value)) ||
                !AddChar(output, &position, ';')) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (!AddChar(output, &position, 0))
        return false;
    *size = static_cast<uint32_t>(position);
    return true;
}

bool ParseSettings(const char *data, const Setting *settings, int count,
                   bool *legacyFormat) {
    if (legacyFormat)
        *legacyFormat = false;
    if (!data)
        return false;

    if (data[0] == 'U' && data[1] == 'I' && data[2] == 'S' &&
        data[3] == 'T' && data[4] == '1' && data[5] == ';') {
        const bool result = ParseLegacySettings(data, settings, count);
        if (result && legacyFormat)
            *legacyFormat = true;
        return result;
    }

    if (data[0] == 'U' && data[1] == 'I' && data[2] == 'S' &&
        data[3] == 'T' && data[4] == '2' && data[5] == ';') {
        return ParseKeyedSettings(data, settings, count);
    }

    return false;
}

} // namespace Detail
} // namespace UI
