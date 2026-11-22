#include <appdef.hpp>
#include <ui/ui.hpp>
#include "cpv_decoder.hpp"

APP_NAME("CPV Video Player")
APP_DESCRIPTION("CPV v1.3 video player for fx-CP400")
APP_AUTHOR("friedrichOsDev")
APP_VERSION("3.2")

namespace {

// No reliable public monotonic timer is currently exposed by this SDK.
// Playback pacing is deliberately disabled until hardware timing is added.
static const uint32_t kSpinIterationsPerMs = 0U;
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

struct PlaybackState {
    bool paused;
    bool ended;
    bool exit;
    bool redraw;
    bool touchLatched;
    uint16_t latchedKey;
    uint32_t remainingMs;
    uint32_t timerEventCount;
    const char *lastInput;
    const char *phase;
    uint32_t workCurrent;
    uint32_t workTotal;
    uint16_t lastRawType;
    uint16_t lastKeyCode;
    uint32_t lastRawAction;
    int32_t lastTouchX;
    int32_t lastTouchY;

    PlaybackState()
        : paused(false), ended(false), exit(false), redraw(true),
          touchLatched(false), latchedKey(0), remainingMs(0),
          timerEventCount(0), lastInput("START"), phase("START"),
          workCurrent(0), workTotal(0), lastRawType(0), lastKeyCode(0),
          lastRawAction(0), lastTouchX(0), lastTouchY(0) {}
};

struct DebugContext {
    UI::Framebuffer *screen;
    CPV::Decoder *decoder;
    PlaybackState *state;
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

unsigned TextLength(const char *text) {
    unsigned length = 0;
    if (text) {
        while (text[length] && length < 80U)
            ++length;
    }
    return length;
}

void DrawInlineText(UI::Framebuffer &screen, int &x, int y,
                    const char *label, const char *value) {
    screen.DrawText(x, y, label, UI::Colors::Gray);
    x += static_cast<int>(TextLength(label) * 6U);
    screen.DrawText(x, y, value ? value : "-", UI::Colors::White);
    x += static_cast<int>(TextLength(value ? value : "-") * 6U) + 8;
}

void DrawInlineNumber(UI::Framebuffer &screen, int &x, int y,
                      const char *label, uint32_t value) {
    char number[12];
    AppendNumber(value, number, sizeof(number));
    DrawInlineText(screen, x, y, label, number);
}

void DrawDebugPage(UI::Framebuffer &screen, const CPV::Decoder &decoder,
                   const PlaybackState &state, const char *operation,
                   uint32_t current, uint32_t total) {
    // fx-CP400 is used in portrait orientation (about 320x528). Keep the
    // diagnostic panel in the top strip; the video remains visible below.
    const int panelHeight = 142;
    screen.FillRect(0, 0, screen.Width(), panelHeight, UI::Colors::Black);
    screen.DrawText(8, 4, "CPV DEBUG", UI::Colors::Yellow);
    screen.DrawText(112, 4, state.paused ? "PAUSED" :
                    (state.ended ? "ENDED" : "PLAYING"),
                    state.paused ? UI::Colors::Yellow : UI::Colors::Green);
    screen.DrawLine(8, 14, screen.Width() - 8, 14, UI::Colors::Gray);

    screen.DrawText(8, 20, "OP:", UI::Colors::Gray);
    screen.DrawText(32, 20, operation ? operation : "UNKNOWN",
                    UI::Colors::White);
    screen.DrawText(8, 32, "IN:", UI::Colors::Gray);
    screen.DrawText(32, 32, state.lastInput ? state.lastInput : "NONE",
                    UI::Colors::White);

    int x = 8;
    DrawInlineNumber(screen, x, 44, "EVT:", state.lastRawType);
    DrawInlineNumber(screen, x, 44, "ACT:", state.lastRawAction);
    DrawInlineNumber(screen, x, 44, "KEY:", state.lastKeyCode);

    x = 8;
    DrawInlineNumber(screen, x, 56, "X:", static_cast<uint32_t>(state.lastTouchX));
    DrawInlineNumber(screen, x, 56, "Y:", static_cast<uint32_t>(state.lastTouchY));

    x = 8;
    DrawInlineNumber(screen, x, 68, "WORK:", current);
    DrawInlineText(screen, x, 68, "/", "");
    DrawInlineNumber(screen, x, 68, "", total);
    DrawInlineNumber(screen, x, 68, "TICK:", state.timerEventCount);

    x = 8;
    DrawInlineNumber(screen, x, 80, "FRAME:",
                     decoder.HasFrame() ? decoder.CurrentFrame() + 1U : 0U);
    DrawInlineNumber(screen, x, 80, "LEFT:", state.remainingMs);

    x = 8;
    DrawInlineNumber(screen, x, 92, "READ:", decoder.LastReadBytes());
    DrawInlineNumber(screen, x, 92, "ST:", decoder.LastStoredFrameBytes());
    DrawInlineNumber(screen, x, 92, "DEC:", decoder.LastDecodedFrameBytes());

    x = 8;
    DrawInlineNumber(screen, x, 104, "PKT:", decoder.LastPacketCount());
    DrawInlineNumber(screen, x, 104, "LIT:", decoder.PacketCount(0));
    DrawInlineNumber(screen, x, 104, "REP:", decoder.PacketCount(1));
    DrawInlineNumber(screen, x, 104, "SKP:", decoder.PacketCount(2));

    x = 8;
    DrawInlineNumber(screen, x, 116, "RAW:", decoder.PacketCount(3));
    DrawInlineNumber(screen, x, 116, "MASK:", decoder.PacketCount(4));
    DrawInlineNumber(screen, x, 116, "SEEK:", decoder.LastSeekFrames());

    const uint8_t compression = decoder.LastCompression();
    const char *codec = compression == 1 ? "LZ4" :
                        compression == 2 ? "DEFLATE" : "RAW";
    screen.DrawText(8, 128, "CODEC:", UI::Colors::Gray);
    screen.DrawText(50, 128, codec, UI::Colors::White);
    screen.Present();
}

void DecoderDebugUpdate(void *opaque, const char *operation,
                        uint32_t current, uint32_t total) {
    DebugContext *context = static_cast<DebugContext *>(opaque);
    if (!context || !context->screen || !context->decoder ||
        !context->state || !context->screen->IsActive())
        return;
    context->state->phase = operation;
    context->state->workCurrent = current;
    context->state->workTotal = total;
    DrawDebugPage(*context->screen, *context->decoder, *context->state,
                  operation, current, total);
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
    bool keyLatched = false;
    while (!done) {
        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0)
            break;
        if (result == 1 && event.type == UI::EventKey) {
            if (event.keyAction == UI::KeyReleased)
                keyLatched = false;
            else if (!keyLatched &&
                     (event.keyCode == UI::KeyEXE ||
                      event.keyCode == UI::KeyBackspace ||
                      event.keyCode == UI::KeyPowerClear)) {
                keyLatched = true;
                done = true;
            }
        }
        if (result == 1 && event.type == UI::EventSystem &&
            event.systemEvent == UI::SystemActionBarEscape)
            done = true;
        if (result == 0)
            WaitMilliseconds(10);
    }
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
        unsigned used = 2;
        while (frameText[used] != 0 && used < sizeof(frameText) - 1U)
            ++used;
        frameText[used] = 0;
        screen.DrawText(90, 4, frameText, UI::Colors::Gray);
        const char *status = ended ? "END" : (paused ? "PAUSE" : "PLAY");
        screen.DrawText(screen.Width() - 42, 4, status,
                        ended ? UI::Colors::Yellow : UI::Colors::Green);
    }
    screen.Present();
}

