# Demo Walkthrough

The application in app_template/ is a practical, read-only example of the UI Library. It is designed to be explored on the fx-CP400, with its source in app_template/main.cpp.

## Build and install

From the repository root, build the SDK, UI library, and demo application:

~~~sh
./app_template/build.sh
~~~

Copy app_template/app_template.hhk to the root of the calculator's fls0 storage and start it with the Hollyhock launcher.

## Main menu

The demo has four entries:

| Entry | What it demonstrates |
| --- | --- |
| Storage check | Path normalization, existence checks, and directory enumeration. |
| Browse files | The built-in file selector with a .txt extension filter. |
| Settings | Boolean, integer, enumeration, and reset settings. |
| Quit | Leaves the UI Library application loop. |

## 1. Storage check

Select Storage check. The callback normalizes fls0/, checks whether the root exists, opens the root directory, and counts the returned entries. It explicitly closes the directory handle and displays the results.

The SDK wrapper uses a negative result for both the end of a search and an error. The displayed count is the number of entries returned before the first negative result; it is not a separate verification that every possible entry was read.

## 2. Settings

Select Settings. The demo defines these in-memory settings:

| Setting | Type | Effect |
| --- | --- | --- |
| Show preview | Boolean | Enables or disables the preview after selecting a file. File size is still displayed. |
| Preview bytes | Integer | Changes the number of file bytes read for the preview, from 8 to 48 in steps of 8. |
| Text case | Enumeration | Displays preview letters as Original, Uppercase, or Lowercase. |
| Reset settings | Action | Restores the default values and returns to the main menu. |

These settings change ordinary C++ variables owned by the application. They are not persistent yet. The descriptions passed to the setting factories are stored in Setting objects, but the current settings screen does not display them.

## 3. Browse and read files

Select Browse files. The file selector starts at the root of fls0. Its filter accepts .txt files; matching is case-insensitive and directories remain visible. Navigate into folders as needed and select a text file.

The selection callback demonstrates the read-only file wrapper:

1. Opens the selected path using UI::File::Open(), which always opens for reading.
2. Retrieves the file size using UI::File::GetSize().
3. Reads up to the configured number of bytes using UI::File::Read().
4. Replaces non-printable bytes and optionally changes the text case.
5. Closes the descriptor before displaying the result.

The preview is intended for simple text files, not binary data or full Unicode text. The callback never modifies the selected file.

## 4. Quit

Select Quit to leave Application::Run(). With the default manageCalculatorLifecycle setting, the UI Library calls calcEnd() when the application exits to restore the display state managed by the SDK.

## Read-only boundary

The UI Library does not provide a file-writing API. It cannot create, write, delete, rename, move, or create directories. The underlying Hollyhock SDK still contains its lower-level file-system functions; this library restriction does not remove or change them.

## Source map

- app_template/main.cpp — demo callbacks, settings, filters, and application configuration.
- ui/include/ui/application.hpp and ui/src/application.cpp — menu state machine and UI behavior.
- ui/include/ui/types.hpp and ui/src/types.cpp — configuration, setting factories, and extension matching.
- ui/include/ui/filesystem.hpp and ui/src/filesystem.cpp — read-only file, directory, and path wrappers.
