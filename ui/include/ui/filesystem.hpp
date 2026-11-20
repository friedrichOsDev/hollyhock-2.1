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

    // The UI wrapper is intentionally read-only. Paths are volume-relative
    // and may use either slash style.
    // Open returns a native descriptor or a negative SDK error code.
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
    // Converts paths such as "fls0/folder/file.txt", "/fls0/file.txt",
    // or "\\fls0\\file.txt" to native "\\fls0\\file.txt".
    // Rejects other volumes and parent traversal.
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
    // Next returns 0 for an entry and a negative SDK code when finished/error.
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
