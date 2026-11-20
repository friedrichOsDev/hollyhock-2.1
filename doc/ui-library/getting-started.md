# Getting Started

This page shows how to compile a Hollyhock application that uses the UI Library.

## 1. Requirements

You need:

- This repository, including the sdk/ and ui/ directories.
- GNU Make and Bash.
- The SH-4 cross-toolchain used by the SDK: sh4-elf-gcc, sh4-elf-g++, sh4-elf-as, sh4-elf-ld, sh4-elf-objcopy, and sh4-elf-readelf.
- A compatible fx-CP400 with the Hollyhock launcher installed when you want to test the resulting application on the calculator.

The compiler must support the SH-4 target. A normal host compiler such as g++ cannot build the calculator application.

## 2. Build the SDK and UI library

Run these commands from the repository root:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

The first command creates the relocatable SDK object sdk/sdk.o. The second compiles the UI library and creates ui/ui.o.

The UI library needs the SDK headers and SDK object. Build the SDK first.

## 3. Link the library into your application

Include the public UI header:

~~~cpp
#include <ui/ui.hpp>
~~~

The application must be compiled with both include directories:

- sdk/include
- ui/include

It must also be linked with both object files:

- sdk/sdk.o
- ui/ui.o

The repository's app_template/Makefile already does this. It defines SDK_DIR and UI_DIR, adds the include directories, and links both objects. Use it as a starting point for your own application.

The SDK and UI library are not loaded dynamically. Their object code becomes part of the final application.

## 4. Minimal application

The following example creates a main menu with a Start action and a Quit entry:

~~~cpp
#include <appdef.hpp>
#include <ui/ui.hpp>

APP_NAME("My UI Application")
APP_DESCRIPTION("A small application using the Hollyhock UI Library")
APP_AUTHOR("Your Name")
APP_VERSION("1.0")

static void StartApplication(UI::Application &app) {
    // Replace this with the work your application should perform.
    // Call app.Close() to exit, or app.ReturnToMenu() to show the menu again.
    (void)app;
}

extern "C" void main() {
    UI::Config config;
    config.title = "My Application";
    config.startLabel = "Start";
    config.showStart = true;
    config.onStart = StartApplication;
    config.showFileSelector = false;
    config.showSettings = false;
    config.showQuit = true;

    UI::Application app(config);
    app.Run();
}
~~~

The APP_* macros place application metadata into sections used by the Hollyhock application format. They come from appdef.hpp, which is part of the SDK.

The UI::Config object controls which menu entries are shown. A Start entry is displayed only when both showStart is true and onStart is not null. The Quit entry is enabled by default.

Application::Run() owns the menu loop. It returns after the user selects Quit or a callback calls app.Close().

## 5. Build an application

From the repository root, the demo can be built with:

~~~sh
./app_template/build.sh
~~~

The script performs the following steps:

1. Removes old application build artifacts.
2. Builds the SDK.
3. Builds the UI library.
4. Builds and links the application.

You can also build the application directly after building the two libraries:

~~~sh
make -C app_template SDK_DIR=../sdk UI_DIR=../ui all
~~~

Use make -C app_template ... clean to remove the application build artifacts. Use make -C ui SDK_DIR=../sdk clean and make -C sdk clean to clean the corresponding library builds.

The application Makefile supports the all, hhk, and bin targets. The all target creates both output formats:

- app_template.hhk — the Hollyhock application file used by the launcher.
- app_template.bin — the raw binary output.

## 6. Run it on the calculator

Copy app_template/app_template.hhk to the root of the calculator's fls0 storage. Open the Hollyhock launcher and run the application.

The repository's [add-on software guide](../user/using.md) describes the launcher workflow.

The demo includes a sample-file action, a filtered file selector, and settings. Selecting a file opens a short debug information screen so that the file wrapper can be tested without adding a second custom-dialog system to the UI library.

## 7. Keep referenced data alive

UI::Application stores a copy of UI::Config, but several fields in that structure are pointers. The application does not make deep copies of the pointed-to data.

Keep these objects valid until Run() returns:

- The strings used for the title and menu labels.
- The FileFilter array and its strings.
- The Setting array.
- The enumeration option arrays.
- The variables referenced by Boolean, Integer, and Enumeration settings.

Local variables in main() are suitable because Run() is synchronous. Do not create the settings or filter arrays inside a helper function that returns before Run() finishes.

## 8. Freestanding C++ notes

The application toolchain is configured for a freestanding SH-4 environment. The app template disables exceptions and RTTI and uses short wchar_t values to match the SDK's file-system interface.

Do not add global objects that require dynamic C++ constructors. The app template checks compiled object files for a .ctors section and rejects them. Create UI::Application and other non-trivial objects inside main() or another function that is called at runtime.
