# Hollyhock UI Library

The Hollyhock UI Library is a small C++ helper library for applications built with the Hollyhock 2.1 SDK on the Casio fx-CP400. It provides an application menu, a read-only file selector and file-system wrapper, built-in settings, and persistent storage backed by the calculator's MCS (Calc Vars).

## Documentation

- Demo walkthrough — the calculator demo and its read-only browser workflow.
- Getting started — toolchain requirements, build steps, linking, and a minimal application.
- Application menus and settings — configuration, callbacks, navigation, settings, and automatic MCS persistence.
- File system API — normalized paths, read-only file access, directories, filters, and errors.
- API reference — public classes, methods, and data types.

## Features

- Main menu with optional Start, Files, Settings, and Quit entries.
- File selector rooted at fls0, with optional extension filters.
- Read-only file and directory wrappers with automatic handle cleanup.
- Path normalization that rejects unsupported volumes and parent traversal.
- Boolean, integer, enumeration, and action settings.
- Optional per-application MCS folders for persistent settings.
- PersistentStore API for application-owned Calc Var data.

## Read-only file access

The UI Library intentionally exposes no file-writing or file-modification API. It has no write modes, Write(), create, delete, rename, move, or directory-creation operations. The built-in browser only navigates, lists, inspects, and reads files.

This restriction does not modify the Hollyhock SDK. Its lower-level file functions remain available to applications that deliberately choose to use the SDK directly. Such calls are outside the UI Library's read-only wrapper.

## Persistent settings

Set UI::Config::settingsFolder to a unique 1-8 character alphanumeric MCS folder name to enable automatic loading and saving of built-in settings. The library creates the folder and immediately persists the initial default settings when the SETTINGS variable is missing. Subsequent setting changes are saved automatically, and the application saves once more on normal exit. Names longer than eight characters are rejected to avoid the SDK's silent folder-name truncation.

The automatic serializer supports at most 32 settings and a payload smaller than 512 bytes. See the application guide for its format and limits. Applications can use UI::PersistentStore for their own MCS variables and data.

## Limitations

The file-system wrapper supports fls0 only. File and directory names are converted to printable ASCII; characters outside ASCII 32-126 are shown as question marks. Non-ASCII paths are not supported.

The UI library does not provide a general-purpose widget system or custom dialog builder. The built-in selector collects up to 64 entries per directory and displays five per page.

The SDK and UI library are freestanding and linked into each application rather than loaded as a separate runtime library.

## Build the demo

From the repository root:

~~~sh
./app_template/build.sh
~~~

The script builds the SDK, UI library, and demo application. It produces app_template/app_template.hhk and app_template/app_template.bin. Copy the .hhk file to the calculator's fls0 root and launch it with the Hollyhock launcher.

## Lifetime rule

The application configuration stores pointers to strings, filters, settings arrays, option arrays, and setting values. Keep these alive for the entire call to Application::Run(). The demo follows this pattern.
