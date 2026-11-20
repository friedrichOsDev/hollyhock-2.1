#pragma once

#include <sdk/os/mcs.hpp>
#include <stdint.h>

namespace UI {

// Small wrapper around the calculator's MCS (Calc Vars) storage.
// Folder names are validated to 1-8 ASCII letters/digits because the SDK
// silently truncates longer folder names.
class PersistentStore {
public:
    PersistentStore();
    ~PersistentStore();
    PersistentStore(const PersistentStore &) = delete;
    PersistentStore &operator=(const PersistentStore &) = delete;

    // Creates the folder when necessary and opens it for subsequent access.
    int Open(const char *folder);
    void Close();
    bool IsOpen() const;
    const char *Folder() const;

    // Copies a variable into caller-owned memory.
    // Returns the SDK result; actualSize receives the stored byte count.
    int Load(const char *name, uint8_t *type, void *buffer,
             uint32_t capacity, uint32_t *actualSize) const;

    // Creates or replaces a variable in the opened folder.
    int Save(const char *name, uint8_t type,
             const void *data, uint32_t size);

private:
    char m_folder[9];
    bool m_open;
};

} // namespace UI
