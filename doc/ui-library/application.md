# Application Menus and Settings

The main entry point of the UI Library is UI::Application. It builds a menu from a UI::Config structure and runs a synchronous menu loop.

Include the API with:

~~~cpp
#include <ui/ui.hpp>
~~~

## Application flow

A typical application follows this sequence:

1. Create a UI::Config structure.
2. Set the title, menu labels, callbacks, filters, and settings.
3. Create a UI::Application object with the configuration.
4. Call app.Run().
5. The library displays the main menu and handles navigation until the app is closed.

The library currently provides four optional main-menu entries:

| Entry | Config fields | When it is shown |
| --- | --- | --- |
| Start | showStart, startLabel, onStart | showStart is true and onStart is not null |
| Files | showFileSelector, filesLabel | showFileSelector is true |
| Settings | showSettings, settingsLabel | showSettings is true and settings or an onSettings callback are available |
| Quit | showQuit, quitLabel | showQuit is true |

The default configuration enables Start and Quit. The Start item still requires an onStart callback before it is displayed. File Selector and Settings are disabled by default.

If no menu entry is available, the current implementation adds a Quit button as a fallback.

## Config fields

The public UI::Config structure contains:

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| title | const char * | "Application" | Main menu title |
| startLabel | const char * | "Start" | Start button text |
| filesLabel | const char * | "Files" | File selector button text |
| settingsLabel | const char * | "Settings" | Settings button text |
| quitLabel | const char * | "Quit" | Quit button text |
| showStart | bool | true | Allows the Start entry |
| showFileSelector | bool | false | Enables the built-in file selector |
| showSettings | bool | false | Enables the built-in settings menu |
| showQuit | bool | true | Shows the Quit entry |
| manageCalculatorLifecycle | bool | true | Lets the library call calcInit() and calcEnd() |
| onStart | ActionCallback | null | Called when Start is selected |
| onSettings | ActionCallback | null | Fallback settings callback when no built-in settings array is configured |
| onFileSelected | FileCallback | null | Called after a file is selected |
| filters | const FileFilter * | null | File extension filters for the selector |
| filterCount | int | 0 | Number of entries in filters |
| settings | Setting * | null | Array of built-in settings |
| settingCount | int | 0 | Number of entries in settings |

The configuration is copied into the Application object, but pointer fields remain pointers to the original data. The strings, arrays, option lists, callbacks, and setting values must remain valid while the application is running.

## Callbacks

Callbacks are ordinary C++ functions with these types:

~~~cpp
typedef void (*ActionCallback)(Application &app);
typedef void (*FileCallback)(Application &app, const char *path);
~~~

The application reference allows a callback to change the menu state.

### Start callback

The Start callback is called after the main menu dialog has returned:

~~~cpp
static void Start(UI::Application &app) {
    // Perform application work here.
    app.ReturnToMenu();
}
~~~

Calling ReturnToMenu() is optional if the callback should simply finish and let the main menu loop continue. Call Close() when the application should exit.

### File selection callback

The file callback receives the selected path:

~~~cpp
static void FileSelected(UI::Application &app, const char *path) {
    (void)app;

    UI::File file;
    int result = file.Open(path, UI::FileRead);
    if (result < 0) {
        // Handle the SDK error.
        return;
    }

    // Read or inspect the file here.
}
~~~

The path is passed as a callback argument and is only guaranteed to be valid for the duration of the callback. Copy it into an application-owned buffer when it must be kept for later use.

If the callback does not change the application state, the file selector returns to the main menu after the callback finishes.

### Custom settings callback

Use onSettings when you want to provide your own settings behavior instead of the built-in settings array:

~~~cpp
static void OpenCustomSettings(UI::Application &app) {
    // Run custom settings code here.
    app.ReturnToMenu();
}

extern "C" void main() {
    UI::Config config;
    config.showSettings = true;
    config.onSettings = OpenCustomSettings;

    UI::Application app(config);
    app.Run();
}
~~~

The custom onSettings callback is used only when settings is null or settingCount is zero. When a built-in settings array is configured, the built-in settings screen takes priority and onSettings is not called.

## Navigation methods

The following methods are available on UI::Application:

| Method | Behavior |
| --- | --- |
| Run() | Starts the menu loop. Returns after the application reaches the exit state. |
| Close() | Requests application exit. It can be called from a callback. |
| ReturnToMenu() | Changes the current state to the main menu. |
| OpenFileSelector() | Opens the file selector when it is enabled in the configuration. |
| OpenSettings() | Opens the built-in settings screen when it is enabled. |
| IsRunning() const | Returns false after the application has entered its exit state. |

