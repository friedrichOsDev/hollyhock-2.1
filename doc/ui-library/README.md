# Hollyhock UI Library

The Hollyhock UI Library is a small C++ helper library for applications built with the Hollyhock 2.1 SDK on the Casio fx-CP400. It provides a ready-to-use application menu, a file selector, a settings menu, and read-only convenience wrappers around the calculator file system.

This guide explains how to build the library, use it from a Hollyhock application, and understand its current limitations. The examples target the SH-4 toolchain and the SDK included in this repository.

## Documentation

- Demo walkthrough — a step-by-step tour of the example application.
- Getting started — toolchain requirements, build steps, linking, and a minimal application.
- Application menus and settings — configuration, callbacks, navigation, settings, and display lifecycle.
- File system API — normalized paths, read-only file access, directories, filters, and error handling.
- API reference — a compact reference for public classes, methods, and data types.

## What the library provides

- A main menu with optional Start, Files, Settings, and Quit entries.
- A file selector rooted at the calculator's fls0 storage volume.
- Optional extension filters for files. Directories remain visible.
- Settings for Boolean values, integer values, enumerations, and custom actions.
- Callbacks for starting an application and handling a selected file.
- A read-only file wrapper for opening, reading, seeking, and querying file size.
- A directory wrapper that closes its search handle automatically.
- A path normalizer that accepts either slash style and rejects paths outside fls0 and parent-directory traversal.

The menu and file selector are implemented with GUI classes from the Hollyhock SDK. The library is built on top of the SDK; it does not replace it.

## Read-only file access

The UI Library intentionally exposes no file-writing or file-modification API. There are no UI-level write modes, Write(), create, delete, rename, move, or directory-creation operations. The built-in file browser only navigates, lists, inspects, and reads files.

This restriction does not modify the Hollyhock SDK. Its lower-level file functions remain available to applications that deliberately choose to use the SDK directly. Such calls are outside the UI Library's read-only wrapper.

## Limitations

The UI library does not provide a general-purpose widget system, a custom dialog builder, or persistent settings storage yet. Settings are stored in the variables supplied by the application and are lost when the application exits unless the application saves them separately.

The file-system wrapper supports the fls0 volume only. File and directory names are converted to printable ASCII for display; characters outside ASCII 32–126 are shown as question marks. Non-ASCII paths are not supported.

The SDK and UI library are freestanding. They are linked into the application rather than loaded as a separate runtime library.

## Build the demo

From the repository root, run:

~~~sh
./app_template/build.sh
~~~

The script builds the SDK, the UI library, and the demo application. It produces app_template/app_template.hhk and app_template/app_template.bin.

For manual deployment, copy the .hhk file to the root of the calculator's flash storage and launch it with the Hollyhock launcher. See the repository's instructions for running add-on software.

## Important lifetime rule

The application configuration stores pointers to strings, file-filter arrays, settings arrays, enumeration option arrays, and setting values. Keep all of these objects alive for the entire call to Application::Run().

The easiest approach is to create the configuration, filters, options, and settings as local variables in main(), then create the UI::Application object and call Run() before leaving main(). The demo follows this pattern.
