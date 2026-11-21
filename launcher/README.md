# Hollyhock Launcher

The launcher keeps its existing discovery and execution backends while using
the Hollyhock UI Library for its user interface.

## Preserved program types

- **HHK applications**: .hhk files in the root of the fls0 and drv0 volumes.
  ELF metadata sections provide the display name, description, author, and
  version.
- **BIN applications**: .bin files in the root of the fls0 and drv0 volumes.
  Existing BIN header and entry-point handling remain in the BIN loader. The
  launcher's own run.bin is excluded.
- **Program variables**: executable program variables in the hhk folder
  continue to be discovered and launched by the existing EXEC loader.

The ELF, BIN, and EXEC loaders remain separate from the UI. The launcher still
produces run.bin and retains its existing linker/bootstrap arrangement.

## UI controls

The screen is rendered by UI::Framebuffer; events are read through UI::Input.

- Up / Down: select a program.
- Left / Right: move one visible page backward / forward.
- EXE: run the selected program.
- Backspace, Clear, or action-bar Escape: close the launcher.
- Touch a row to select it.
- Touch PREV, RUN, NEXT, or CLOSE at the bottom to invoke that action.

The detail area shows type, metadata, source path, and description where
available. A loader error is shown in the list rather than silently closing
the launcher.

## Independent build

From the repository root:

~~~sh
./launcher/build.sh
~~~

This builds the SDK, the UI Library, and the launcher in that order. It does
not invoke or depend on app_template. The output is launcher/run.bin.

To remove only launcher build outputs:

~~~sh
./launcher/build.sh clean
~~~

The SDK and UI library outputs are shared with other projects and are not
removed by this command.

The equivalent manual build is:

~~~sh
make -C sdk
make -C ui SDK_DIR=../sdk
make -C launcher SDK_DIR=../sdk UI_DIR=../ui
~~~

SDK_DIR remains required by the launcher Makefile, matching the previous
build convention. UI_DIR defaults to ../ui.

## Lifecycle boundary

The launcher is a low-level program started by the OS loader. The UI Library
manages launcher state and drawing/input, but manageCalculatorLifecycle is
disabled so the launcher does not take ownership of the calculator lifecycle
that was previously managed by its environment.

init.s, linker.ld, and main_bootstrap.cpp are unchanged. The bootstrap
continues to return the SDK message-box result expected by the OS after the
launcher closes or a launched program returns.
