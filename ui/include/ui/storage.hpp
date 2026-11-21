#pragma once

#include <sdk/os/mcs.hpp>
#include <stdint.h>

namespace UI {

// Small wrapper around the calculator's MCS (Calc Vars) storage.
// Folder names are restricted to 1-8 ASCII letters or digits because the
// SDK silently truncates longer names.
class PersistentStore {
public:
    PersistentStore();
    ~PersistentStore();

    PersistentStore(const PersistentStore &) = delete;
    PersistentStore &operator=(const PersistentStore &) = delete;

    // Creates the folder when necessary and opens it for subsequent access.
    // Returns zero on success; MCS errors are positive SDK constants.
    int Open(const char *folder);

    void Close();
    bool IsOpen() const;
    const char *Folder() const;

    // Copies a variable into caller-owned memory. On success, actualSize
    // reports the stored size, including MCS 4-byte alignment padding.
    // When the buffer is too small, type and actualSize still describe the
    // stored variable and EOUTOFBOUND is returned.
    int Load(const char *name, uint8_t *type, void *buffer,
             uint32_t capacity, uint32_t *actualSize) const;

    // Creates or replaces a variable. Data is copied to aligned storage and
    // padded with zero bytes to a multiple of four before the MCS call.
    int Save(const char *name, uint8_t type,
             const void *data, uint32_t size);

private:
    char m_folder[9];
    bool m_open;
};

} // namespace UI
