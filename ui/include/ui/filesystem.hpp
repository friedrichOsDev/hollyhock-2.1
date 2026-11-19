#pragma once

#include <sdk/os/file.hpp>
#include <stdint.h>

namespace UI {

enum FileMode {
    FileRead = OPEN_READ,
    FileWrite = OPEN_WRITE,
    FileReadWrite = OPEN_READ | OPEN_WRITE,
    FileCreate = OPEN_WRITE | OPEN_CREATE,
    FileAppend = OPEN_WRITE | OPEN_APPEND
};

class File {
public:
    File();
    ~File();
    File(const File &) = delete;
    File &operator=(const File &) = delete;

    // Paths are volume-relative and may use either slash style.
    // Open returns a native descriptor or a negative SDK error code.
    int Open(const char *path, int flags = OPEN_READ);
    int Close();
    int Read(void *buffer, int count);
    int Write(const void *buffer, int count);
    int Seek(int offset, int whence = SEEK_SET);
    int GetSize(uint32_t *size) const;
    bool IsOpen() const;
    int Handle() const;

private:
    int m_fd;
};

class FileSystem {
public:
    // Converts paths such as "fls0/folder/file.txt", "/fls0/file.txt",
    // or "\\fls0\\file.txt" to native "\fls0\file.txt".
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
