# UI library tests

## Host tests

Run:

    make -C ui/tests host-test

This checks the public filter API and setting/config factories without the calculator SDK.

## Temporary calculator smoke test

The calculator smoke-test application is temporarily located in `app_template/` and is built using the normal app-template build system. It links both the SDK and UI library.

Build it with:

    ./app_template/build.sh

The generated `app_template.bin` and `app_template.hhk` can be copied to the fx-CP400 for manual testing. This temporary test app will be replaced by the normal app template later.
