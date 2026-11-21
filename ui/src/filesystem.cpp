#include <ui/filesystem.hpp>

namespace UI {
namespace {

int StrLen(const char *text) {
    int length = 0;
    if (text) {
        while (text[length])
            ++length;
    }
    return length;
}

char Lower(char value) {
    if (value >= 'A' && value <= 'Z')
        return char(value + ('a' - 'A'));
    return value;
}

bool IsVolumeName(const char *name, int length) {
    return length == 4 &&
           Lower(name[0]) == 'f' &&
           Lower(name[1]) == 'l' &&
           Lower(name[2]) == 's' &&
           name[3] == '0';
}

bool IsValidPathCharacter(char value) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (character < 32 || character > 126)
        return false;

    switch (character) {
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
        return false;
    default:
        return true;
    }
}

void CopyName(char *destination, const wchar_t *source, int capacity) {
    int index = 0;
    if (source) {
        while (source[index] && index < capacity - 1) {
            const unsigned int character =
                static_cast<unsigned int>(source[index]);
            destination[index] =
                (character >= 32 && character <= 126)
                    ? static_cast<char>(character)
                    : '?';
            ++index;
        }
    }
    destination[index] = 0;
}

void ToWide(const char *source, wchar_t *destination, int capacity) {
    int index = 0;
    if (source) {
        while (source[index] && index < capacity - 1) {
            destination[index] =
                static_cast<wchar_t>(
                    static_cast<unsigned char>(source[index]));
            ++index;
        }
    }
    destination[index] = 0;
}

} // namespace

File::File()
    : m_fd(-1) {}

File::~File() {
    Close();
}

int File::Open(const char *path) {
    const int closeResult = Close();
    if (closeResult < 0)
        return closeResult;

    char nativePath[256];
    const int normalizeResult =
        FileSystem::NormalizePath(path, nativePath, sizeof(nativePath));
    if (normalizeResult < 0)
        return normalizeResult;

    // The UI wrapper deliberately exposes read access only.
    const int descriptor = ::open(nativePath, OPEN_READ);
    if (descriptor < 0)
        return descriptor;

    m_fd = descriptor;
    return m_fd;
}

int File::Close() {
    if (m_fd < 0)
        return 0;

    const int descriptor = m_fd;
    m_fd = -1;
    return ::close(descriptor);
}

int File::Read(void *buffer, int count) {
    if (m_fd < 0)
        return EBADF;
    if (count < 0 || (count > 0 && !buffer))
        return EINVAL;

    return ::read(m_fd, buffer, count);
}

int File::Seek(int offset, int whence) {
    if (m_fd < 0)
        return EBADF;

    return ::lseek(m_fd, offset, whence);
}

int File::GetSize(uint32_t *size) const {
    if (m_fd < 0)
        return EBADF;
    if (!size)
        return EINVAL;

    struct stat info;
    const int result = ::fstat(m_fd, &info);
    if (result < 0)
        return result;

    *size = info.fileSize;
    return 0;
}

bool File::IsOpen() const {
    return m_fd >= 0;
}

