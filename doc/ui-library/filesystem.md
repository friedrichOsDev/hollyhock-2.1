# File System API

The UI Library provides read-only C++ wrappers around the read operations in the Hollyhock SDK. They are declared in:

~~~cpp
#include <ui/filesystem.hpp>
~~~

The header is also included by ui/ui.hpp. The wrappers use the SDK file system; they do not provide a separate storage layer.

## Read-only design

The UI Library deliberately exposes no file-writing API. Its public interface has no write modes, Write(), create, delete, rename, move, or directory-creation operations.

This restriction applies only to the UI Library. The underlying Hollyhock SDK is unchanged and still provides its native file-system functions, including write and modification functions. Applications that intentionally need those lower-level operations can call the SDK directly, outside the UI Library. The built-in UI file browser never modifies files.

## Storage volume and path format

The current wrapper supports the calculator's fls0 volume only. It accepts forward slashes and backslashes and converts the path to the native form used by the SDK.

| Input | Normalized native path |
| --- | --- |
| fls0/folder/file.txt | \fls0\folder\file.txt |
| /fls0/file.txt | \fls0\file.txt |
| \fls0\file.txt | \fls0\file.txt |

In C++ source code, escape each backslash in a string literal:

~~~cpp
"\\fls0\\file.txt"
~~~

A path without a leading slash is also valid: "fls0/file.txt".

### Path validation

NormalizePath() performs these checks:

- The input must identify the fls0 volume. Other volume names are rejected.
- Repeated separators are collapsed.
- A path component equal to . is skipped.
- A path component equal to .. is rejected to prevent parent-directory traversal.
- The output uses backslashes and begins with \fls0\.
- The output buffer must be large enough. File and directory wrappers use a 256-byte path buffer.

~~~cpp
char nativePath[256];
int result = UI::FileSystem::NormalizePath(
    "fls0/folder/file.txt",
    nativePath,
    sizeof(nativePath)
);
~~~

Use a buffer with room for the terminating null byte. A result of zero indicates success; a negative result is an SDK error code.

## File wrapper

UI::File manages one SDK file descriptor. It is non-copyable. Its destructor calls Close(), so an open descriptor is closed when the object leaves scope.

Open() accepts only a path and always uses the SDK's OPEN_READ flag. The caller cannot pass flags through the UI wrapper.

~~~cpp
class File {
public:
    int Open(const char *path);
    int Close();
    int Read(void *buffer, int count);
    int Seek(int offset, int whence = SEEK_SET);
    int GetSize(uint32_t *size) const;
    bool IsOpen() const;
    int Handle() const;
};
~~~

| Method | Description |
| --- | --- |
| Open(path) | Opens the normalized path for reading only. Returns the native descriptor or a negative SDK error code. Closes any descriptor already held by the object first. |
| Close() | Closes the descriptor. Returns 0 if already closed or if the SDK close operation succeeds. |
| Read(buffer, count) | Reads up to count bytes. Returns the number of bytes read or a negative error code. |
| Seek(offset, whence = SEEK_SET) | Changes the file position. Returns the new position or a negative error code. |
| GetSize(size) | Retrieves file metadata and stores the file size in bytes. |
| IsOpen() | Returns whether the object currently holds a valid descriptor. |
| Handle() | Returns the native descriptor, or -1 when closed. |

A read may return fewer bytes than requested. A return value of zero means that no more bytes were read (normally end of file). Always check negative return values before using a result as a byte count.

### Reading a file

~~~cpp
#include <ui/filesystem.hpp>

static void ReadTextFile(const char *path) {
    UI::File file;
    int descriptor = file.Open(path);
    if (descriptor < 0)
        return;

    uint32_t fileSize = 0;
    if (file.GetSize(&fileSize) < 0)
        return;

    char buffer[128];
    int bytesToRead = fileSize < sizeof(buffer) - 1
        ? (int)fileSize
        : (int)sizeof(buffer) - 1;

    int bytesRead = file.Read(buffer, bytesToRead);
    if (bytesRead < 0)
        return;

    buffer[bytesRead] = '\0';
    // Use the bytes in buffer. Larger files are only read partially here.
}
~~~

The destructor closes the file when the function returns, including on early returns. This example assumes that the file contains text. For binary files, treat the buffer as bytes and do not use string functions on it.

## FileSystem helper methods

| Method | Description |
| --- | --- |
| NormalizePath(input, output, capacity) | Validates and normalizes a path. Returns 0 on success or a negative SDK error code. |
| Stat(path, info) | Retrieves file metadata into a struct stat. Returns the SDK result code. |
| Exists(path) | Returns true when Stat() succeeds. Returns false for any error, not only when a file is missing. |

~~~cpp
struct stat info;
if (UI::FileSystem::Stat("fls0/readme.txt", &info) == 0) {
    uint32_t size = info.fileSize;
}
~~~

Because Exists() converts all errors to false, use Stat() when you need to distinguish a missing file from another file-system error.

## Directory wrapper

UI::Directory enumerates the entries of one directory. It is non-copyable, and its destructor closes an open search handle.

~~~cpp
class Directory {
public:
    int Open(const char *path);
    int Next(DirectoryEntry *entry);
    int Close();
    bool IsOpen() const;
};
~~~

The SDK's find API uses a search handle. Always close it. The wrapper closes it automatically when the object is destroyed.

Each UI::DirectoryEntry contains:

- name[128] — the entry name converted to printable ASCII.
- isDirectory — true when the entry is a directory.
- size — the entry size in bytes; directories normally report zero.

### Listing a directory

~~~cpp
UI::Directory directory;
if (directory.Open("fls0/") == 0) {
    UI::DirectoryEntry entry;
    while (directory.Next(&entry) == 0) {
        // Inspect entry.name, entry.isDirectory, and entry.size.
    }
    directory.Close();
}
~~~

The current API uses a negative return value for both end-of-search and errors. It does not provide a separate status that distinguishes those cases.

## File filters

~~~cpp
static const UI::FileFilter filters[] = {
    UI::FileFilter(".txt", "Text files"),
    UI::FileFilter("dat", "Data files")
};
config.filters = filters;
config.filterCount = 2;
~~~

Extensions may include or omit the leading dot. Matching is case-insensitive and based on the filename suffix. Directories are always shown. A null filter array or non-positive filter count means every file matches. The description is stored but is not currently displayed.

The selector is not recursive. The user enters directories manually. The current implementation collects at most 64 entries in a directory and displays five entries per page.

## Errors

The wrappers return the negative error codes defined by the SDK in sdk/include/sdk/os/file.hpp. Common values include:

| Error | Meaning |
| --- | --- |
| EINVAL | Invalid argument or path |
| EACCES | Access denied; also used when a path contains a .. component |
| ENOVOLUME | The path does not name the supported fls0 volume |
| ENAMETOOLONG | The normalized path does not fit into the supplied buffer |
| ENOENT | The requested path does not exist |
| EBADF | The file or directory object is not open |
| ENOMEM | Memory allocation failed |

Check the complete list in the SDK header when handling specific failures. Preserve and inspect actual error codes when diagnosing failures.