void DrawCurrent(UI::Framebuffer &screen, const CPV::Decoder &decoder,
                 const PlaybackState &state, bool debugMode,
                 bool showControls) {
    DrawFrame(screen, decoder, debugMode ? false : showControls,
              state.paused, state.ended);
    if (debugMode) {
        DrawDebugPage(screen, decoder, state, state.phase,
                      state.workCurrent, state.workTotal);
    }
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
    state.phase = "SEEK";
    state.lastInput = delta < 0 ? "SEEK BACK" : "SEEK FORWARD";
    if (decoder.Seek(target)) {
        state.remainingMs = decoder.CurrentFrameDurationMs();
        state.redraw = true;
        state.ended = false;
    }
}

void AdvancePlaybackSlice(PlaybackState &state) {
    if (state.paused || state.ended)
        return;
    if (state.remainingMs > kWaitSliceMs)
        state.remainingMs -= kWaitSliceMs;
    else
        state.remainingMs = 0;
}

void TogglePlayback(PlaybackState &state, CPV::Decoder &decoder) {
    if (state.ended) {
        state.phase = "RESTART";
        if (decoder.Seek(0)) {
            state.ended = false;
            state.paused = false;
            state.redraw = true;
            state.remainingMs = decoder.CurrentFrameDurationMs();
            state.lastInput = "RESTART";
        }
    } else {
        state.paused = !state.paused;
        state.redraw = true;
        state.lastInput = state.paused ? "PAUSE" : "RESUME";
        state.phase = state.lastInput;
    }
}

