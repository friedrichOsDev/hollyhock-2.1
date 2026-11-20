# File System API

The UI Library provides small C++ wrappers around the file-system functions in the Hollyhock SDK. They are declared in:

~~~cpp
#include <ui/filesystem.hpp>
~~~

The header is also included by ui/ui.hpp.

The wrappers use the SDK's file system. They do not provide a separate file system or storage layer.

## Storage volume and path format

The current wrapper supports the calculator's fls0 volume only. It accepts forward slashes and backslashes and converts the path to the native form used by the SDK.

The following inputs refer to the same file:

| Input | Normalized native path |
| --- | --- |
| fls0/folder/file.txt | \fls0\folder\file.txt |
| /fls0/file.txt | \fls0\file.txt |
| \fls0\file.txt | \fls0\file.txt |

In C++ source code, escape each backslash in a string literal:

~~~cpp
"\\fls0\\file.txt"
~~~

A path without a leading slash is also valid:

~~~cpp
"fls0/file.txt"
~~~

### Path validation

FileSystem::NormalizePath() performs these checks:

- The input must identify the fls0 volume. Other volume names are rejected.
- Repeated separators are collapsed.
- A path component equal to . is skipped.
- A path component equal to .. is rejected to prevent parent-directory traversal.
- The output is written using backslashes and begins with \fls0\.
- The output buffer must be large enough. The file and directory wrappers use a 256-byte path buffer.

Use a buffer that includes room for the terminating null byte when calling NormalizePath() directly.

Example:

~~~cpp
char nativePath[256];
int result = UI::FileSystem::NormalizePath(
    "fls0/folder/file.txt",
    nativePath,
    sizeof(nativePath)
);

if (result == 0) {
    // nativePath now contains the normalized SDK path.
} else {
    // result is a negative SDK error code.
}
~~~

## File wrapper

UI::File manages one SDK file descriptor. It is non-copyable. Its destructor calls Close(), so an open descriptor is closed when the object leaves scope.

### Open modes

The following convenience values are provided:

| Mode | Meaning |
| --- | --- |
| UI::FileRead | Open for reading |
| UI::FileWrite | Open for writing |
| UI::FileReadWrite | Open for reading and writing |
| UI::FileCreate | Open for writing and create the file if it does not exist |
| UI::FileAppend | Open for writing with the file position initially at the end |

These values map directly to the SDK's OPEN_* flags. Combine the flags only when the SDK supports the requested combination.

### Methods

| Method | Description |
| --- | --- |
| Open(path, flags = OPEN_READ) | Opens a file. Returns the native file descriptor on success or a negative SDK error code. Calling it closes any file already held by the object first. |
| Close() | Closes the descriptor. Returns 0 if already closed or if the SDK close operation succeeds. |
| Read(buffer, count) | Reads up to count bytes. Returns the number of bytes read or a negative error code. |
| Write(buffer, count) | Writes up to count bytes according to the SDK result. Returns the number of bytes written or a negative error code. |
| Seek(offset, whence = SEEK_SET) | Changes the file position. Returns the new position or a negative error code. |
| GetSize(size) | Reads file metadata and stores the file size in bytes. Returns an SDK result code. |
| IsOpen() | Returns whether the object currently holds a valid descriptor. |
| Handle() | Returns the native descriptor, or -1 when closed. |

A read may return fewer bytes than requested. A return value of zero means that no more bytes were read (normally end of file). Always check negative return values before using a result as a byte count.

### Reading a file

~~~cpp
#include <ui/filesystem.hpp>

static void ReadTextFile(const char *path) {
    UI::File file;

    int descriptor = file.Open(path, UI::FileRead);
    if (descriptor < 0) {
        // Handle the SDK error.
        return;
    }

    uint32_t fileSize = 0;
    if (file.GetSize(&fileSize) < 0) {
        // Handle the metadata error.
        return;
    }

    char buffer[128];
    int bytesToRead = fileSize < sizeof(buffer) - 1
        ? (int)fileSize
        : (int)sizeof(buffer) - 1;

    int bytesRead = file.Read(buffer, bytesToRead);
    if (bytesRead < 0) {
        // Handle the read error.
        return;
    }

    buffer[bytesRead] = '\0';
    // Use the bytes in buffer. This example reads only the first part
    // of the file when the file is larger than the buffer.
}
~~~

