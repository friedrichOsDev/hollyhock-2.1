# UI Library Demo Walkthrough

The app_template/ application is the reference showcase for the Hollyhock UI Library. It deliberately mixes all three menu renderer modes in one application:

| Menu | Renderer | Implementation |
| --- | --- | --- |
| Main menu | **Framebuffer** | Drawn by the UI Library |
| File selector | **Native** | Existing Hollyhock SDK dialog |
| Settings | **Custom** | Drawn and controlled by the demo application |

The Start entry opens a second custom framebuffer dashboard. That dashboard contains small demonstrations for file-system access, normalized input events, and direct LCD drawing.

## Build and install

Run from the repository root:

~~~sh
./app_template/build.sh
~~~

The build script cleans and builds the SDK, UI library, and demo application. The app Makefile compiles both main.cpp and the additional source in demo/demo.cpp.

Copy app_template/app_template.hhk to the calculator's fls0 root and launch it through the Hollyhock launcher.

## Main menu

The main menu is configured in app_template/main.cpp:

~~~cpp
config.mainMenuRenderer = UI::MenuRenderer::Framebuffer;
config.fileSelectorRenderer = UI::MenuRenderer::Native;
config.settingsRenderer = UI::MenuRenderer::Custom;
~~~

All three renderers default to Native in the library. The demo explicitly selects this combination to demonstrate independent per-menu configuration.

The visible entries are:

- Open dashboard — invokes DemoStart().
- Browse files — opens the native SDK file selector.
- Demo settings — invokes DemoCustomSettings().
- Exit — closes the application.

Navigate with UP/DOWN and EXE or touch a row.

## Custom dashboard

DemoStart() owns a small event loop and draws its screen with UI::Framebuffer. It demonstrates:

| Dashboard page | API demonstrated |
| --- | --- |
| Storage diagnostics | FileSystem::NormalizePath(), FileSystem::Exists(), Directory |
| Input event monitor | Input::Poll(), key/touch/system event decoding |
| Framebuffer drawing | RGB565 colors, rectangles, lines, text, and Present() |
| Return to main menu | Returning from a custom Start callback without changing application state |
| Exit application | Application::Close() |

The callback detaches its framebuffer before opening a separate page and reattaches it after that page returns. When it returns in the main-menu state, the library resumes its framebuffer main menu.

## Native file selector and file preview

The demo filters the native selector to .txt, .md, .ini, and .cfg files. Directories remain visible regardless of the file filters.

When a file is selected, DemoFileSelected():

1. Receives the normalized path through onFileSelected.
2. Reads file metadata with FileSystem::Stat().
3. Opens the file with the read-only UI::File wrapper.
4. Reads a bounded preview according to the current settings.
5. Replaces non-printable bytes and applies the selected ASCII letter case.
6. Closes the file before displaying the result.

The result page is custom framebuffer UI. The demo does not write to or modify the selected file.

## Custom settings menu

DemoCustomSettings() owns both drawing and input handling. It obtains the shared settings array with:

~~~cpp
UI::Setting *settings = app.GetSettings();
int count = app.GetSettingCount();
~~~

Value changes go through app.AdjustSetting(index, direction), which reuses the library's setting rules and triggers MCS persistence when configured. The Reset action detaches the demo framebuffer before invoking the action callback, then returns to the main menu.

The five settings demonstrate Boolean, Integer, Enumeration, and Action entries:

- File preview
- Preview bytes
- Preview format (text or hexadecimal)
- Show file size
- Reset demo settings

The demo uses the MCS folder UIDEMO2. Settings are loaded at application startup and saved after changes and on normal exit.

## Source map

- app_template/main.cpp — application metadata, renderer configuration, filters, settings, and UI::Application setup.
- app_template/demo/demo.hpp — demo callback declarations and shared settings structure.
- app_template/demo/demo.cpp — dashboard, storage/input/drawing pages, file preview, and custom settings renderer.
- ui/include/ui/application.hpp and ui/src/application.cpp — application state machine, native/framebuffer/custom menu dispatch, and persistence.
- ui/include/ui/framebuffer.hpp and ui/src/framebuffer.cpp — bounded RGB565 drawing and bitmap font.
- ui/include/ui/input.hpp and ui/src/input.cpp — normalized key, touch, system, and unknown events.
- ui/include/ui/filesystem.hpp and ui/src/filesystem.cpp — read-only file and directory wrappers.
- ui/include/ui/storage.hpp and ui/src/storage.cpp — MCS-backed PersistentStore.
