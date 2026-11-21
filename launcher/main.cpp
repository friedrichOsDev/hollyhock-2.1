#include <ui/ui.hpp>
#include "apps.hpp"
#include "bins.hpp"
#include "execs.hpp"
#include "screen.hpp"

void main() {
    Apps::LoadAppInfo();
    Bins::LoadAppInfo();
    Execs::LoadExecInfo();

    UI::Config config;
    config.title = "Hollyhock Launcher";
    config.showMainMenu = false;
    config.onStart = LauncherScreen::Run;

    // This low-level binary is entered by the OS loader, which owns lifecycle.
    config.manageCalculatorLifecycle = false;

    UI::Application application(config);
    application.Run();
}
