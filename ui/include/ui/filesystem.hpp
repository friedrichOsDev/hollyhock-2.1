#pragma once

#include <sdk/os/file.hpp>
#include <stdint.h>

namespace UI {

class File {
public:
    File();
    ~File();

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    // Paths are volume-relative and may use either slash style.
    // The wrapper always opens files with OPEN_READ.
    int Open(const char *path);
    int Close();
    int Read(void *buffer, int count);
    int Seek(int offset, int whence = SEEK_SET);
    int GetSize(uint32_t *size) const;

    bool IsOpen() const;

private:
    int m_fd;
};

class FileSystem {
public:
    // Converts "fls0/folder/file.txt", "/fls0/file.txt", and
    // "\\fls0\\file.txt" to the SDK's native path format.
    // Only printable ASCII path components are accepted. FAT-reserved
    // characters and parent traversal are rejected.
    // The normalized path is limited to 255 characters plus the null byte.
    static int NormalizePath(const char *input, char *output, int capacity);
    static int Stat(const char *path, struct stat *info);
    static bool Exists(const char *path);
};

struct DirectoryEntry {
    char name[128];
    bool isDirectory;
    uint32_t size;
};

class Directory {
public:
    Directory();
    ~Directory();

    Directory(const Directory &) = delete;
    Directory &operator=(const Directory &) = delete;

    int Open(const char *path);

    // Returns zero for an entry and a negative SDK error at end-of-search
    // or on failure. Call Close() when enumeration is complete.
    int Next(DirectoryEntry *entry);

    int Close();
    bool IsOpen() const;

private:
    int m_handle;
    bool m_open;
    bool m_hasFirst;
    wchar_t m_firstName[128];
    struct findInfo m_firstInfo;
};

} // namespace UI
