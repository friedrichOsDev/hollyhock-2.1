#include <ui/storage.hpp>

#include <sdk/os/file.hpp>
#include <sdk/os/mem.hpp>
#include <stddef.h>

namespace UI {
namespace {

int Length(const char *text) {
    int length = 0;
    if (text) {
        while (text[length])
            ++length;
    }
    return length;
}

char Upper(char value) {
    if (value >= 'a' && value <= 'z')
        return char(value - ('a' - 'A'));
    return value;
}

bool IsAlphaNumeric(char value) {
    const char upper = Upper(value);
    return (upper >= 'A' && upper <= 'Z') ||
           (upper >= '0' && upper <= '9');
}

bool ValidVariableName(const char *name) {
    if (!name || !name[0])
        return false;

    for (int i = 0; name[i]; ++i) {
        if (!IsAlphaNumeric(name[i]))
            return false;
    }

    return true;
}

} // namespace

PersistentStore::PersistentStore()
    : m_open(false) {
    m_folder[0] = 0;
}

PersistentStore::~PersistentStore() {
    Close();
}

int PersistentStore::Open(const char *folder) {
    Close();

    const int length = Length(folder);
    if (length < 1 || length > 8)
        return EINVAL;

    char normalized[9];
    for (int i = 0; i < length; ++i) {
        if (!IsAlphaNumeric(folder[i]))
            return EINVAL;
        normalized[i] = Upper(folder[i]);
    }
    normalized[length] = 0;

    uint8_t folderIndex = 0;
    const int result = MCS_CreateFolder(normalized, &folderIndex);
    if (result != 0 && result != MCS_FOLDER_EXISTS)
        return result;

    for (int i = 0; i <= length; ++i)
        m_folder[i] = normalized[i];

    m_open = true;
    return 0;
}

void PersistentStore::Close() {
    m_open = false;
    m_folder[0] = 0;
}

bool PersistentStore::IsOpen() const {
    return m_open;
}

const char *PersistentStore::Folder() const {
    return m_open ? m_folder : 0;
}

int PersistentStore::Load(const char *name, uint8_t *type, void *buffer,
                          uint32_t capacity, uint32_t *actualSize) const {
    if (type)
        *type = 0;
    if (actualSize)
        *actualSize = 0;

    if (!m_open)
        return MCS_NO_FOLDER;
    if (!ValidVariableName(name) || !type || (!buffer && capacity != 0))
        return EINVAL;

    uint8_t storedType = 0;
    char *storedName = 0;
    void *storedData = 0;
    uint32_t storedSize = 0;

    const int result = MCS_GetVariable(
        m_folder, name, &storedType, &storedName, &storedData, &storedSize);
    if (result != 0)
        return result;

    // Preserve the required metadata when the caller's buffer is too small.
    *type = storedType;
    if (actualSize)
        *actualSize = storedSize;

    if (storedSize > capacity)
        return EOUTOFBOUND;
    if (storedSize != 0 && (!storedData || !buffer))
        return EUNFORMAT;

    const uint8_t *source = static_cast<const uint8_t *>(storedData);
    uint8_t *destination = static_cast<uint8_t *>(buffer);
    for (uint32_t i = 0; i < storedSize; ++i)
        destination[i] = source[i];

    return 0;
}

int PersistentStore::Save(const char *name, uint8_t type,
                          const void *data, uint32_t size) {
    if (!m_open)
        return MCS_NO_FOLDER;
    if (!ValidVariableName(name) || !data || size == 0)
        return EINVAL;

    // MCS stores payloads in 32-bit units. Round the payload size up to a
    // multiple of four and copy it into malloc-aligned memory before calling
    // MCS_SetVariable(). Padding bytes are initialized to zero.
    if (size > 0xFFFFFFFCU)
        return EOUTOFBOUND;

    const uint32_t alignedSize = (size + 3U) & ~3U;
    uint8_t *alignedData = static_cast<uint8_t *>(malloc(alignedSize));
    if (!alignedData)
        return ENOMEM;

    const uint8_t *source = static_cast<const uint8_t *>(data);
    for (uint32_t i = 0; i < size; ++i)
        alignedData[i] = source[i];
    for (uint32_t i = size; i < alignedSize; ++i)
        alignedData[i] = 0;

    const int result = MCS_SetVariable(
        m_folder, name, type, alignedSize, alignedData);
    free(alignedData);

    return result;
}

} // namespace UI
