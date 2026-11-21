# API Reference

This page summarizes the public API declared in ui/include/ui/. For longer examples, see Application Menus and Settings and the File System API.

~~~cpp
#include <ui/ui.hpp>
~~~

All public classes, enumerations, structures, and functions are inside the UI namespace.

## Application

~~~cpp
class Application {
public:
    explicit Application(const Config &config);
    ~Application();
    void Run();
    void Close();
    void ReturnToMenu();
    void OpenFileSelector();
    void OpenSettings();
    bool IsRunning() const;
    int SettingsPersistenceResult() const;
};
~~~

Application is non-copyable. SettingsPersistenceResult() returns zero on success or the latest MCS persistence error.

## Config

Important fields:

| Member | Type | Default |
| --- | --- | --- |
| settingsFolder | const char * | null |
| settings | Setting * | null |
| settingCount | int | 0 |
| filters | const FileFilter * | null |
| filterCount | int | 0 |
| showStart, showFileSelector, showSettings, showQuit | bool | true, false, false, true |
| manageCalculatorLifecycle | bool | true |

The other fields configure menu labels and callbacks. See application.md for the full list.

settingsFolder enables automatic settings persistence in an MCS folder. Folder names must contain 1-8 ASCII letters or digits; they are normalized to uppercase. Use a unique stable name for each application.

## Settings

~~~cpp
enum SettingType {
    SettingBoolean,
    SettingInteger,
    SettingEnum,
    SettingAction
};
~~~

Setting::Boolean, Setting::Integer, Setting::Enumeration, and Setting::Action create entries for the built-in settings screen.

Automatic settings persistence stores Boolean, Integer, and Enumeration values and ignores Action values. It supports at most 32 entries and a serialized payload smaller than 512 bytes. Settings are loaded before the first menu is shown and saved after each selection and when Run() exits normally.

## PersistentStore

PersistentStore is the lower-level MCS (Calc Vars) wrapper for application-owned persistent data. It can be used for data beyond the built-in settings array.

~~~cpp
class PersistentStore {
public:
    PersistentStore();
    ~PersistentStore();
    int Open(const char *folder);
    void Close();
    bool IsOpen() const;
    const char *Folder() const;
    int Load(const char *name, uint8_t *type, void *buffer,
             uint32_t capacity, uint32_t *actualSize) const;
    int Save(const char *name, uint8_t type,
             const void *data, uint32_t size);
};
~~~

Open() creates the MCS folder when needed and returns zero only on success. Folder names are validated as 1-8 ASCII letters/digits because the SDK truncates longer names to eight characters. Variable names accepted by this wrapper must contain ASCII letters or digits. Load() copies the stored variable into caller-owned memory and reports its type and stored size. Save() copies the payload into a 4-byte-aligned buffer and rounds the stored size up to a multiple of four bytes before calling MCS_SetVariable(); padding bytes are zero. Therefore, actualSize may be larger than the original payload size. The caller is responsible for providing a sufficiently large buffer and interpreting the variable type. MCS functions can return positive error constants such as MCS_NO_FOLDER and MCS_NO_VARIABLE; wrapper argument and buffer errors use the SDK's negative error codes. Always test for result == 0, not result < 0.

Example:

~~~cpp
UI::PersistentStore store;
if (store.Open("MYAPP") == 0) {
    const char data[] = "example";
    store.Save("USERDATA", VARTYPE_STR, data, sizeof(data));
    char buffer[64];
    uint8_t type = 0;
    uint32_t size = 0;
    int result = store.Load("USERDATA", &type, buffer, sizeof(buffer), &size);
}
~~~

## Read-only file system

The UI Library exposes no file modification functions. The SDK remains unchanged and continues to provide lower-level write, create, delete, rename, and directory-creation functions to applications that deliberately use the SDK directly.

### File

~~~cpp
class File {
public:
    File();
    ~File();
    int Open(const char *path);
    int Close();
    int Read(void *buffer, int count);
    int Seek(int offset, int whence = SEEK_SET);
    int GetSize(uint32_t *size) const;
    bool IsOpen() const;
};
~~~

Open(path) always uses OPEN_READ. The class is non-copyable, exposes no raw descriptor, and closes its descriptor in the destructor.

### FileSystem

~~~cpp
class FileSystem {
public:
    static int NormalizePath(const char *input, char *output, int capacity);
    static int Stat(const char *path, struct stat *info);
    static bool Exists(const char *path);
};
~~~

Only fls0 is accepted and parent traversal using .. is rejected.

### Directory

~~~cpp
struct DirectoryEntry {
    char name[128];
    bool isDirectory;
    uint32_t size;
};

class Directory {
public:
    Directory();
    ~Directory();
    int Open(const char *path);
    int Next(DirectoryEntry *entry);
    int Close();
    bool IsOpen() const;
};
~~~

Directory is non-copyable and closes an open search handle in its destructor. Next() returns zero for an entry and a negative SDK code when the search ends or fails.

## File filters

FileFilter matches file-name suffixes case-insensitively. Directories remain visible regardless of filter. The filter description is metadata and is not currently displayed.

## SDK boundary

The UI Library uses sdk/os/gui.hpp, sdk/os/file.hpp, sdk/os/mcs.hpp, sdk/calc/calc.hpp, and sdk/os/debug.hpp. The UI file wrapper is read-only; SDK headers and lower-level functionality are unchanged.