The destructor closes the file when the function returns, including on early returns. You can also call Close() explicitly when you want to release the descriptor earlier.

This example assumes that the file contains text. For binary files, treat the buffer as bytes and do not use string functions on it.

### Writing a file

~~~cpp
#include <ui/filesystem.hpp>

static int WriteTextFile(const char *path) {
    static const char contents[] = "Hello from Hollyhock.\\r\\n";

    UI::File file;
    int descriptor = file.Open(path, UI::FileCreate);
    if (descriptor < 0) {
        return descriptor;
    }

    int written = file.Write(contents, sizeof(contents) - 1);
    if (written < 0) {
        return written;
    }

    if (written != (int)(sizeof(contents) - 1)) {
        return -1; // The write was shorter than requested.
    }

    return file.Close();
}
~~~

The wrapper exposes the SDK's create flag but does not add a separate truncate operation. Do not assume that opening an existing file with FileCreate clears any bytes after the new data. For a robust replacement operation, use an approach supported by the SDK and verify its behavior on the target calculator.

## FileSystem helper methods

UI::FileSystem contains three static methods:

| Method | Description |
| --- | --- |
| NormalizePath(input, output, capacity) | Validates and normalizes a path. Returns 0 on success or a negative SDK error code. |
| Stat(path, info) | Retrieves file metadata into a struct stat. Returns the SDK result code. |
| Exists(path) | Returns true when Stat() succeeds. It returns false for any error, not only when a file is missing. |

Example:

~~~cpp
struct stat info;
if (UI::FileSystem::Stat("fls0/readme.txt", &info) == 0) {
    uint32_t size = info.fileSize;
}

if (UI::FileSystem::Exists("fls0/readme.txt")) {
    // The SDK successfully found the path.
}
~~~

Because Exists() converts all errors to false, use Stat() when you need to distinguish a missing file from another file-system error.

## Directory wrapper

UI::Directory enumerates the entries of one directory. It is non-copyable, and its destructor closes an open search handle.

### Methods

| Method | Description |
| --- | --- |
| Open(path) | Starts a directory search. Returns 0 on success or a negative SDK error code. |
| Next(entry) | Writes the next entry and returns 0. A negative result means the search ended or an error occurred. |
| Close() | Closes the search handle. Returns 0 if already closed or if closing succeeds. |
| IsOpen() | Returns whether the directory search handle is open. |

The SDK's find API uses a search handle. Always close it. The wrapper closes it automatically when the object is destroyed, but calling Close() explicitly is useful when the search finishes before the scope ends.

Each UI::DirectoryEntry contains:

- name[128] — the entry name converted to printable ASCII.
- isDirectory — true when the entry is a directory.
- size — the entry size in bytes; directories normally report zero.

### Listing a directory

~~~cpp
#include <ui/filesystem.hpp>

static void ListDirectory(const char *path) {
    UI::Directory directory;
    if (directory.Open(path) < 0) {
        return;
    }

    UI::DirectoryEntry entry;
    while (directory.Next(&entry) == 0) {
        if (entry.isDirectory) {
            // Handle a directory.
        } else {
            // Handle a file and entry.size.
        }
    }

    directory.Close();
}
~~~

The current API uses a negative return value for both end-of-search and errors. It does not provide a separate status that distinguishes those cases.

## File filters

UI::FileFilter configures the built-in application file selector:

~~~cpp
static const UI::FileFilter filters[] = {
    UI::FileFilter(".txt", "Text files"),
    UI::FileFilter("dat", "Data files")
};

config.filters = filters;
config.filterCount = 2;
~~~

Rules:

- The extension may include or omit its leading dot.
- Matching is case-insensitive.
- Matching is based on the filename suffix.
- Directories are always shown.
- A null filter array or a non-positive filter count means that every file matches.
- Empty extension strings are ignored.
- The description is stored but is not currently shown by the selector.

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

The SDK defines additional error values. Check the complete list in sdk/include/sdk/os/file.hpp when handling specific failures.

Do not treat every negative result as a Boolean false/true value. Preserve and inspect the actual error code when you need to diagnose a failure.
