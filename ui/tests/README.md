# UI Library tests

## Host tests

Run the host-side tests with:

~~~sh
make -C ui/tests host-test
~~~

These tests use the host C++ compiler. They check the public file-filter matching function, the setting factory functions, and the default configuration values. They do not emulate the calculator firmware or test the native GUI implementation.

## Calculator demo

The application in `app_template/` is the UI Library's calculator demo and the practical companion to the documentation. It is built with the SH-4 cross-toolchain and linked against both the SDK and UI library.

From the repository root, run:

~~~sh
./app_template/build.sh
~~~

The build produces `app_template/app_template.bin` and `app_template/app_template.hhk`. Copy the `.hhk` file to the calculator's `fls0` root and run it with the Hollyhock launcher.

See the [Demo Walkthrough](../../doc/ui-library/demo-walkthrough.md) for instructions on trying each feature.
