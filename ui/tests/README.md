# UI library tests

## Host tests
Run:
    make -C ui/tests host-test
This checks the public filter API and setting/config factories without the calculator SDK.

## Calculator API smoke-test
With the SH4 toolchain installed:
    make -C ui/tests calculator-test
Install ui_api_test.bin or ui_api_test.hhk on the fx-CP400 and manually verify:
- Main menu Start callback returns without losing the menu.
- File browser enters directories, navigates up/back, filters files, and invokes selection callback.
- Settings toggles Boolean, cycles Enum, increments/wraps Integer, and runs Action.
- Quit restores the screen.
The custom settings callback is only used when no built-in settings entries are registered.
This target compiles and links a test app; it cannot simulate the firmware's GUI event loop.
