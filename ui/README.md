# Hollyhock UI Library

A small C++ application-menu and file-system helper library for the fx-CP400, built on top of this repository's Hollyhock 2.1 SDK.

## Full documentation

Start here: [UI Library documentation](../doc/ui-library/README.md).

The guide covers:

- Build requirements and linking the SDK and UI library.
- Application menus, callbacks, and navigation.
- Boolean, integer, and enumeration settings that control the demo's preview, plus action settings to create a sample file and reset values.
- File filters and the built-in file selector.
- File, directory, path-normalization, and error-handling APIs.
- Current limitations and data-lifetime requirements.

## Quick build

From the repository root:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

This creates `sdk/sdk.o` and `ui/ui.o`. Link both objects into your application and add `sdk/include` and `ui/include` to the compiler include paths.

The repository's `app_template/` is a complete example that links both libraries. Build it from the repository root with:

~~~sh
./app_template/build.sh
~~~

## Demo application

The demo is intended to be a practical companion to the documentation. It demonstrates:

- A Start callback that uses path normalization and directory enumeration.
- A file selector filtered to `.txt` files.
- Reading the selected file and retrieving its size.
- Boolean, integer, and enumeration settings.
- Action settings that create a sample file and reset the values.

The demo uses the SDK debug text output for brief feedback; it is not an additional UI Library widget API.

## Important

The UI Library targets the fx-CP400 and this repository's reverse-engineered Hollyhock 2.1 SDK. Its GUI behavior and firmware calls are platform-specific. Read the limitations in the full documentation and test applications on the target calculator.
