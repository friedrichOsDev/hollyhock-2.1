# Demo Walkthrough

The application in `app_template/` is a practical example of the UI Library. It is designed to be explored on the fx-CP400, with the source code in `app_template/main.cpp`.

## Build and install

From the repository root, build the SDK, UI library, and demo application:

~~~sh
./app_template/build.sh
~~~

Copy `app_template/app_template.hhk` to the root of the calculator's `fls0` storage and start it with the Hollyhock launcher. See the [repository's launcher guide](../user/using.md) for the launch process.

## Main menu

The demo has four entries:

| Entry | What it demonstrates |
| --- | --- |
| Storage check | Uses `FileSystem::NormalizePath()` and `Directory::Open()`, `Next()`, and `Close()`. |
| Browse files | Opens the built-in file selector with a `.txt` extension filter. |
| Settings | Demonstrates all four setting types and changes the file preview behavior. |
| Quit | Leaves the UI Library application loop. |

## 1. Storage check

Select **Storage check**.

The callback normalizes the path of the sample file and opens the root directory on `fls0`. It calls `Directory::Next()` until the function returns a negative value, then explicitly closes the directory handle.

The screen displays the normalized sample-file path (or an error if normalization failed) and how many entries were returned by the directory search. The SDK wrapper uses a negative result for both the end of a search and an error, so the displayed count is the number of entries returned before that result. It is not a separate verification that every possible entry was read.

Press a number key to return to the main menu. This uses the SDK's debug text input, not a UI Library button.

## 2. Settings

Select **Settings**. The demo defines the following settings:

| Setting | Type | Effect |
| --- | --- | --- |
| Show preview | Boolean | Enables or disables the text preview after selecting a file. File size is still displayed. |
| Preview bytes | Integer | Changes the number of file bytes read for the preview, from 4 to 24 in steps of 4. |
| Text case | Enumeration | Displays preview letters as Original, Uppercase, or Lowercase. |
| Create sample | Action | Writes the demonstration text to `fls0/ui_library_demo.txt`. |
| Reset settings | Action | Restores the default values and returns to the main menu. |

These settings change ordinary C++ variables owned by the application. They are not saved permanently by the UI Library. The descriptions passed to the setting factories are stored in the `Setting` objects, but the current settings screen does not display them.

The sample-file action uses `UI::File::Open()` with `UI::FileCreate` and writes a fixed text buffer with `UI::File::Write()`. The action checks for a negative result and also checks whether the complete buffer was written. The wrapper does not add a file-truncation operation, so do not treat `FileCreate` as a general-purpose way to replace arbitrary longer files.

## 3. Browse and read the sample file

First use **Settings → Create sample** to create the demonstration file. Return to the main menu and select **Browse files**.

The file selector starts at the root of `fls0`. Its filter accepts `.txt` files; matching is case-insensitive and directories remain visible. Navigate into folders as needed and select `ui_library_demo.txt`.

The selection callback demonstrates the file wrapper:

1. Opens the selected path with `UI::File::Open()`.
2. Retrieves the file size with `UI::File::GetSize()`.
3. Reads up to the configured number of bytes with `UI::File::Read()`.
4. Replaces non-printable bytes with spaces and applies the selected text case.
5. Closes the descriptor before displaying the result.

When preview is enabled, the demo displays a short preview. When preview is disabled, it still displays the file size. The preview is intended for simple text files, not binary data or full Unicode text.

Press a number key to return. The demo uses `Debug_WaitKey()`, whose current SDK documentation describes responses for number keys and the Power/Clear key.

## 4. Quit

Select **Quit** to leave `Application::Run()`. With the default `manageCalculatorLifecycle` setting, the UI Library calls `calcEnd()` when the application exits to restore the display state managed by the SDK.

## Source map

The relevant files are:

- `app_template/main.cpp` — complete demo callbacks, settings, filters, and application configuration.
- `ui/include/ui/application.hpp` and `ui/src/application.cpp` — menu state machine and UI behavior.
- `ui/include/ui/types.hpp` and `ui/src/types.cpp` — configuration, setting factories, and extension matching.
- `ui/include/ui/filesystem.hpp` and `ui/src/filesystem.cpp` — file, directory, and path wrappers.
