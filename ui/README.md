# Hollyhock UI Library

A small C++ application-menu, direct LCD framebuffer, input, read-only file-system, and persistent MCS storage library for the fx-CP400.

## Full documentation

See ../doc/ui-library/README.md for the complete guide.

## Quick build

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

Link sdk/sdk.o and ui/ui.o into your application and include sdk/include and ui/include. app_template/ contains a complete example:

~~~sh
./app_template/build.sh
~~~

## Public APIs

- ui/application.hpp — per-menu Native, Framebuffer, or Custom renderers, callbacks, and settings.
- ui/framebuffer.hpp — bounded RGB565 drawing directly into LCD VRAM.
- ui/input.hpp — normalized key, touch, system, and unknown events.
- ui/filesystem.hpp — read-only file and directory access.
- ui/storage.hpp — MCS-backed PersistentStore.

## Host API tests

Run:

~~~sh
make -C ui/tests clean host-test
~~~

The host tests use mocked SDK calls and cover path normalization, file/directory wrappers, settings, framebuffer drawing and clipping, and normalized input events. They do not replace testing on the calculator.

## Display lifecycle

Framebuffer requires the LCD lifecycle to be initialized. Normally create it inside an Application callback. It does not own or restore the VRAM lifecycle. The built-in main menu, file selector, and settings each support independent Native, Framebuffer, or Custom rendering. All defaults remain Native. Set Config::showMainMenu to false to bypass the library main menu and implement custom navigation through onStart.

## Read-only boundary

The UI file-system wrapper intentionally exposes no file-writing or file-modification API. The underlying SDK remains unchanged.
