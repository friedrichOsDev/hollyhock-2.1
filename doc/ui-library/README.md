# Hollyhock UI Library

A small C++ helper library for fx-CP400 applications built with the Hollyhock 2.1 SDK. It provides an optional main menu, direct LCD framebuffer drawing, normalized key/touch input, a read-only file selector and file-system wrapper, settings, and MCS-backed persistence.

## Documentation

- [Getting started](getting-started.md)
- [Application menus and settings](application.md)
- [Framebuffer and input](framebuffer-input.md)
- [File system API](filesystem.md)
- [API reference](api-reference.md)
- [Demo walkthrough](demo-walkthrough.md)

## Features

- Optional built-in Start, Files, Settings, and Quit menu.
- Independent Native, Framebuffer, or Custom renderer selection for each menu.
- Custom application screens using Framebuffer and Input.
- RGB565 drawing directly into LCD VRAM.
- Bounded pixel, rectangle, line, and text drawing.
- Embedded 5x7 printable-ASCII bitmap font.
- Normalized key, touch, action-bar, and unknown events.
- Existing read-only file selector and file-system API.
- Boolean, integer, enumeration, and action settings.
- Optional per-application MCS settings persistence.

## Per-menu renderers

The main menu, file selector, and settings screen each default to the original Native SDK GUI. Applications can choose the library's Framebuffer menus or supply Custom callbacks independently. See [Application menus and settings](application.md) for the renderer configuration and custom file-selection API.

## Custom screens

The built-in menu remains enabled by default. Set Config::showMainMenu to false to invoke onStart directly after the display lifecycle has been initialized. The callback can own the full screen using Framebuffer and Input. It can also request the existing file selector or settings screen; when either returns, onStart is called again.

See framebuffer-input.md for lifecycle rules and examples.

## Display lifecycle

Framebuffer attaches to the existing LCD VRAM and does not allocate a second image buffer. It does not manage VRAM backup/restore. Normally create Framebuffer inside an Application callback after Application::Run() has called calcInit(). Application remains responsible for calcEnd() and restoring the previous display state.

## Read-only file access

The UI Library intentionally exposes no file-writing or file-modification API. Its file wrapper only opens in read-only mode. The underlying SDK remains unchanged.

## Persistent settings

Set UI::Config::settingsFolder to a unique 1-8 character alphanumeric MCS folder name. The library loads settings before the first application screen, saves after changes, and saves again on normal exit. The serializer supports at most 32 entries and a payload smaller than 512 bytes.

## Limitations

- File-system wrapper supports fls0 only.
- Framebuffer draws directly to VRAM; there is no double-buffer.
- Built-in font supports printable ASCII 32-126; lowercase is rendered as uppercase.
- DrawText handles at most 512 input bytes per call.
- Input wrapper exposes the SDK's single-touch data; multi-touch is not included.
- The library is a small drawing/input layer, not a general widget framework.

## Build

From the repository root:

~~~sh
./app_template/build.sh
~~~

This builds the SDK, UI library, and custom framebuffer demo. Copy app_template/app_template.hhk to the calculator's fls0 root and launch it with the Hollyhock launcher.

## Host tests

Run:

~~~sh
make -C ui/tests clean host-test
~~~

The tests mock the SDK calls. They do not replace testing on the fx-CP400.