The navigation methods change the library's internal state. They do not create a new thread or run a second event loop.

## Built-in settings

A UI::Setting describes one entry in the settings array. The current implementation supports four types:

| Type | Factory | Value pointer | Behavior |
| --- | --- | --- | --- |
| Boolean | Setting::Boolean() | bool * | Toggles between true and false |
| Integer | Setting::Integer() | int * | Adds the configured step and wraps to the minimum after the maximum |
| Enumeration | Setting::Enumeration() | int * | Advances to the next option and wraps to option zero |
| Action | Setting::Action() | none | Calls the supplied callback |

### Example settings

~~~cpp
static bool soundEnabled = true;
static int volume = 4;
static int difficulty = 1;

static const char *const difficultyOptions[] = {
    "Easy",
    "Normal",
    "Hard"
};

static void ResetSettings(UI::Application &app) {
    soundEnabled = true;
    volume = 4;
    difficulty = 1;
    app.ReturnToMenu();
}

extern "C" void main() {
    UI::Config config;

    UI::Setting settings[] = {
        UI::Setting::Boolean(
            "Sound", &soundEnabled, "Enable sound"
        ),
        UI::Setting::Integer(
            "Volume", &volume, 0, 10, 2, "Volume level"
        ),
        UI::Setting::Enumeration(
            "Difficulty", &difficulty,
            difficultyOptions, 3, "Game difficulty"
        ),
        UI::Setting::Action(
            "Reset", ResetSettings, "Restore default values"
        )
    };

    config.showSettings = true;
    config.settings = settings;
    config.settingCount = 4;

    UI::Application app(config);
    app.Run();
}
~~~

Initialize value variables before entering the menu:

- Boolean values should point to valid bool variables.
- Integer values should start within the configured minimum and maximum.
- Enumeration values should be valid option indices from zero to optionCount - 1.
- The option array must contain at least optionCount valid strings.

The settings screen changes the values in memory. The library does not save them to MCS or to a file.

The description argument is stored in the setting structure, but the current settings renderer displays the setting name and its current value only. It does not currently show the description.

Integer settings use a positive step. A step of zero or a negative step is changed to 1 by the factory. When the next value is above the maximum, it wraps to the minimum.

### Action behavior

An Action setting calls its callback when selected. The settings screen remains active unless the callback changes the application state. For example, calling app.ReturnToMenu() leaves the settings screen and returns to the main menu.

## File selector configuration

Enable the file selector and provide a callback:

~~~cpp
static const UI::FileFilter filters[] = {
    UI::FileFilter(".txt", "Text files"),
    UI::FileFilter("dat", "Data files")
};

extern "C" void main() {
    UI::Config config;
    config.showFileSelector = true;
    config.filters = filters;
    config.filterCount = 2;
    config.onFileSelected = FileSelected;

    UI::Application app(config);
    app.Run();
}
~~~

Filter extensions can include or omit the leading dot. Matching is case-insensitive. Directories are always shown, even when they do not match a filter.

If filters is null or filterCount is zero or less, all files are shown. The current selector starts at \\fls0\\, displays five entries per page, and stores up to 64 entries from the current directory. It sorts directories before files and sorts names alphabetically without case sensitivity.

The description field of FileFilter is currently metadata; the selector matches using the extension only and does not display the description.

## Display lifecycle

By default, manageCalculatorLifecycle is true. When Run() starts, the library calls the SDK's calcInit(). When the application exits, it calls calcEnd(). In this SDK, those helpers initialize the display information, back up VRAM, and restore the previous VRAM contents when the app exits.

Keep the default unless your application already manages this lifecycle itself.

Set manageCalculatorLifecycle = false only when your application is deliberately responsible for calling the appropriate display setup and restoration functions. Do not let two parts of the application independently back up and restore the same display state.

The library's dialog implementation relies on the reverse-engineered GUI behavior exposed by the SDK. The SDK does not expose a documented, general-purpose custom-dialog dismissal API. The current menu implementation uses registered button events to leave its dialogs. Test GUI behavior on the target calculator and firmware.

## Practical limitations

- The built-in menu is a simple button-based interface; it is not a general widget layout system.
- Settings are in-memory only.
- The file selector only uses the fls0 volume.
- A maximum of 64 entries is collected per directory and five are shown per page.
- The current UI file selector converts non-ASCII names to question marks.
- The current file filter compares filename suffixes; it does not support wildcard patterns or MIME types.
- The implementation is designed for this repository's fx-CP400/Hollyhock SDK combination, not as a portable desktop GUI library.
