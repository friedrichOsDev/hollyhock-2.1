# Demo Walkthrough

The application in app_template/ is a practical example of the UI Library. It uses read-only file-system access and MCS-backed settings persistence. Its source is app_template/main.cpp.

## Build and install

From the repository root:

~~~sh
./app_template/build.sh
~~~

Copy app_template/app_template.hhk to the root of the calculator's fls0 storage and start it with the Hollyhock launcher.

## Main menu

| Entry | What it demonstrates |
| --- | --- |
| Storage check | Path normalization, existence checks, directory enumeration, and persistence status. |
| Browse files | The built-in file selector with a .txt filter. |
| Settings | Boolean, integer, enumeration, reset, and automatic persistence. |
| Quit | Leaves the UI Library application loop. |

## 1. Storage check

Select Storage check. The callback normalizes fls0/, checks whether the root exists, opens the root directory, counts entries, closes the directory handle, and displays the MCS persistence status.

The SDK's find API uses a negative result for both end-of-search and errors. The displayed count is the number of entries returned before the first negative result.

## 2. Persistent settings

The demo sets config.settingsFolder to UIDEMO. On the first launch, the library immediately creates the SETTINGS variable with default values. Later setting changes are saved automatically and persisted values are loaded before the next menu appears.

Try changing Preview bytes or Text case, exit the application, and start it again. The selected values should be restored. This behavior uses Calc Vars, not files on fls0. The folder name is unique to this demo and within the SDK's eight-character limit.

## 3. Browse and read files

Select Browse files. The selector starts at the root of fls0. Its filter accepts .txt files; matching is case-insensitive and directories remain visible. Navigate into folders and select a text file.

The callback demonstrates the read-only wrapper:

1. Opens the selected path using UI::File::Open(), which always uses OPEN_READ.
2. Retrieves file size.
3. Seeks to the beginning of the file.
4. Reads up to the configured number of bytes.
5. Replaces non-printable bytes and optionally changes text case.
6. Closes the descriptor before displaying results.

The preview is intended for simple text files, not binary data or full Unicode text. The callback never modifies the selected file.

## Read-only boundary

The UI Library cannot create, write, delete, rename, move, or create directories. The underlying Hollyhock SDK still contains its lower-level file-system functions; this library restriction does not remove or change them.

## Source map

- app_template/main.cpp — demo callbacks, settings, filters, and application configuration.
- ui/include/ui/application.hpp and ui/src/application.cpp — menu state machine and automatic settings persistence.
- ui/include/ui/storage.hpp and ui/src/storage.cpp — MCS wrapper.
- ui/include/ui/filesystem.hpp and ui/src/filesystem.cpp — read-only file, directory, and path wrappers.