void HandleEvent(const UI::Event &event, CPV::Decoder &decoder,
                 PlaybackState &state, int screenWidth) {
    state.lastRawType = event.rawType;
    state.lastRawAction = event.rawAction;
    state.lastKeyCode = event.keyCode;
    state.lastTouchX = event.x;
    state.lastTouchY = event.y;

    if (event.type == UI::EventSystem &&
        event.systemEvent == UI::SystemActionBarEscape) {
        state.exit = true;
        state.lastInput = "ACTION BAR ESCAPE";
        return;
    }

    if (event.type == UI::EventKey) {
        if (event.keyAction == UI::KeyReleased) {
            if (event.keyCode == state.latchedKey)
                state.latchedKey = 0;
            return;
        }
        const bool firstPress = event.keyAction == UI::KeyPressed ||
            (event.keyAction == UI::KeyHeld &&
             event.keyCode != state.latchedKey);
        if (!firstPress)
            return;
        state.latchedKey = event.keyCode;

        if (event.keyCode == UI::KeyBackspace ||
            event.keyCode == UI::KeyPowerClear) {
            state.exit = true;
            state.lastInput = "EXIT KEY";
        } else if (event.keyCode == UI::KeyEXE) {
            TogglePlayback(state, decoder);
        } else if (event.keyCode == UI::KeyLeft) {
            SeekRelative(decoder, -static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.keyCode == UI::KeyRight) {
            SeekRelative(decoder, static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.keyCode == UI::KeyUp) {
            SeekRelative(decoder, -20000, state);
        } else if (event.keyCode == UI::KeyDown) {
            SeekRelative(decoder, 20000, state);
        } else {
            state.lastInput = "OTHER KEY";
        }
        return;
    }

    if (event.type == UI::EventTouch) {
        if (event.touchAction == UI::TouchUp) {
            if (!state.touchLatched) {
                const int third = DividePositive(screenWidth, 3);
                if (event.x < third)
                    SeekRelative(decoder, -static_cast<int32_t>(kSeekStepMs), state);
                else if (event.x >= screenWidth - third)
                    SeekRelative(decoder, static_cast<int32_t>(kSeekStepMs), state);
                else
                    TogglePlayback(state, decoder);
            }
            state.touchLatched = false;
            return;
        }
        if (event.touchAction != UI::TouchDown || state.touchLatched)
            return;
        state.touchLatched = true;
        const int third = DividePositive(screenWidth, 3);
        if (event.x < third) {
            SeekRelative(decoder, -static_cast<int32_t>(kSeekStepMs), state);
        } else if (event.x >= screenWidth - third) {
            SeekRelative(decoder, static_cast<int32_t>(kSeekStepMs), state);
        } else {
            TogglePlayback(state, decoder);
        }
    }
}

void PlayVideo(UI::Application &app, const char *path,
               bool repeatPlayback, bool showControls, bool debugMode) {
    CPV::Decoder decoder;
    UI::Framebuffer screen;
    if (!screen.Begin()) {
        DrawError("DISPLAY ERROR", "LCD framebuffer unavailable");
        return;
    }

    screen.Clear(UI::Colors::Black);
    screen.Present();

    PlaybackState state;
    DebugContext debugContext = {&screen, &decoder, &state};
    if (debugMode)
        decoder.SetDebugCallback(DecoderDebugUpdate, &debugContext);

    if (!decoder.Open(path)) {
        const char *error = decoder.Error();
        screen.End();
        DrawError("CPV OPEN ERROR", error);
        return;
    }
    state.phase = "DECODE FIRST FRAME";
    if (!decoder.DecodeNext()) {
        const char *error = decoder.Error();
        screen.End();
        DrawError("CPV DECODE ERROR", error);
        return;
    }

    state.remainingMs = decoder.CurrentFrameDurationMs();
    state.phase = "PLAYBACK READY";
    state.workCurrent = 0;
    state.workTotal = 0;
    state.redraw = true;

    const char *playbackError = 0;
    while (app.IsRunning() && !state.exit) {
        if (state.redraw) {
            DrawCurrent(screen, decoder, state, debugMode, showControls);
            state.redraw = false;
        }

        if (!state.paused && !state.ended && state.remainingMs == 0) {
            if (decoder.AtEnd()) {
                if (repeatPlayback) {
                    state.phase = "REPEAT SEEK";
                    if (decoder.Seek(0)) {
                        state.redraw = true;
                        state.remainingMs = decoder.CurrentFrameDurationMs();
                    } else {
                        state.ended = true;
                        state.redraw = true;
                    }
                } else {
                    state.ended = true;
                    state.phase = "END OF VIDEO";
                    state.redraw = true;
                }
                continue;
            }
            state.phase = "DECODE NEXT";
            if (!decoder.DecodeNext()) {
                playbackError = decoder.Error();
                break;
            }
            state.redraw = true;
            state.remainingMs = decoder.CurrentFrameDurationMs();
            state.lastInput = "FRAME DECODED";
            continue;
        }

        UI::Event event;
        const int result = UI::Input::Poll(&event);
        if (result < 0) {
            state.lastInput = "INPUT ERROR";
            break;
        }
        if (result == 1) {
            HandleEvent(event, decoder, state, screen.Width());
            if (event.type == UI::EventTimer) {
                ++state.timerEventCount;
                AdvancePlaybackSlice(state);
            }

            if (state.redraw && !state.exit) {
                DrawCurrent(screen, decoder, state, debugMode, showControls);
                state.redraw = false;
            } else if (debugMode && !state.exit &&
                       event.type != UI::EventTimer) {
                // Update the small diagnostic panel without redrawing the video.
                DrawDebugPage(screen, decoder, state, state.phase,
                              state.workCurrent, state.workTotal);
            } else if (debugMode && !state.exit &&
                       (state.timerEventCount & 0x07U) == 0U) {
                DrawDebugPage(screen, decoder, state, state.phase,
                              state.workCurrent, state.workTotal);
            }
        } else {
            WaitMilliseconds(kWaitSliceMs);
            AdvancePlaybackSlice(state);
        }
    }

    screen.End();
    if (playbackError)
        DrawError("CPV DECODE ERROR", playbackError);
}

static bool g_repeatPlayback = false;
static bool g_showControls = true;
static bool g_debugMode = false;

void CPVFileSelected(UI::Application &app, const char *path) {
    PlayVideo(app, path, g_repeatPlayback, g_showControls, g_debugMode);
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
                             &g_showControls),
        UI::Setting::Boolean("debug.enabled", "Debug mode", &g_debugMode)
    };
    config.settings = settings;
    config.settingCount = sizeof(settings) / sizeof(settings[0]);

    UI::Application app(config);
    app.Run();
}
