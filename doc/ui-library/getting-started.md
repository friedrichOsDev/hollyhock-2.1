# Getting Started

This page shows how to compile a Hollyhock application that uses the UI Library.

## 1. Requirements

You need:

- This repository, including sdk/ and ui/.
- GNU Make and Bash.
- The SH-4 cross-toolchain used by the SDK: sh4-elf-gcc, sh4-elf-g++, sh4-elf-as, sh4-elf-ld, sh4-elf-objcopy, and sh4-elf-readelf.
- A compatible fx-CP400 with the Hollyhock launcher installed when testing on the calculator.

A normal host compiler such as g++ cannot build the calculator application.

## 2. Build the SDK and UI library

Run these commands from the repository root:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

The first command creates sdk/sdk.o. The second compiles every source file in ui/src/, including the MCS storage wrapper, and creates ui/ui.o.

Build the SDK before the UI library because the UI library needs the SDK headers and object file.

## 3. Link the library into your application

Include the public UI header:

~~~cpp
#include <ui/ui.hpp>
~~~

Compile with both include directories, sdk/include and ui/include, and link both sdk/sdk.o and ui/ui.o. The app_template/Makefile demonstrates the required configuration.

The SDK and UI library are linked into the application; they are not loaded dynamically.

## 4. Minimal application

~~~cpp
#include <appdef.hpp>
#include <ui/ui.hpp>

APP_NAME("My UI Application")
APP_DESCRIPTION("A small application using the Hollyhock UI Library")
APP_AUTHOR("Your Name")
APP_VERSION("1.0")

static bool soundEnabled = true;

static void StartApplication(UI::Application &app) {
    (void)app;
}

extern "C" void main() {
    UI::Config config;
    config.title = "My Application";
    config.showStart = true;
    config.onStart = StartApplication;
    config.showSettings = true;

    // Optional: give this program its own unique MCS folder.
    config.settingsFolder = "MYAPP";

    UI::Setting settings[] = {
        UI::Setting::Boolean("Sound", &soundEnabled)
    };
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);

    UI::Application app(config);
    app.Run();
}
~~~

The APP_* macros place metadata into sections used by the Hollyhock application format. UI::Config controls the menu. The Start entry is displayed only when both showStart is true and onStart is not null. Quit is enabled by default.

The settingsFolder field is optional. When set to a unique 1-8 character alphanumeric name, the library creates that MCS folder, loads built-in settings before the menu appears, and saves after changes and on normal exit. Folder names are limited to eight characters by the SDK.

## 5. Build an application

From the repository root:

~~~sh
./app_template/build.sh
~~~

The script cleans old artifacts, builds the SDK, builds the UI library, and builds the demo application.

You can also build directly after building both libraries:

~~~sh
make -C app_template SDK_DIR=../sdk UI_DIR=../ui all
~~~

The all target creates app_template.hhk and app_template.bin. Use make -C app_template ... clean to remove application artifacts. The SDK and UI library have their own clean targets.

## 6. Run it on the calculator

Copy app_template/app_template.hhk to the root of the calculator's fls0 storage. Open the Hollyhock launcher and run the application.

The demo shows the read-only file browser, file metadata and read preview, settings, and persistent settings in the UIDEMO MCS folder. Change a setting, exit, and launch the demo again to verify that the value is restored.

A separate read-only API test application is available in ui_test/. Build it with:

~~~sh
./ui_test/build.sh
~~~

It tests path validation, directory enumeration, file filtering, metadata, opening, seeking, reading, and handle cleanup. Its settings use a separate UITEST MCS folder.

## 7. Keep referenced data alive

UI::Application stores a copy of UI::Config, but pointer fields remain pointers to application-owned data. Keep the following alive until Run() returns:

- Strings used for the title, menu labels, and settingsFolder.
- The FileFilter array and its strings.
- The Setting array and enumeration option arrays.
- Variables referenced by Boolean, Integer, and Enumeration settings.

Local variables in main() are suitable because Run() is synchronous. Do not create settings or filter arrays in a helper function that returns before Run() finishes.

## 8. Freestanding C++ notes

The application toolchain is configured for a freestanding SH-4 environment. The app template disables exceptions and RTTI and uses short wchar_t values to match the SDK file-system interface.

Do not add global objects that require dynamic C++ constructors. The app Makefile checks object files for a .ctors section and rejects it. Create UI::Application, PersistentStore, and other non-trivial objects at runtime inside main() or another called function.
