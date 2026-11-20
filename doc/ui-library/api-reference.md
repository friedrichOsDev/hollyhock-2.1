# API Reference

This page summarizes the public API declared in ui/include/ui/. For explanations and longer examples, see Application Menus and Settings and the File System API.

Include the complete public interface with:

~~~cpp
#include <ui/ui.hpp>
~~~

## Namespace

All UI Library classes, enumerations, structures, and functions are inside the UI namespace.

## Application

### UI::Application

~~~cpp
explicit Application(const Config &config);
~Application();

void Run();
void Close();
void ReturnToMenu();
void OpenFileSelector();
void OpenSettings();
bool IsRunning() const;
~~~

The class is non-copyable. Run() enters the synchronous menu loop. Close() requests exit. ReturnToMenu() changes the state to the main menu. OpenFileSelector() and OpenSettings() request the corresponding enabled screens. IsRunning() returns false after the exit state is selected.

### Callback types

~~~cpp
typedef void (*ActionCallback)(Application &app);
typedef void (*FileCallback)(Application &app, const char *path);
~~~

### UI::Config

| Member | Type |
| --- | --- |
| title | const char * |
| startLabel, filesLabel, settingsLabel, quitLabel | const char * |
| showStart, showFileSelector, showSettings, showQuit, manageCalculatorLifecycle | bool |
| onStart, onSettings | ActionCallback |
| onFileSelected | FileCallback |
| filters | const FileFilter * |
| filterCount | int |
| settings | Setting * |
| settingCount | int |

Defaults: title is Application; labels are Start, Files, Settings, and Quit; Start and Quit are enabled; File Selector and Settings are disabled; calculator lifecycle management is enabled; callbacks and pointer arrays are null and counts are zero.

### UI::FileFilter

~~~cpp
struct FileFilter {
    const char *extension;
    const char *description;
    FileFilter(const char *ext = 0, const char *desc = 0);
};
~~~

Filters are matched case-insensitively against the filename suffix. The description is currently not displayed by the selector.

## Settings

### UI::Setting

~~~cpp
enum SettingType {
    SettingBoolean,
    SettingInteger,
    SettingEnum,
    SettingAction
};

struct Setting {
    const char *name;
    const char *description;
    SettingType type;
    void *value;
    int minimum;
    int maximum;
    int step;
    const char *const *options;
    int optionCount;
    ActionCallback action;

    static Setting Boolean(const char *, bool *, const char *description = 0);
    static Setting Integer(const char *, int *, int minimum, int maximum,
                           int step = 1, const char *description = 0);
    static Setting Enumeration(const char *, int *, const char *const *,
                               int optionCount, const char *description = 0);
    static Setting Action(const char *, ActionCallback, const char *description = 0);
};
~~~

The settings menu reads and modifies the pointed-to values directly. The data must remain alive while the application runs. Descriptions are stored but are not currently drawn by the settings menu.

## Read-only file system

The UI Library intentionally does not expose file modification operations. Its filesystem wrapper cannot create, write, delete, rename, move, or create directories. This is a UI Library restriction only; the lower-level SDK remains unchanged.

### UI::File

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
    int Handle() const;
};
~~~

The class is non-copyable. Open(path) always uses the SDK's OPEN_READ flag and offers no way to pass write flags. Its destructor closes an open descriptor.

### UI::FileSystem

~~~cpp
class FileSystem {
public:
    static int NormalizePath(const char *input, char *output, int capacity);
    static int Stat(const char *path, struct stat *info);
    static bool Exists(const char *path);
};
~~~

Paths are normalized to the SDK's native fls0 path form. Only the fls0 volume is accepted. Parent traversal using .. is rejected.

### UI::DirectoryEntry

~~~cpp
struct DirectoryEntry {
    char name[128];
    bool isDirectory;
    uint32_t size;
};
~~~

Names are converted from the SDK's wide-character result to printable ASCII. Characters outside ASCII 32–126 are replaced with question marks.

### UI::Directory

~~~cpp
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

The class is non-copyable. Its destructor closes an open search handle. Next() returns zero for an entry and a negative code when the search ends or fails. The API does not distinguish end-of-search from an error.

## Related SDK APIs

The UI Library is layered on top of the Hollyhock SDK:

- sdk/os/gui.hpp — dialogs, buttons, labels, text boxes, and other native GUI wrappers.
- sdk/os/file.hpp — descriptors, metadata, directory search, and lower-level SDK file operations.
- sdk/calc/calc.hpp — calcInit() and calcEnd(), used by the default application lifecycle.
- sdk/os/debug.hpp — debug text output used by the demo application.

The SDK headers remain the source of truth for lower-level functions and return codes. The UI Library's read-only restriction does not remove or alter any SDK function.
