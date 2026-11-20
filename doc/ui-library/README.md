# Hollyhock UI Library

The Hollyhock UI Library is a small C++ helper library for applications built with the Hollyhock 2.1 SDK on the Casio fx-CP400. It provides a ready-to-use application menu, a file selector, a settings menu, and convenience wrappers around the calculator file system.

This guide explains how to build the library, use it from a Hollyhock application, and understand its current limitations. The examples target the SH-4 toolchain and the SDK included in this repository.

## Documentation

- [Demo walkthrough](demo-walkthrough.md) — a step-by-step tour of the example application.
- [Getting started](getting-started.md) — toolchain requirements, build steps, linking, and a minimal application.
- [Application menus and settings](application.md) — configuration, callbacks, navigation, settings, and display lifecycle.
- [File system API](filesystem.md) — normalized paths, files, directories, filters, and error handling.
- [API reference](api-reference.md) — a compact reference for the public classes, methods, and data types.

## What the library provides

- A main menu with optional Start, Files, Settings, and Quit entries.
- A file selector rooted at the calculator's fls0 storage volume.
- Optional extension filters for files. Directories remain visible.
- Settings for Boolean values, integer values, enumerations, and custom actions.
- Callbacks for starting an application and handling a selected file.
- A file wrapper that closes open descriptors automatically when its object is destroyed.
- A directory wrapper that closes its search handle automatically.
- A path normalizer that accepts either slash style and rejects paths outside fls0 and parent-directory traversal.

The menu and file selector are implemented with GUI classes from the Hollyhock SDK. The library is built on top of the SDK; it does not replace it.

## What the library does not provide

The current implementation is intentionally small. It does not provide a general-purpose widget system, a custom dialog builder, or persistent settings storage. Settings are stored in the variables supplied by the application and are lost when the application exits unless the application saves them separately.

The file-system wrapper supports opening, reading, writing, seeking, querying file size, checking existence, querying metadata, and enumerating directories. It does not provide wrapper methods for deleting, renaming, or creating directories.

## Target and compatibility

The code targets the fx-CP400 and the reverse-engineered Hollyhock 2.1 SDK in this repository. The SDK calls firmware functions and uses reverse-engineered GUI object layouts. Do not assume that the same binaries or GUI behavior are compatible with other calculator models or OS versions.

The UI library's file selector and file-system wrapper currently support the fls0 volume only. File and directory names are converted to printable ASCII for display; characters outside ASCII 32–126 are shown as question marks. Non-ASCII paths are not supported.

The SDK and UI library are freestanding. They are linked into the application rather than loaded as a separate runtime library.

## Build the demo

From the repository root, run:

~~~sh
./app_template/build.sh
~~~

The script builds the SDK, the UI library, and the demo application. It produces app_template/app_template.hhk and app_template/app_template.bin.

For manual deployment, copy the .hhk file to the root of the calculator's flash storage and launch it with the Hollyhock launcher. See the repository's [instructions for running add-on software](../user/using.md).

## Important lifetime rule

The application configuration stores pointers to strings, file-filter arrays, settings arrays, enumeration option arrays, and setting values. Keep all of these objects alive for the entire call to Application::Run().

The easiest approach is to create the configuration, filters, options, and settings as local variables in main(), then create the UI::Application object and call Run() before leaving main(). The demo application follows this pattern.
