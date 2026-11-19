# Hollyhock UI Library

Custom menu UI for Hollyhock 2.1 fx-CP400 apps. Uses SDK GUIDialog, GUIButton, GUILabel and filesystem functions; does not call firmware message-box APIs.

## Build
From repository root:
    make -C sdk
    make -C ui SDK_DIR=../sdk
Link ui/ui.o and sdk/sdk.o with your app and add ui/include to compiler include paths.

## API
    #include <ui/ui.hpp>
    static void Start(UI::Application &app) { (void)app; }
    static void Selected(UI::Application &app,const char *path) { (void)app; (void)path; }
    extern "C" void main() {
      UI::Config config;
      config.title="Example"; config.onStart=Start;
      config.showFileSelector=true; config.onFileSelected=Selected;
      const UI::FileFilter filters[]={UI::FileFilter(".txt","Text"),UI::FileFilter("dat","Data")};
      config.filters=filters; config.filterCount=2;
      UI::Application app(config); app.Run();
    }

## Settings
Settings values belong to the app and are changed in-place. Supported: Boolean, Integer, Enum, Action. Keep values/options alive during Run. MCS persistence is not included.
    bool enabled=true; int level=3; int mode=1;
    const char *modes[]={"Easy","Normal","Hard"};
    UI::Setting settings[]={UI::Setting::Boolean("Enabled",&enabled),
      UI::Setting::Integer("Level",&level,1,10),
      UI::Setting::Enumeration("Mode",&mode,modes,3)};
    config.showSettings=true; config.settings=settings; config.settingCount=3;

## Navigation and lifecycle
Run drives the menu loop. Close exits; ReturnToMenu returns to the menu; OpenFileSelector and OpenSettings navigate if enabled. manageCalculatorLifecycle defaults true and calls calcInit/calcEnd; set false if the app manages display backup/restore itself.

## File filtering
Directories always show. Extensions may include or omit the dot and are matched case-insensitively. No filters displays all. Non-ASCII filenames are shown as question marks; non-ASCII paths are not supported.

## Runtime caveat
SDK exposes OnEvent and ShowDialog but no documented generic custom-dialog dismissal API. Navigation assumes custom button events return from ShowDialog. This is not hardware-verified; test on the fx-CP400.
