#include <sdk/calc/calc.hpp>
#include "apps.hpp"
#include "bins.hpp"
#include "execs.hpp"
#include "screen.hpp"

void main() {
    // Save the native calculator screen so it can be restored when the
    // launcher exits. The UI framebuffer itself deliberately does not own
    // calculator lifecycle or VRAM backup state.
    calcInit();

    // Preserve the existing discovery and execution paths. The launcher UI
    // uses only the UI library's framebuffer and normalized input components.
    Apps::LoadAppInfo();
    Bins::LoadAppInfo();
    Execs::LoadExecInfo();

    LauncherScreen::Run();

    // Restore the exact screen content that was present before this launcher.
    // calcEnd() also refreshes the LCD immediately.
    calcEnd();
}
