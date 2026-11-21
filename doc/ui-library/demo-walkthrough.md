# Demo Walkthrough

The application in app_template/ demonstrates independent menu renderers. The library-provided framebuffer renderer draws the main menu and settings screen, while the original native SDK dialog handles file selection. The Start entry opens a separate custom framebuffer screen implemented by the demo itself.

## Build and install

~~~sh
./app_template/build.sh
~~~

Copy app_template/app_template.hhk to the calculator's fls0 root and start it with the Hollyhock launcher.

## Renderer configuration

The demo configures the menus independently:

~~~cpp
config.mainMenuRenderer = UI::MenuRenderer::Framebuffer;
config.fileSelectorRenderer = UI::MenuRenderer::Native;
config.settingsRenderer = UI::MenuRenderer::Framebuffer;
~~~

The default for all three is Native; the demo explicitly selects its combination to show that one application can mix both built-in presentation systems.

## Main menu and custom application screen

The library draws the main menu. It contains Start, Files, Settings, and Quit. Start invokes the demo's StartApplication callback, which draws its own framebuffer screen.

| Custom screen entry | What it demonstrates |
| --- | --- |
| Storage check | Path normalization, existence checks, and directory enumeration |
| Browse files | Transition to the built-in native file selector |
| Settings | Transition to the library's framebuffer settings screen |
| Exit | Closes the application |

Navigate using UP/DOWN and EXE or touch a row.

## Storage check

The storage page normalizes fls0/, checks whether the root exists, enumerates the root directory, closes the search handle, and draws the results using Framebuffer::DrawText(). Press a key or touch to return.

## Persistent settings

The demo uses the UIDEMO MCS folder. Its settings are Show preview, Preview bytes, Text case, and Reset settings. Changes are saved automatically and restored on the next launch.

## Browse and read files

The native selector starts at the root of fls0. Its filter accepts .txt files; matching is case-insensitive and directories remain visible.

The callback opens the selected path with UI::File::Open(), retrieves metadata and size, seeks to the beginning, reads the configured number of bytes, replaces non-printable characters, closes the descriptor, and draws the result using the framebuffer API. It never modifies the selected file.

## Custom renderer mode

Any menu can instead use MenuRenderer::Custom and its matching onCustomMainMenu, onCustomFileSelector, or onCustomSettings callback. These callbacks own drawing and input processing. Application retains calculator lifecycle, persistence, and navigation. For a custom file selector, use CurrentDirectory(), SetCurrentDirectory(), FileMatchesFilter(), and SelectFile(). For a custom settings view, use GetSettings(), GetSettingCount(), and AdjustSetting().

## Source map

- app_template/main.cpp — renderer configuration, the demo's custom start screen, file callback, and settings.
- ui/include/ui/framebuffer.hpp and ui/src/framebuffer.cpp — bounded drawing and bitmap font.
- ui/include/ui/input.hpp and ui/src/input.cpp — normalized key, touch, and system events.
- ui/include/ui/application.hpp and ui/src/application.cpp — application state machine, menu renderers, callbacks, and persistence.
- ui/include/ui/filesystem.hpp and ui/src/filesystem.cpp — read-only file and directory wrappers.
- ui/include/ui/storage.hpp and ui/src/storage.cpp — MCS persistence wrapper.
