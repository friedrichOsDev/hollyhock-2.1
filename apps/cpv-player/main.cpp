#include <appdef.hpp>
#include <ui/ui.hpp>
#include "cpv_decoder.hpp"

APP_NAME("CPV Video Player")
APP_DESCRIPTION("CPV v1.3 video player for fx-CP400")
APP_AUTHOR("friedrichOsDev")
APP_VERSION("3.0")

namespace {

// The current SDK exposes input polling but no monotonic millisecond clock.
// This value is a starting point and must be calibrated on the real fx-CP400.
// Adjust it if measured playback runs too fast or too slowly.
static const uint32_t kSpinIterationsPerMs = 50000U;
static const uint32_t kWaitSliceMs = 10U;
static const uint32_t kSeekStepMs = 5000U;

void WaitMilliseconds(uint32_t milliseconds) {
    volatile uint32_t iterations = milliseconds * kSpinIterationsPerMs;
    while (iterations != 0)
        --iterations;
}

void AppendNumber(uint32_t value, char *out, unsigned capacity) {
    if (!out || capacity == 0)
        return;
    char digits[11];
    unsigned count = 0;
    do {
        uint32_t quotient = 0;
        uint32_t remainder = value;
        while (remainder >= 10U) {
            remainder -= 10U;
            ++quotient;
        }
        digits[count++] = static_cast<char>('0' + remainder);
        value = quotient;
    } while (value && count < sizeof(digits));

    unsigned position = 0;
    while (count && position + 1U < capacity)
        out[position++] = digits[--count];
    out[position] = 0;
}

void DrawError(const char *title, const char *message) {
    UI::Framebuffer screen;
    if (!screen.Begin())
        return;
    screen.Clear(UI::Colors::Black);
    screen.DrawText(12, 24, title ? title : "CPV ERROR", UI::Colors::Red);
    screen.DrawText(12, 44, message ? message : "Unknown error",
                    UI::Colors::White);
    screen.DrawText(12, screen.Height() - 18, "EXE OR BACK TO RETURN",
                    UI::Colors::Gray);
    screen.Present();

    bool done = false;
    while (!done) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0)
            break;
        if (result == 1 && event.type == UI::EventKey &&
            event.keyAction == UI::KeyPressed &&
            (event.keyCode == UI::KeyEXE ||
             event.keyCode == UI::KeyBackspace ||
             event.keyCode == UI::KeyPowerClear))
            done = true;
        if (result == 1 && event.type == UI::EventSystem &&
            event.systemEvent == UI::SystemActionBarEscape)
            done = true;
        if (result == 0)
            WaitMilliseconds(10);
    }
}

struct PlaybackState {
    bool paused;
    bool ended;
    bool exit;
    bool redraw;
    uint32_t remainingMs;
    PlaybackState()
        : paused(false), ended(false), exit(false), redraw(true),
          remainingMs(0) {}
};

int DividePositive(int dividend, int divisor) {
    if (dividend <= 0 || divisor <= 0)
        return 0;
    int quotient = 0;
    while (dividend >= divisor) {
        dividend -= divisor;
        ++quotient;
    }
    return quotient;
}

void DrawFrame(UI::Framebuffer &screen, const CPV::Decoder &decoder,
               bool showControls, bool paused, bool ended) {
    screen.Clear(UI::Colors::Black);
    const int width = decoder.Width();
    const int height = decoder.Height();
    int scaleX = DividePositive(screen.Width(), width);
    int scaleY = DividePositive(screen.Height(), height);
    int scale = scaleX < scaleY ? scaleX : scaleY;
    if (scale < 1)
        scale = 1;
    if (scale > 64)
        scale = 64;

    const int drawWidth = width * scale;
    const int drawHeight = height * scale;
    const int x = (screen.Width() - drawWidth) / 2;
    const int y = (screen.Height() - drawHeight) / 2;
    screen.BlitRGB565(x, y, decoder.Pixels(), width, height, scale);

    if (showControls || ended) {
        screen.FillRect(0, 0, screen.Width(), 16, UI::Colors::Black);
        screen.DrawText(4, 4, "CPV PLAYER", UI::Colors::White);
        char frameText[32];
        frameText[0] = 'F'; frameText[1] = ' ';
        AppendNumber(decoder.CurrentFrame() + 1U, frameText + 2,
                     sizeof(frameText) - 2U);
        const unsigned used = 2U + (
            decoder.CurrentFrame() >= 999999999U ? 10U :
            decoder.CurrentFrame() >= 99999999U ? 9U :
            decoder.CurrentFrame() >= 9999999U ? 8U :
            decoder.CurrentFrame() >= 999999U ? 7U :
            decoder.CurrentFrame() >= 99999U ? 6U :
            decoder.CurrentFrame() >= 9999U ? 5U :
            decoder.CurrentFrame() >= 999U ? 4U :
            decoder.CurrentFrame() >= 99U ? 3U :
            decoder.CurrentFrame() >= 9U ? 2U : 1U);
        frameText[used] = 0;
        screen.DrawText(90, 4, frameText, UI::Colors::Gray);
        const char *status = ended ? "END" : (paused ? "PAUSE" : "PLAY");
        screen.DrawText(screen.Width() - 42, 4, status,
                        ended ? UI::Colors::Yellow : UI::Colors::Green);
    }
    screen.Present();
}

void SeekRelative(CPV::Decoder &decoder, int32_t delta,
                  PlaybackState &state) {
    uint32_t current = decoder.CurrentTimeMs();
    uint32_t target = current;
    if (delta < 0) {
        const uint32_t amount = static_cast<uint32_t>(-delta);
        target = current > amount ? current - amount : 0;
    } else {
        const uint32_t amount = static_cast<uint32_t>(delta);
        target = amount > decoder.DurationMs() - current
            ? decoder.DurationMs() - 1U : current + amount;
    }
    if (decoder.Seek(target)) {
        state.remainingMs = decoder.CurrentFrameDurationMs();
        state.redraw = true;
        state.ended = false;
    }
}

