# Application Menus and Settings

The main entry point of the UI Library is UI::Application. It builds a menu from a UI::Config structure and runs a synchronous menu loop.

Include the API with:

~~~cpp
#include <ui/ui.hpp>
~~~

## Application flow

1. Create a UI::Config structure.
2. Set the title, menu labels, callbacks, filters, settings, and optional settings folder.
3. Create a UI::Application object with the configuration.
4. Call app.Run().
5. The library displays the main menu and handles navigation until the app is closed.

The main menu can show Start, Files, Settings, and Quit. Start needs an onStart callback; Files and Settings are enabled by their corresponding flags.

## Config fields

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| title | const char * | "Application" | Main menu title |
| startLabel, filesLabel, settingsLabel, quitLabel | const char * | Default labels | Menu button text |
| showStart | bool | true | Allows the Start entry |
| showFileSelector | bool | false | Enables the built-in file selector |
| showSettings | bool | false | Enables the built-in settings menu |
| showQuit | bool | true | Shows the Quit entry |
| manageCalculatorLifecycle | bool | true | Lets the library call calcInit() and calcEnd() |
| settingsFolder | const char * | null | Optional MCS folder used for this app's persistent settings |
| onStart, onSettings | ActionCallback | null | Start and custom settings callbacks |
| onFileSelected | FileCallback | null | Called after a file is selected |
| filters | const FileFilter * | null | File extension filters for the selector |
| filterCount | int | 0 | Number of entries in filters |
| settings | Setting * | null | Array of built-in settings |
| settingCount | int | 0 | Number of entries in settings |

The configuration is copied into the Application object, but pointer fields remain pointers to the original data. Keep strings, arrays, option lists, callbacks, and setting values alive while the application is running.

## Callbacks

~~~cpp
typedef void (*ActionCallback)(Application &app);
typedef void (*FileCallback)(Application &app, const char *path);
~~~

The application reference allows a callback to change menu state. Call ReturnToMenu() to return to the main menu or Close() to request exit.

### File selection callback

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

The path is valid for the duration of the callback. Copy it into an application-owned buffer when it must be kept for later use.

## Navigation methods

| Method | Behavior |
| --- | --- |
| Run() | Starts the menu loop and returns after exit. |
| Close() | Requests application exit. |
| ReturnToMenu() | Changes the current state to the main menu. |
| OpenFileSelector() | Opens the file selector when enabled. |
| OpenSettings() | Opens the built-in settings screen when enabled. |
| IsRunning() const | Returns false after exit is selected. |
| SettingsPersistenceResult() const | Returns 0 on success or the latest MCS persistence error. |

## Built-in settings

The current implementation supports four setting types:

| Type | Factory | Value pointer | Behavior |
| --- | --- | --- | --- |
| Boolean | Setting::Boolean() | bool * | Toggles true/false |
| Integer | Setting::Integer() | int * | Adds the step and wraps to the minimum after the maximum |
| Enumeration | Setting::Enumeration() | int * | Advances to the next option and wraps to zero |
| Action | Setting::Action() | none | Calls the supplied callback |

Boolean values need valid bool storage. Integer values should start within configured bounds. Enumeration values must be valid indices from zero to optionCount - 1. The option array must contain enough strings. Setting descriptions are stored but not currently displayed.

### Persistent settings in Calc Vars

Persistence is optional. Set config.settingsFolder to a unique MCS folder name for the application. The SDK's MCS folder API truncates names longer than eight characters, so the UI Library rejects names outside 1-8 ASCII letters or digits instead of silently allowing collisions. Names are normalized to uppercase.

For example, the demo uses UIDEMO. Give every application a distinct, stable folder name. The folder is created automatically on first run.

When settingsFolder is set, UI::Application loads saved settings before showing the first menu. It saves settings after a settings entry is selected and once again when Run() exits normally. When the SETTINGS variable does not exist yet, the library now immediately writes the application's default settings to MCS during startup. This creates a visible SETTINGS variable on the first launch rather than waiting for the first settings interaction or application exit.

The automatic settings serializer supports Boolean, Integer, and Enumeration settings plus Action entries (actions have no stored value). It currently supports up to 32 entries and a serialized payload smaller than 512 bytes. Integer definitions must have a valid minimum/maximum range and a positive step; enumeration definitions need a non-empty option array. Initial and changed values must remain within their configured bounds before they can be persisted. The saved data is versioned; incompatible or invalid data is rejected rather than copied into application variables.

Check the result with:

~~~cpp
int result = app.SettingsPersistenceResult();
~~~

A result of zero means the latest persistence operation succeeded. A nonzero result is an SDK error or a format/size error. Persistence errors do not terminate the UI menu.

For application-owned data beyond the built-in settings array, use UI::PersistentStore directly. It stores named MCS variables and does not use the file-system wrapper.

## Custom settings callback

Use onSettings when you want custom settings behavior instead of a built-in settings array. It is called only when settings is null or settingCount is zero.

## File selector configuration

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

Extensions can include or omit the leading dot. Matching is case-insensitive. Directories are always shown. With no filters, all files are shown. The selector starts at fls0, displays five entries per page, collects up to 64 entries in a directory, and sorts directories before files.

The file-system wrapper is read-only. It does not create, write, delete, rename, move, or create directories. This restriction does not change the underlying SDK.

## Display lifecycle

By default, manageCalculatorLifecycle is true. When Run() starts, the library calls calcInit(). When the application exits, it calls calcEnd() to restore the display state managed by the SDK.

Keep the default unless your application already manages this lifecycle. Do not let two parts of the application independently back up and restore the same display state.

The dialog implementation relies on reverse-engineered GUI behavior exposed by the SDK. Test GUI behavior on the target calculator and firmware.

## Practical limitations

- The built-in menu is a simple button-based interface, not a general widget system.
- MCS settings persistence is opt-in and supports at most 32 entries in a payload smaller than 512 bytes.
- The file selector supports fls0 only, collects 64 entries per directory, and displays five per page.
- Non-ASCII file and directory names are shown as question marks.
- The implementation targets this repository's fx-CP400/Hollyhock SDK combination.
