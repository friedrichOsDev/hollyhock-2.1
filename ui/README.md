# Hollyhock UI Library

A small C++ application-menu and read-only file-system helper library for the fx-CP400, built on top of this repository's Hollyhock 2.1 SDK.

## Full documentation

Start here: the UI Library documentation in ../doc/ui-library/README.md.

The guide covers:

- Build requirements and linking the SDK and UI library.
- Application menus, callbacks, and navigation.
- Boolean, integer, and enumeration settings, plus custom actions.
- File filters and the built-in file selector.
- Read-only file, directory, path-normalization, and error-handling APIs.
- Current limitations and data-lifetime requirements.

## Quick build

From the repository root:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

This creates sdk/sdk.o and ui/ui.o. Link both objects into your application and add sdk/include and ui/include to the compiler include paths.

The repository's app_template/ is a complete example that links both libraries. Build it from the repository root with:

~~~sh
./app_template/build.sh
~~~

## Demo application

The demo is a read-only companion to the documentation. It demonstrates:

- A Start callback that normalizes a path and enumerates the root directory.
- A file selector filtered to .txt files.
- Reading a selected file and retrieving its size.
- Boolean, integer, and enumeration settings.
- A reset action for the in-memory settings.

The demo does not create, write, delete, rename, or move files. It uses SDK debug text output for brief feedback; this is not an additional UI Library widget API.

## SDK boundary

The UI file-system wrapper is intentionally read-only. This does not remove or change the corresponding lower-level file-system functions from the Hollyhock SDK.
