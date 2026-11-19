#include <ui/filesystem.hpp>
#include <stddef.h>

namespace UI {
namespace {
int StrLen(const char *s) {
    int n = 0;
    if (s) while (s[n]) ++n;
    return n;
}
char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c; }
bool IsVolumeName(const char *s, int n) {
    return n == 4 && Lower(s[0]) == 'f' && Lower(s[1]) == 'l' && Lower(s[2]) == 's' && s[3] == '0';
}
void CopyName(char *out, const wchar_t *in, int cap) {
    int i = 0;
    if (in) while (in[i] && i < cap - 1) {
        unsigned int c = (unsigned int)in[i];
        out[i] = (c >= 32 && c <= 126) ? (char)c : '?';
        ++i;
    }
    out[i] = 0;
}
void ToWide(const char *in, wchar_t *out, int cap) {
    int i = 0;
    if (in) while (in[i] && i < cap - 1) {
        out[i] = (wchar_t)(unsigned char)in[i];
        ++i;
    }
    out[i] = 0;
}
}
File::File() : m_fd(-1) {}
File::~File() { Close(); }
int File::Open(const char *path, int flags) {
    Close();
    char native[256];
    int ret = FileSystem::NormalizePath(path, native, sizeof(native));
    if (ret < 0) return ret;
    m_fd = ::open(native, flags);
    return m_fd;
}
int File::Close() {
    if (m_fd < 0) return 0;
    int fd = m_fd;
    m_fd = -1;
    return ::close(fd);
}
int File::Read(void *buffer, int count) {
    if (m_fd < 0) return EBADF;
    if (!buffer || count < 0) return EINVAL;
    return ::read(m_fd, buffer, count);
}
int File::Write(const void *buffer, int count) {
    if (m_fd < 0) return EBADF;
    if (!buffer || count < 0) return EINVAL;
    return ::write(m_fd, buffer, count);
}
int File::Seek(int offset, int whence) {
    if (m_fd < 0) return EBADF;
    return ::lseek(m_fd, offset, whence);
}
int File::GetSize(uint32_t *size) const {
    if (m_fd < 0) return EBADF;
    if (!size) return EINVAL;
    struct stat info;
    int ret = ::fstat(m_fd, &info);
    if (ret < 0) return ret;
    *size = info.fileSize;
    return ret;
}
bool File::IsOpen() const { return m_fd >= 0; }
int File::Handle() const { return m_fd; }

int FileSystem::NormalizePath(const char *input, char *output, int capacity) {
    if (!input || !output || capacity < 7) return EINVAL;
    output[0] = 0;
    int length = StrLen(input);
    if (length <= 0) return EINVAL;

    int i = 0;
    while (i < length && (input[i] == '/' || input[i] == '\\')) ++i;
    if (i >= length) return EINVAL;
    int volumeStart = i;
    while (i < length && input[i] != '/' && input[i] != '\\') ++i;
    if (!IsVolumeName(input + volumeStart, i - volumeStart)) return ENOVOLUME;

    int out = 0;
    const char root[] = "\\fls0\\";
    for (int k = 0; k < 6; ++k) {
        if (out >= capacity - 1) return ENAMETOOLONG;
        output[out++] = root[k];
    }
    while (i < length) {
        while (i < length && (input[i] == '/' || input[i] == '\\')) ++i;
        if (i >= length) break;
        int start = i;
        while (i < length && input[i] != '/' && input[i] != '\\') ++i;
        int componentLength = i - start;
        if (componentLength == 1 && input[start] == '.') continue;
        if (componentLength == 2 && input[start] == '.' && input[start + 1] == '.') return EACCES;
        if (out > 6 && output[out - 1] != '\\') {
            if (out >= capacity - 1) return ENAMETOOLONG;
            output[out++] = '\\';
        }
        if (out + componentLength >= capacity) return ENAMETOOLONG;
        for (int k = 0; k < componentLength; ++k) output[out++] = input[start + k];
    }
    output[out] = 0;
    return 0;
}
int FileSystem::Stat(const char *path, struct stat *info) {
    if (!info) return EINVAL;
    char native[256];
    int ret = NormalizePath(path, native, sizeof(native));
    if (ret < 0) return ret;
    return ::stat(native, info);
}
bool FileSystem::Exists(const char *path) {
    struct stat info;
    return Stat(path, &info) == 0;
}

Directory::Directory() : m_handle(-1), m_open(false), m_hasFirst(false) {}
Directory::~Directory() { Close(); }
int Directory::Open(const char *path) {
    Close();
    char native[256];
    int ret = FileSystem::NormalizePath(path, native, sizeof(native));
    if (ret < 0) return ret;
    int n = StrLen(native);
    if (n + 2 >= (int)sizeof(native)) return ENAMETOOLONG;
    if (native[n - 1] != '\\') native[n++] = '\\';
    native[n++] = '*';
    native[n] = 0;
    wchar_t search[256];
    ToWide(native, search, 256);
    ret = ::findFirst(search, &m_handle, m_firstName, &m_firstInfo);
    if (ret < 0) {
        m_handle = -1;
        m_open = false;
        m_hasFirst = false;
        return ret;
    }
    m_open = true;
    m_hasFirst = true;
    return 0;
}
int Directory::Next(DirectoryEntry *entry) {
    if (!m_open) return EBADF;
    if (!entry) return EINVAL;
    wchar_t name[128];
    struct findInfo info;
    if (m_hasFirst) {
        for (int i = 0; i < 128; ++i) name[i] = m_firstName[i];
        info = m_firstInfo;
        m_hasFirst = false;
    } else {
        int ret = ::findNext(m_handle, name, &info);
        if (ret < 0) return ret;
    }
    CopyName(entry->name, name, sizeof(entry->name));
    entry->isDirectory = info.type == findInfo::EntryTypeDirectory;
    entry->size = info.size;
    return 0;
}
int Directory::Close() {
    if (!m_open) return 0;
    int h = m_handle;
    m_handle = -1;
    m_open = false;
    m_hasFirst = false;
    return ::findClose(h);
}
bool Directory::IsOpen() const { return m_open; }
} // namespace UI