void HandleEvent(const UI::Event &event, CPV::Decoder &decoder,
                 PlaybackState &state, int screenWidth) {
    if (event.type == UI::EventSystem &&
        event.systemEvent == UI::SystemActionBarEscape) {
        state.exit = true;
        return;
    }
    if (event.type == UI::EventKey && event.keyAction == UI::KeyPressed) {
        if (event.keyCode == UI::KeyBackspace ||
            event.keyCode == UI::KeyPowerClear) {
            state.exit = true;
        } else if (event.keyCode == UI::KeyEXE) {
            if (state.ended) {
                if (decoder.Seek(0)) {
                    state.ended = false;
                    state.paused = false;
                    state.redraw = true;
                    state.remainingMs = decoder.CurrentFrameDurationMs();
                }
            } else {
                state.paused = !state.paused;
            }
        } else if (event.keyCode == UI::KeyLeft) {
            SeekRelative(decoder, -static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.keyCode == UI::KeyRight) {
            SeekRelative(decoder, static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.keyCode == UI::KeyUp) {
            SeekRelative(decoder, -20000, state);
        } else if (event.keyCode == UI::KeyDown) {
            SeekRelative(decoder, 20000, state);
        }
    } else if (event.type == UI::EventTouch &&
               event.touchAction == UI::TouchUp) {
        const int third = DividePositive(screenWidth, 3);
        if (event.x < third) {
            SeekRelative(decoder, -static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.x >= screenWidth - third) {
            SeekRelative(decoder, static_cast<int32_t>(kSeekStepMs), state);
        } else if (state.ended) {
            if (decoder.Seek(0)) {
                state.ended = false;
                state.paused = false;
                state.redraw = true;
                state.remainingMs = decoder.CurrentFrameDurationMs();
            }
        } else {
            state.paused = !state.paused;
        }
    }
}

void PlayVideo(UI::Application &app, const char *path,
               bool repeatPlayback, bool showControls) {
    CPV::Decoder decoder;
    if (!decoder.Open(path)) {
        DrawError("CPV OPEN ERROR", decoder.Error());
        return;
    }
    if (!decoder.DecodeNext()) {
        DrawError("CPV DECODE ERROR", decoder.Error());
        return;
    }

    UI::Framebuffer screen;
    if (!screen.Begin()) {
        DrawError("DISPLAY ERROR", "LCD framebuffer unavailable");
        return;
    }

    PlaybackState state;
    const char *playbackError = 0;
    state.remainingMs = decoder.CurrentFrameDurationMs();
    while (app.IsRunning() && !state.exit) {
        if (state.redraw) {
            DrawFrame(screen, decoder, showControls, state.paused, state.ended);
            state.redraw = false;
        }

        if (state.ended) {
            UI::Event event;
            const int result = UI::Input::Poll(&event);
            if (result < 0)
                break;
            if (result == 1)
                HandleEvent(event, decoder, state, screen.Width());
            if (!state.exit)
                WaitMilliseconds(kWaitSliceMs);
            continue;
        }

        if (!state.paused && state.remainingMs == 0) {
            if (decoder.AtEnd()) {
                if (repeatPlayback && decoder.Seek(0)) {
                    state.redraw = true;
                    state.remainingMs = decoder.CurrentFrameDurationMs();
                } else {
                    state.ended = true;
                    state.redraw = true;
                }
                continue;
            }
            if (!decoder.DecodeNext()) {
                playbackError = decoder.Error();
                break;
            }
            state.redraw = true;
            state.remainingMs = decoder.CurrentFrameDurationMs();
            continue;
        }

        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0)
            break;
        if (result == 1)
            HandleEvent(event, decoder, state, screen.Width());
        if (!state.exit) {
            WaitMilliseconds(kWaitSliceMs);
            if (!state.paused && state.remainingMs > kWaitSliceMs)
                state.remainingMs -= kWaitSliceMs;
            else if (!state.paused)
                state.remainingMs = 0;
        }
    }
    screen.End();
    if (playbackError)
        DrawError("CPV DECODE ERROR", playbackError);
}

static bool g_repeatPlayback = false;
static bool g_showControls = true;

void CPVFileSelected(UI::Application &app, const char *path) {
    PlayVideo(app, path, g_repeatPlayback, g_showControls);
}

} // namespace

extern "C" void main() {
    UI::Config config;
    config.title = "CPV Player";
    config.settingsFolder = "CPVPL";
    config.filesLabel = "Play video";
    config.settingsLabel = "Settings";
    config.quitLabel = "Exit";
    config.showMainMenu = true;
    config.showStart = false;
    config.showFileSelector = true;
    config.showSettings = true;
    config.showQuit = true;
    config.mainMenuRenderer = UI::MenuRenderer::Framebuffer;
    config.fileSelectorRenderer = UI::MenuRenderer::Framebuffer;
    config.settingsRenderer = UI::MenuRenderer::Framebuffer;
    config.manageCalculatorLifecycle = true;

    const UI::FileFilter filters[] = {
        UI::FileFilter(".cpv", "CPV v1 video files")
    };
    config.filters = filters;
    config.filterCount = sizeof(filters) / sizeof(filters[0]);
    config.onFileSelected = CPVFileSelected;

    UI::Setting settings[] = {
        UI::Setting::Boolean("playback.repeat", "Repeat video",
                             &g_repeatPlayback),
        UI::Setting::Boolean("display.controls", "Show controls",
                             &g_showControls)
    };
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);

    UI::Application app(config);
    app.Run();
}
