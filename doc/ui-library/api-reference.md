# API Reference

This page summarizes the public API declared in ui/include/ui/. For longer examples, see the application, framebuffer/input, and file-system guides.

~~~cpp
#include <ui/ui.hpp>
~~~

All public API declarations are inside the UI namespace.

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
    int SelectFile(const char *path);
    const char *CurrentDirectory() const;
    int SetCurrentDirectory(const char *path);
    Setting *GetSettings();
    int GetSettingCount() const;
    int AdjustSetting(int index, int direction = 1);
    bool FileMatchesFilter(const char *filename) const;
    bool IsRunning() const;
    int SettingsPersistenceResult() const;
};
~~~

Application is non-copyable. SettingsPersistenceResult() returns zero on success or the latest MCS persistence error.

~~~cpp
enum class MenuRenderer {
    Native,
    Framebuffer,
    Custom
};
~~~

Each menu has an independent renderer selection:

| Config member | Type | Default |
| --- | --- | --- |
| mainMenuRenderer | MenuRenderer | Native |
| fileSelectorRenderer | MenuRenderer | Native |
| settingsRenderer | MenuRenderer | Native |
| onCustomMainMenu | ActionCallback | null |
| onCustomFileSelector | ActionCallback | null |
| onCustomSettings | ActionCallback | null |

Custom callbacks own the drawing and event loop. Lifecycle and persistence remain managed by Application. Missing custom callbacks fall back to Native.

## Config

| Member | Type | Default |
| --- | --- | --- |
| showMainMenu | bool | true |
| showStart, showFileSelector, showSettings, showQuit | bool | true, false, false, true |
| manageCalculatorLifecycle | bool | true |
| settingsFolder | const char * | null |
| settings | Setting * | null |
| settingCount | int | 0 |
| filters | const FileFilter * | null |
| filterCount | int | 0 |

Set showMainMenu to false to call onStart directly and create a custom application screen. After the built-in file selector or settings returns, onStart is invoked again.

## Framebuffer

~~~cpp
typedef uint16_t Color;
Color RGB(uint8_t red, uint8_t green, uint8_t blue);

class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();
    bool Begin();
    void End();
    bool IsActive() const;
    int Width() const;
    int Height() const;
    bool Clear(Color color);
    bool SetPixel(int x, int y, Color color);
    bool FillRect(int x, int y, int width, int height, Color color);
    bool DrawRect(int x, int y, int width, int height, Color color);
    bool DrawLine(int x1, int y1, int x2, int y2, Color color);
    bool DrawChar(int x, int y, char character, Color color);
    bool DrawText(int x, int y, const char *text, Color color);
    bool Present();
};
~~~

Framebuffer is non-copyable. It attaches to LCD VRAM and does not allocate a second buffer. Drawing modifies VRAM; Present() refreshes the display. Colors are RGB565. UI::Colors provides standard colors. The built-in 5x7 font supports printable ASCII; DrawText() processes no more than 512 input bytes.

## Input

~~~cpp
enum EventType {
    EventNone, EventKey, EventTouch, EventSystem, EventUnknown
};
enum KeyAction {
    KeyActionUnknown, KeyPressed, KeyHeld, KeyReleased
};
enum TouchAction {
    TouchActionUnknown, TouchDown, TouchDrag, TouchUp, TouchActionBar
};

struct Event {
    EventType type;
    uint16_t rawType;
    uint16_t keyCode;
    KeyAction keyAction;
    int32_t x;
    int32_t y;
    TouchAction touchAction;
    SystemEvent systemEvent;
    uint32_t rawAction;
};

class Input {
public:
    static int Poll(Event *event);
};
~~~

Poll() returns 1 for an event, 0 when no event is available, -1 for a null pointer, or -2 for an SDK error. Unknown event types are preserved. Touch coordinates are passed through unchanged. The wrapper exposes single-touch events, key actions, and action-bar/system events.

KeyCode includes KeyUp, KeyDown, KeyLeft, KeyRight, KeyEXE, KeyPowerClear, and fx-CP400 keyboard keys. SystemEvent includes action-bar resize, swap, rotate, escape, and settings.

## Settings

~~~cpp
enum SettingType {
    SettingBoolean,
    SettingInteger,
    SettingEnum,
    SettingAction
};
~~~

Setting::Boolean, Setting::Integer, Setting::Enumeration, and Setting::Action create entries for the built-in settings screen. Persistent settings store Boolean, Integer, and Enumeration values, and ignore Action values. The serializer supports at most 32 entries and a payload smaller than 512 bytes.

## PersistentStore

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

PersistentStore is the lower-level MCS wrapper for application-owned persistent data. Folder names are limited to 1-8 ASCII alphanumeric characters. MCS errors can be positive constants; check for result == 0.

## Read-only file system

The UI Library exposes no file modification functions. The lower-level SDK remains unchanged.

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

Open(path) always uses OPEN_READ. File is non-copyable and closes its descriptor in the destructor.

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

Directory is non-copyable and closes an open search handle in its destructor.

## File filters

FileFilter matches file-name suffixes case-insensitively. Directories remain visible regardless of filter.

## SDK boundary

The UI Library uses SDK LCD, input, GUI, file, MCS, and calculator lifecycle functions. The UI file wrapper is read-only; SDK headers and lower-level functionality are unchanged.
