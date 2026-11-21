# Demo Walkthrough

The application in app_template/ demonstrates the framebuffer and input APIs. It has its own touch/key-driven menu while retaining the built-in file selector and settings screen.

## Build and install

~~~sh
./app_template/build.sh
~~~

Copy app_template/app_template.hhk to the calculator's fls0 root and start it with the Hollyhock launcher.

## Custom menu

The demo sets Config::showMainMenu to false. The library initializes the display and settings persistence, then calls StartApplication directly.

| Entry | What it demonstrates |
| --- | --- |
| Storage check | Path normalization, existence checks, and directory enumeration |
| Browse files | The existing built-in file selector with a .txt filter |
| Settings | Built-in settings and MCS persistence |
| Exit | Closes the application |

Navigate using UP/DOWN and EXE or touch a row.

## Storage check

The storage page normalizes fls0/, checks whether the root exists, enumerates the root directory, closes the search handle, and draws the results using Framebuffer::DrawText(). Press a key or touch to return.

## Persistent settings

The demo uses the UIDEMO MCS folder. Its settings are Show preview, Preview bytes, Text case, and Reset settings. Changes are saved automatically and restored on the next launch.

## Browse and read files

The built-in selector starts at the root of fls0. Its filter accepts .txt files; matching is case-insensitive and directories remain visible.

The callback opens the selected path with UI::File::Open(), retrieves metadata and size, seeks to the beginning, reads the configured number of bytes, replaces non-printable characters, closes the descriptor, and draws the result using the framebuffer API. It never modifies the selected file.

## Input

The demo uses UI::Input::Poll() to receive key and touch events. The library translates SDK events into UI::Event structures. The demo interface does not use Debug_Printf() or debug text mode.

## Source map

- app_template/main.cpp — custom menu, drawing, input handling, file callback, settings, and app configuration.
- ui/include/ui/framebuffer.hpp and ui/src/framebuffer.cpp — bounded drawing and bitmap font.
- ui/include/ui/input.hpp and ui/src/input.cpp — normalized key, touch, and system events.
- ui/include/ui/application.hpp and ui/src/application.cpp — application state machine and persistence.
- ui/include/ui/filesystem.hpp and ui/src/filesystem.cpp — read-only file and directory wrappers.
- ui/include/ui/storage.hpp and ui/src/storage.cpp — MCS persistence wrapper.
