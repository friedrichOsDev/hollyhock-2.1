# Application Menus and Settings

The main entry point of the UI Library is UI::Application. It can show the built-in menu or hand the entire main screen to the application.

Include:

~~~cpp
#include <ui/ui.hpp>
~~~

## Application flow

1. Create a UI::Config structure.
2. Configure menu visibility, callbacks, settings, filters, and an optional MCS folder.
3. Create a UI::Application object.
4. Call app.Run().
5. The library initializes the calculator display and settings persistence, then runs the selected application flow.

## Config fields

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| title | const char * | "Application" | Main menu title |
| startLabel, filesLabel, settingsLabel, quitLabel | const char * | Default labels | Menu button text |
| settingsFolder | const char * | null | Optional MCS folder |
| showMainMenu | bool | true | Shows the built-in main menu |
| showStart | bool | true | Allows the Start entry |
| showFileSelector | bool | false | Enables built-in file selector |
| showSettings | bool | false | Enables built-in settings |
| showQuit | bool | true | Shows Quit |
| manageCalculatorLifecycle | bool | true | Library calls calcInit() and calcEnd() |
| onStart, onSettings | ActionCallback | null | Start and custom settings callbacks |
| onFileSelected | FileCallback | null | Called after file selection |
| filters | const FileFilter * | null | File extension filters |
| filterCount | int | 0 | Number of filters |
| settings | Setting * | null | Built-in setting array |
| settingCount | int | 0 | Number of settings |

Keep all strings and arrays referenced by Config alive for the entire call to Run().

## Built-in main menu

The built-in menu is enabled by default. It displays the configured Start, Files, Settings, and Quit entries. Start requires showStart and a non-null onStart callback.

## Custom main screen

Set showMainMenu to false to bypass the built-in menu. After initializing the LCD lifecycle and settings persistence, Application::Run() invokes onStart directly.

The onStart callback owns the application screen. It can use UI::Framebuffer and UI::Input to implement its own menu, game, or complete UI. It should normally keep running its own event loop until it calls Close(), OpenFileSelector(), or OpenSettings().

If onStart switches to the built-in file selector or settings screen, the library runs that screen. When it returns to the main application state, onStart is called again so the custom screen can be redrawn. If onStart returns without changing the state, the application exits.

~~~cpp
static void Start(UI::Application &app) {
    UI::Framebuffer screen;
    if (!screen.Begin()) {
        app.Close();
        return;
    }

    // Draw a custom screen and process UI::Input events here.
}

extern "C" void main() {
    UI::Config config;
    config.showMainMenu = false;
    config.onStart = Start;

    UI::Application app(config);
    app.Run();
}
~~~

See framebuffer-input.md for drawing and event examples.

## Callbacks

~~~cpp
typedef void (*ActionCallback)(Application &app);
typedef void (*FileCallback)(Application &app, const char *path);
~~~

Call ReturnToMenu() to return to the main application state, Close() to request exit, or OpenFileSelector()/OpenSettings() to switch to a built-in screen.

## File selection callback

~~~cpp
static void FileSelected(UI::Application &app, const char *path) {
    (void)app;
    UI::File file;
    int result = file.Open(path); // Always read-only.
    if (result < 0)
        return;
    // Read or inspect the file here.
}
~~~

The path is valid for the duration of the callback. A custom file browser can combine Framebuffer, Input, Directory, and FileSystem.

## Navigation methods

| Method | Behavior |
| --- | --- |
| Run() | Starts the application loop and returns after exit. |
| Close() | Requests application exit. |
| ReturnToMenu() | Changes the current state to the main application state. |
| OpenFileSelector() | Opens the built-in file selector when enabled. |
| OpenSettings() | Opens the built-in settings screen when enabled. |
| IsRunning() const | Returns false after exit is selected. |
| SettingsPersistenceResult() const | Returns 0 on success or latest MCS persistence error. |

## Built-in settings

| Type | Factory | Value pointer | Behavior |
| --- | --- | --- | --- |
| Boolean | Setting::Boolean() | bool * | Toggles true/false |
| Integer | Setting::Integer() | int * | Adds step and wraps |
| Enumeration | Setting::Enumeration() | int * | Advances and wraps |
| Action | Setting::Action() | none | Calls callback |

Setting descriptions are stored but not currently displayed.

### Persistent settings

Set config.settingsFolder to a unique 1-8 character MCS folder name. The library loads saved settings before the first application screen and saves after changes and normal exit. The serializer supports at most 32 settings and a payload smaller than 512 bytes.

Use UI::PersistentStore for application-owned persistent data beyond the built-in settings array.

## Built-in file selector

~~~cpp
static const UI::FileFilter filters[] = {
    UI::FileFilter(".txt", "Text files"),
    UI::FileFilter("dat", "Data files")
};
UI::Config config;
config.showFileSelector = true;
config.filters = filters;
config.filterCount = 2;
config.onFileSelected = FileSelected;
~~~

Extensions can include or omit the leading dot. Matching is case-insensitive. Directories are always shown. The selector starts at fls0, displays five entries per page, collects up to 64 entries in a directory, and sorts directories before files.

The file-system wrapper is read-only. It does not create, write, delete, rename, move, or create directories.

## Display lifecycle

By default, manageCalculatorLifecycle is true. When Run() starts, the library calls calcInit(). When the application exits, it calls calcEnd() to restore the display state managed by the SDK.

Framebuffer does not own this lifecycle. Create framebuffer objects at runtime after display initialization, normally inside callbacks. Do not independently back up or restore VRAM while Application owns the display lifecycle.

## Limitations

- The built-in menu is a simple button-based interface, not a widget system.
- The framebuffer draws directly into VRAM and does not use a second buffer.
- The built-in font supports printable ASCII 32-126.
- The file selector supports fls0 only, collects 64 entries per directory, and displays five per page.
- Non-ASCII file and directory names are shown as question marks.