int FileSystem::NormalizePath(const char *input, char *output,
                              int capacity) {
    if (!output)
        return EINVAL;
    if (capacity <= 0)
        return ENAMETOOLONG;
    if (!input) {
        output[0] = 0;
        return EINVAL;
    }

    // Copy before clearing output so input and output may safely alias.
    const int inputLength = StrLen(input);
    if (inputLength <= 0) {
        output[0] = 0;
        return EINVAL;
    }
    if (inputLength >= 256) {
        output[0] = 0;
        return ENAMETOOLONG;
    }

    char source[256];
    for (int i = 0; i <= inputLength; ++i)
        source[i] = input[i];
    output[0] = 0;

    int index = 0;
    while (index < inputLength &&
           (source[index] == '/' || source[index] == '\\')) {
        ++index;
    }
    if (index >= inputLength)
        return EINVAL;

    const int volumeStart = index;
    while (index < inputLength &&
           source[index] != '/' && source[index] != '\\') {
        ++index;
    }
    if (!IsVolumeName(source + volumeStart, index - volumeStart))
        return ENOVOLUME;

    char normalized[256];
    int normalizedLength = 0;
    const char root[] = "\\fls0\\";
    for (int i = 0; i < 6; ++i)
        normalized[normalizedLength++] = root[i];

    while (index < inputLength) {
        while (index < inputLength &&
               (source[index] == '/' || source[index] == '\\')) {
            ++index;
        }
        if (index >= inputLength)
            break;

        const int componentStart = index;
        while (index < inputLength &&
               source[index] != '/' && source[index] != '\\') {
            ++index;
        }

        const int componentLength = index - componentStart;
        if (componentLength == 1 && source[componentStart] == '.')
            continue;
        if (componentLength == 2 &&
            source[componentStart] == '.' &&
            source[componentStart + 1] == '.') {
            return EACCES;
        }

        for (int i = 0; i < componentLength; ++i) {
            if (!IsValidPathCharacter(source[componentStart + i]))
                return EINVAL;
        }

        const char lastCharacter = source[index - 1];
        if (lastCharacter == '.' || lastCharacter == ' ')
            return EINVAL;

        if (normalizedLength > 6 &&
            normalized[normalizedLength - 1] != '\\') {
            if (normalizedLength >= (int)sizeof(normalized) - 1)
                return ENAMETOOLONG;
            normalized[normalizedLength++] = '\\';
        }

        if (normalizedLength + componentLength >=
            (int)sizeof(normalized)) {
            return ENAMETOOLONG;
        }

        for (int i = 0; i < componentLength; ++i) {
            normalized[normalizedLength++] =
                source[componentStart + i];
        }
    }

    if (normalizedLength + 1 > capacity)
        return ENAMETOOLONG;

    normalized[normalizedLength] = 0;
    for (int i = 0; i <= normalizedLength; ++i)
        output[i] = normalized[i];

    return 0;
}

int FileSystem::Stat(const char *path, struct stat *info) {
    if (!info)
        return EINVAL;

    char nativePath[256];
    const int normalizeResult =
        NormalizePath(path, nativePath, sizeof(nativePath));
    if (normalizeResult < 0)
        return normalizeResult;

    return ::stat(nativePath, info);
}

bool FileSystem::Exists(const char *path) {
    struct stat info;
    return Stat(path, &info) == 0;
}

Directory::Directory()
    : m_handle(-1),
      m_open(false),
      m_hasFirst(false) {}

Directory::~Directory() {
    Close();
}

int Directory::Open(const char *path) {
    const int closeResult = Close();
    if (closeResult < 0)
        return closeResult;

    char nativePath[256];
    const int normalizeResult =
        FileSystem::NormalizePath(path, nativePath, sizeof(nativePath));
    if (normalizeResult < 0)
        return normalizeResult;

    int pathLength = StrLen(nativePath);
    if (pathLength + 2 >= (int)sizeof(nativePath))
        return ENAMETOOLONG;

    if (nativePath[pathLength - 1] != '\\')
        nativePath[pathLength++] = '\\';
    nativePath[pathLength++] = '*';
    nativePath[pathLength] = 0;

    wchar_t searchPath[256];
    ToWide(nativePath, searchPath, sizeof(searchPath) / sizeof(searchPath[0]));

    const int result =
        ::findFirst(searchPath, &m_handle, m_firstName, &m_firstInfo);
    if (result < 0) {
        m_handle = -1;
        m_open = false;
        m_hasFirst = false;
        return result;
    }

    m_open = true;
    m_hasFirst = true;
    return 0;
}

int Directory::Next(DirectoryEntry *entry) {
    if (!m_open)
        return EBADF;
    if (!entry)
        return EINVAL;

    entry->name[0] = 0;
    entry->isDirectory = false;
    entry->size = 0;

    wchar_t name[128];
    struct findInfo info;

    if (m_hasFirst) {
        for (int i = 0; i < 128; ++i)
            name[i] = m_firstName[i];
        info = m_firstInfo;
        m_hasFirst = false;
    } else {
        const int result = ::findNext(m_handle, name, &info);
        if (result < 0)
            return result;
    }

    CopyName(entry->name, name, sizeof(entry->name));
    entry->isDirectory = info.type == findInfo::EntryTypeDirectory;
    entry->size = info.size;
    return 0;
}

int Directory::Close() {
    if (!m_open)
        return 0;

    const int handle = m_handle;
    m_handle = -1;
    m_open = false;
    m_hasFirst = false;

    return ::findClose(handle);
}

bool Directory::IsOpen() const {
    return m_open;
}

} // namespace UI
