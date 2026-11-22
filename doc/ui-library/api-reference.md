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

| Member | Type | Default | Purpose |
| --- | --- | --- | --- |
| title | const char * | "Application" | Main menu title |
| startLabel, filesLabel, settingsLabel, quitLabel | const char * | Default labels | Menu labels |
| settingsFolder | const char * | null | Optional 1-8 character MCS folder |
| showMainMenu | bool | true | Show the main menu; false calls onStart directly |
| showStart | bool | true | Allow the Start entry |
| showFileSelector | bool | false | Enable file selection |
| showSettings | bool | false | Enable settings |
| showQuit | bool | true | Show Quit |
| manageCalculatorLifecycle | bool | true | Let Application call calcInit()/calcEnd() |
| mainMenuRenderer | MenuRenderer | Native | Main-menu renderer |
| fileSelectorRenderer | MenuRenderer | Native | File-selector renderer |
| settingsRenderer | MenuRenderer | Native | Settings renderer |
| onStart | ActionCallback | null | Start/custom dashboard callback |
| onSettings | ActionCallback | null | Callback used by the native/framebuffer settings path when no Setting array exists |
| onFileSelected | FileCallback | null | Called with a normalized selected path |
| onCustomMainMenu | ActionCallback | null | Custom main-menu event loop |
| onCustomFileSelector | ActionCallback | null | Custom file-selector event loop |
| onCustomSettings | ActionCallback | null | Custom settings event loop |
| filters, filterCount | FileFilter array, int | null, 0 | File extensions accepted by built-in selectors |
| settings, settingCount | Setting array, int | null, 0 | Application-owned setting definitions |

Keep all strings, arrays, and values referenced by Config alive until Run() returns. Application copies Config field by field, but the pointer members still refer to application-owned storage.

Set showMainMenu to false to call onStart directly. When onStart requests OpenFileSelector() or OpenSettings(), the selected screen is run and onStart is called again when the application returns to the main state.

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
    bool BlitRGB565(int x, int y, const uint16_t *source,
                    int sourceWidth, int sourceHeight, int scale = 1);
    bool DrawChar(int x, int y, char character, Color color);
    bool DrawText(int x, int y, const char *text, Color color);
    bool Present();
};
~~~

Framebuffer is non-copyable. It attaches to LCD VRAM and does not allocate a second buffer. Drawing modifies VRAM; Present() refreshes the display. BlitRGB565() copies a caller-owned image of at most 528×320 RGB565 pixels directly to VRAM, optionally replicating each source pixel by an integer scale from 1 to 64; destination writes are clipped to the LCD. It allocates no memory. Colors are RGB565. UI::Colors provides standard colors. The built-in 5x7 font supports printable ASCII 32-126; lowercase letters are drawn using uppercase glyphs. DrawText() handles newline, carriage return, and tab, and processes at most 512 input bytes.

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

Setting::Boolean, Setting::Integer, Setting::Enumeration, and Setting::Action create entries for the built-in or custom settings screen. Each factory has its original overload and an overload that accepts a stable ID before the display name:

~~~cpp
UI::Setting::Boolean("display.showGrid", "Show grid", &showGrid);
UI::Setting::Integer("editor.zoom", "Zoom", &zoom, 50, 200, 10);
UI::Setting::Enumeration(
    "editor.theme", "Theme", &theme, themeOptions, 3);
UI::Setting::Action("cache.clear", "Clear cache", ClearCache);
~~~

The original overloads remain source-compatible. Without an explicit ID, the setting name is used as the storage key. IDs/names used as keys must be unique within the settings array and contain 1-63 printable ASCII characters. Keys are case-sensitive.

Integer values use their configured range and step; enumeration values wrap through their options. Action entries invoke their callback on a positive adjustment. Persistent settings store Boolean, Integer, and Enumeration values, and serialize Action entries as markers without a stored value. The UIST2 serializer supports at most 32 entries and a payload smaller than 512 bytes. It matches values by key rather than array order, ignores removed keys, and leaves newly added settings at their defaults.

The previous UIST1 positional format remains readable and is upgraded to UIST2 after a successful load. Since UIST1 contains no IDs, its one-time migration must match entries by the current array order.

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

~~~cpp
struct FileFilter {
    const char *extension;
    const char *description;
    FileFilter(const char *ext = 0, const char *desc = 0);
};

bool FileFilterMatches(const char *filename,
                       const FileFilter *filters,
                       int filterCount);
~~~

FileFilter stores an extension and optional description. FileFilterMatches() tests filename suffixes case-insensitively. Extensions may include or omit the leading dot. A null filter array or a non-positive count allows all non-empty file names. Directories remain visible regardless of the configured file filters.

## SDK boundary

The UI Library uses SDK LCD, input, GUI, file, MCS, and calculator lifecycle functions. The UI file wrapper is read-only; SDK headers and lower-level functionality are unchanged.
