# UI Library tests

## Host tests

Run the host-side API tests with:

~~~sh
make -C ui/tests host-test
~~~

The tests use the host C++ compiler and mocked SDK file-system functions. They cover file-filter matching, setting factories and defaults, path normalization and rejection, file operations, read-only open flags, metadata access, directory enumeration, and automatic handle cleanup.

The host tests do not emulate calculator firmware, MCS internals, or the native GUI implementation. PersistentStore and automatic settings persistence therefore still require testing on the fx-CP400.

## Calculator UI test

The `ui_test/` application tests the public read-only file-browser API on the calculator. It exercises path normalization and rejection, directory enumeration, filtering, file metadata, opening, handle state, seeking, reading, and closing. It also uses a separate MCS folder named `UITEST` to test persistent built-in settings.

Build it from the repository root:

~~~sh
./ui_test/build.sh
~~~

The build produces `ui_test/ui_test.bin` and `ui_test/ui_test.hhk`. Copy the `.hhk` file to the calculator's fls0 root and run it with the Hollyhock launcher. All file-system operations exposed by this UI test are read-only.

The test also writes and reads a small test variable through `PersistentStore` in the `UITEST` Calc Var folder. This is separate from the fls0 file-system API, which remains read-only. Built-in settings use a separate `SETTINGS` variable in the same app folder.

The `app_template/` program remains the smaller example used in the documentation:

~~~sh
./app_template/build.sh
~~~
