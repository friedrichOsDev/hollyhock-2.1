# Hollyhock UI Library

A small C++ application-menu and read-only file-system helper library for the fx-CP400, built on top of this repository's Hollyhock 2.1 SDK. It also provides an MCS (Calc Vars) storage wrapper and optional automatic persistence for built-in settings.

## Full documentation

See ../doc/ui-library/README.md for the complete guide, including build instructions, application menus, settings persistence, the read-only file API, and current limitations.

## Quick build

From the repository root:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
~~~

This creates sdk/sdk.o and ui/ui.o. The UI Makefile automatically includes all sources in ui/src/, including the MCS storage wrapper.

Link both objects into your application and add sdk/include and ui/include to the compiler include paths. The app_template/ directory contains a complete example:

~~~sh
./app_template/build.sh
~~~

## Demo application

The demo is read-only for file-system access. It demonstrates root-directory enumeration, the built-in filtered file selector, file metadata, read/seek operations, and configurable text previews. Its built-in settings are stored in the UIDEMO MCS folder.

## SDK boundary

The UI file-system wrapper is intentionally read-only. This does not remove or change any lower-level file-system functions from the Hollyhock SDK.
