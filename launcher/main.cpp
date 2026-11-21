#include "apps.hpp"
#include "bins.hpp"
#include "execs.hpp"
#include "screen.hpp"

void main() {
    // Preserve the existing discovery and execution paths. The launcher UI
    // uses only the UI library's framebuffer and normalized input components.
    Apps::LoadAppInfo();
    Bins::LoadAppInfo();
    Execs::LoadExecInfo();

    LauncherScreen::Run();
}
