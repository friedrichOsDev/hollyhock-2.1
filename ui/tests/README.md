# UI Library tests

## Host tests

Run:

~~~sh
make -C ui/tests clean host-test
~~~

The host tests use the host C++ compiler and mocked SDK calls. They cover file filters, setting factories and defaults, path normalization, file and directory wrappers, read-only open flags, resource cleanup, framebuffer drawing and clipping, font bounds, and normalized key/touch/system input events.

The host tests do not emulate calculator firmware, MCS internals, or the native GUI implementation. PersistentStore, display restoration, and actual touch/key behavior still require testing on the fx-CP400.
