# UI Library tests

## Host tests

Run the host-side API tests with:

~~~sh
make -C ui/tests host-test
~~~

The tests use the host C++ compiler and mocked SDK file-system functions. They cover file-filter matching, setting factories and defaults, path normalization and rejection, file operations, read-only open flags, metadata access, directory enumeration, and automatic handle cleanup.

The host tests do not emulate calculator firmware, MCS internals, or the native GUI implementation. PersistentStore and automatic settings persistence therefore still require testing on the fx-CP400.
