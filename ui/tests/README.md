# UI Library tests

## Host tests

Run the host-side tests with:

~~~sh
make -C ui/tests host-test
~~~

These tests use the host C++ compiler. They check file-filter matching, setting factories, and default configuration values, including that MCS persistence is disabled unless a folder is explicitly configured. They do not emulate calculator firmware, MCS calls, or the native GUI implementation.

## Calculator UI test

The ui_test/ application tests the public read-only file-browser API on the calculator. It exercises path normalization and rejection, directory enumeration, filtering, file metadata, opening, handle state, seeking, reading, and closing. It also uses a separate MCS folder named UITEST to test persistent built-in settings.

Build it from the repository root:

~~~sh
./ui_test/build.sh
~~~

The build produces ui_test/ui_test.bin and ui_test/ui_test.hhk. Copy the .hhk file to the calculator's fls0 root and run it with the Hollyhock launcher. All file-system operations exposed by this UI test are read-only.

The test also writes and reads a small test variable through PersistentStore in the UITEST Calc Var folder. This is separate from the fls0 file-system API, which remains read-only. Built-in settings use a separate SETTINGS variable in the same app folder.

The app_template/ program remains the smaller example used in the documentation:

~~~sh
./app_template/build.sh
~~~
