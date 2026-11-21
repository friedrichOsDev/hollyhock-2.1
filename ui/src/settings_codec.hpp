#pragma once

#include <stdint.h>
#include <ui/types.hpp>

namespace UI {
namespace Detail {

enum {
    MaxPersistentSettings = 32,
    MaxSettingKeyLength = 63,
    PersistenceBufferSize = 512
};

bool ValidSettingDefinition(const Setting &setting);
bool ValidSettingValue(const Setting &setting);
bool ValidSettingDefinitions(const Setting *settings, int count);

bool SerializeSettings(const Setting *settings, int count,
                       char *output, uint32_t *size);

// Loads both UIST1 (positional legacy format) and UIST2 (keyed format).
// legacyFormat is set to true only when a UIST1 payload was loaded.
bool ParseSettings(const char *data, const Setting *settings, int count,
                   bool *legacyFormat = 0);

} // namespace Detail
} // namespace UI
