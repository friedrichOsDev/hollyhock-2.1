#include <sdk/calc/calc.hpp>
#include <ui/framebuffer.hpp>
#include <ui/input.hpp>
#include "apps.hpp"
#include "bins.hpp"
#include "execs.hpp"
#include "screen.hpp"

namespace LauncherScreen {
namespace {
enum { LIST_TOP = 36, ROW_HEIGHT = 18, FOOTER_HEIGHT = 27, DETAIL_HEIGHT = 92 };

struct Program {
    const char *kind;
    const char *name;
    const char *fileName;
    const char *path;
    const char *description;
    const char *author;
    const char *version;
};

int SafeCount(int count, int maximum) {
    if (count < 0) return 0;
    return count > maximum ? maximum : count;
}
int AppCount() { return SafeCount(Apps::g_numApps, Apps::MAX_APPS); }
int BinCount() { return SafeCount(Bins::g_numApps, Bins::MAX_APPS); }
int ExecCount() { return SafeCount(Execs::g_numExecs, Execs::MAX_EXECS); }
int TotalCount() { return AppCount() + BinCount() + ExecCount(); }

int Length(const char *s) {
    int n = 0;
    if (s) while (s[n]) ++n;
    return n;
}
void Copy(char *dst, const char *src, int cap) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) while (src[i] && i < cap - 1) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}
void Append(char *dst, const char *src, int cap) {
    const int n = Length(dst);
    if (n < cap - 1) Copy(dst + n, src, cap - n);
}
void CopyLimit(char *dst, const char *src, int cap, int limit) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) while (src[i] && i < cap - 1 && i < limit) {
        dst[i] = src[i]; ++i;
    }
    dst[i] = 0;
}
void AppendNumber(char *dst, int number, int cap) {
    if (!dst || cap <= 0)
        return;
    if (number < 0)
        number = 0;

    char reversed[12];
    int count = 0;
    do {
        // Extract the remainder as the next decimal digit and carry the
        // quotient into the next iteration. This avoids SH-4 division and
        // modulo helpers while always making progress, including for 0..9.
        int quotient = 0;
        while (number >= 10) {
            number -= 10;
            ++quotient;
        }
        reversed[count++] = static_cast<char>('0' + number);
        number = quotient;
    } while (number > 0 && count < static_cast<int>(sizeof(reversed)));

    int pos = Length(dst);
    if (pos >= cap) {
        dst[cap - 1] = 0;
        return;
    }
    while (count > 0 && pos < cap - 1)
        dst[pos++] = reversed[--count];
    dst[pos] = 0;
}
int DividePositive(int value, int divisor) {
    if (value <= 0 || divisor <= 0) return 0;
    int result = 0;
    while (value >= divisor) { value -= divisor; ++result; }
    return result;
}
int PageStart(int index, int pageSize) {
    if (index <= 0 || pageSize <= 0) return 0;
    int remainder = index;
    while (remainder >= pageSize) remainder -= pageSize;
    return index - remainder;
}

void GetProgram(int index, Program *p) {
    if (!p) return;
    p->kind = "PROGRAM";
    p->name = "";
    p->fileName = "";
    p->path = "";
    p->description = "";
    p->author = "";
    p->version = "";
    const int apps = AppCount();
    const int bins = BinCount();
    if (index < 0 || index >= TotalCount()) return;

    if (index < apps) {
        const Apps::AppInfo &a = Apps::g_apps[index];
        p->kind = "HHK"; p->name = a.name[0] ? a.name : a.fileName;
        p->fileName = a.fileName; p->path = a.path;
        p->description = a.description; p->author = a.author; p->version = a.version;
        return;
    }
    index -= apps;
    if (index < bins) {
        const Bins::AppInfo &a = Bins::g_apps[index];
        p->kind = "BIN"; p->name = a.name[0] ? a.name : a.fileName;
        p->fileName = a.fileName; p->path = a.path;
        p->description = a.description; p->author = a.author; p->version = a.version;
        return;
    }
    index -= bins;
    if (index < ExecCount()) {
        const Execs::ExecInfo &e = Execs::g_execs[index];
        p->kind = "EXEC"; p->name = e.name[0] ? e.name : e.fileName;
        p->fileName = e.fileName; p->description = e.description;
        p->author = e.author; p->version = e.version;
    }
}

void DrawFooter(UI::Framebuffer &fb, int y, int slotWidth) {
    const char *labels[4] = {"PREV", "RUN", "NEXT", "CLOSE"};
    for (int i = 0; i < 4; ++i) {
        const int x = i * slotWidth;
        const int width = i == 3 ? fb.Width() - x : slotWidth;
        if (width > 4) {
            fb.DrawRect(x + 2, y, width - 4, FOOTER_HEIGHT - 3, UI::Colors::Gray);
            fb.DrawText(x + 8, y + 9, labels[i], UI::Colors::Black);
        }
    }
}

void DrawDetails(UI::Framebuffer &fb, int y, int selected, const char *status) {
    if (selected < 0 || selected >= TotalCount()) {
        fb.DrawText(8, y, "NO PROGRAMS FOUND", UI::Colors::Blue);
        fb.DrawText(8, y + 12, "Copy .HHK or .BIN files to", UI::Colors::Black);
        fb.DrawText(8, y + 22, "the FLS0 or DRV0 root folder.", UI::Colors::Black);
        if (status[0]) fb.DrawText(8, y + 54, status, UI::Colors::Red);
        return;
    }

    Program p;
    GetProgram(selected, &p);
    const int maxChars = (fb.Width() - 16) / 6;
    char name[72], metadata[144], source[144], desc1[72], desc2[72];
    CopyLimit(name, p.name, sizeof(name), maxChars);

    Copy(metadata, p.kind, sizeof(metadata));
    if (p.version && p.version[0]) {
        Append(metadata, "  VERSION ", sizeof(metadata));
        Append(metadata, p.version, sizeof(metadata));
    }
    if (p.author && p.author[0]) {
        Append(metadata, "  BY ", sizeof(metadata));
        Append(metadata, p.author, sizeof(metadata));
    }

    Copy(source, "FROM ", sizeof(source));
    if (p.kind[0] == 'E') {
        Append(source, "hhk/", sizeof(source));
        Append(source, p.fileName, sizeof(source));
    } else {
        Append(source, p.path, sizeof(source));
    }
    CopyLimit(desc1, p.description, sizeof(desc1), maxChars);
    CopyLimit(desc2, p.description ? p.description + Length(desc1) : 0,
              sizeof(desc2), maxChars);

    fb.DrawText(8, y, name, UI::Colors::Blue);
    fb.DrawText(8, y + 10, metadata, UI::Colors::Gray);
    fb.DrawText(8, y + 20, source, UI::Colors::Black);
    fb.DrawText(8, y + 32, desc1, UI::Colors::Gray);
    fb.DrawText(8, y + 42, desc2, UI::Colors::Gray);
    fb.DrawText(8, y + 54,
        status[0] ? status : "UP/DOWN SELECT  EXE RUN  CLR EXIT",
        status[0] ? UI::Colors::Red : UI::Colors::Gray);
}

void Render(UI::Framebuffer &fb, int selected, int total,
            int rows, int slotWidth, const char *status) {
    const int detailY = fb.Height() - DETAIL_HEIGHT;
    const int footerY = fb.Height() - FOOTER_HEIGHT;
    const int first = selected >= 0 ? PageStart(selected, rows) : 0;
    int visible = total - first;
    if (visible > rows) visible = rows;
    if (visible < 0) visible = 0;

    fb.Clear(UI::Colors::White);
    fb.FillRect(0, 0, fb.Width(), 28, UI::Colors::Black);
    fb.DrawText(8, 10, "HOLLYHOCK LAUNCHER", UI::Colors::White);
    char count[32];
    Copy(count, "PROGRAMS ", sizeof(count));
    AppendNumber(count, total, sizeof(count));
    fb.DrawText(fb.Width() - Length(count) * 6 - 8, 10, count, UI::Colors::White);
    fb.DrawLine(8, 30, fb.Width() - 8, 30, UI::Colors::Gray);

    if (total == 0) {
        fb.DrawText(12, LIST_TOP + 10, "No compatible programs were found.",
                    UI::Colors::Gray);
    } else {
        for (int row = 0; row < visible; ++row) {
            const int index = first + row;
            const int y = LIST_TOP + row * ROW_HEIGHT;
            Program p;
            GetProgram(index, &p);
            char label[72];
            Copy(label, p.kind, sizeof(label));
            Append(label, " | ", sizeof(label));
            Append(label, p.name, sizeof(label));
            const int maxChars = (fb.Width() - 28) / 6;
            if (maxChars >= 0 && maxChars < static_cast<int>(sizeof(label)))
                label[maxChars] = 0;

            if (index == selected) {
                fb.FillRect(6, y, fb.Width() - 12, ROW_HEIGHT - 2, UI::Colors::Blue);
                fb.DrawText(12, y + 5, ">", UI::Colors::White);
                fb.DrawText(22, y + 5, label, UI::Colors::White);
            } else {
                fb.DrawRect(6, y, fb.Width() - 12, ROW_HEIGHT - 2, UI::Colors::Gray);
                fb.DrawText(22, y + 5, label, UI::Colors::Black);
            }
        }
    }

    fb.DrawLine(8, detailY - 5, fb.Width() - 8, detailY - 5, UI::Colors::Gray);
    DrawDetails(fb, detailY, selected, status);
    DrawFooter(fb, footerY, slotWidth);
    fb.Present();
}

void LaunchSelected(UI::Framebuffer &fb, int selected,
                    char *status, int statusCap, bool &running) {
    const int apps = AppCount();
    const int bins = BinCount();
    const int total = apps + bins + ExecCount();
    if (selected < 0 || selected >= total)
        return;

    // Stop drawing before another program takes control.
    fb.End();
    if (selected < apps) {
        Apps::EntryPoint entry = Apps::RunApp(selected);
        if (entry) {
            // Restore the pre-launcher UI before transferring control. This
            // also ensures that a child application's calcInit() backs up the
            // native screen rather than the launcher's framebuffer.
            calcEnd();
            entry();
            running = false;
            return;
        }
    } else if (selected < apps + bins) {
        Bins::EntryPoint entry = Bins::RunApp(selected - apps);
        if (entry) {
            calcEnd();
            entry();
            running = false;
            return;
        }
    } else {
        Execs::EntryPoint entry = Execs::RunExec(selected - apps - bins);
        if (entry) {
            calcEnd();
            entry();
            running = false;
            return;
        }
    }

    Copy(status, "Could not load the selected program.", statusCap);
    if (!fb.Begin())
        running = false;
}
} // namespace

void Run() {
    const int total = TotalCount();
    int selected = total > 0 ? 0 : -1;
    UI::Framebuffer fb;
    if (!fb.Begin()) return;

    int rows = DividePositive(fb.Height() - DETAIL_HEIGHT - LIST_TOP - 6,
                              ROW_HEIGHT);
    if (rows < 1) rows = 1;
    int slotWidth = fb.Width() >> 2;
    if (slotWidth < 1) slotWidth = 1;

    char status[72];
    status[0] = 0;
    bool running = true;
    while (running) {
        Render(fb, selected, total, rows, slotWidth, status);
        bool redraw = false;
        UI::Event event;

        while (!redraw && running) {
            const int result = UI::Input::Poll(&event);
            if (result < 0) { running = false; break; }
            if (result == 0) continue;

            if (event.type == UI::EventSystem &&
                event.systemEvent == UI::SystemActionBarEscape) {
                running = false; break;
            }

            if (event.type == UI::EventKey && event.keyAction == UI::KeyPressed) {
                if (event.keyCode == UI::KeyUp && total > 0) {
                    selected = selected <= 0 ? total - 1 : selected - 1;
                    redraw = true;
                } else if (event.keyCode == UI::KeyDown && total > 0) {
                    ++selected;
                    if (selected >= total) selected = 0;
                    redraw = true;
                } else if (event.keyCode == UI::KeyLeft && total > 0) {
                    const int first = PageStart(selected, rows);
                    selected = first >= rows ? first - rows : 0;
                    redraw = true;
                } else if (event.keyCode == UI::KeyRight && total > 0) {
                    const int nextPage = PageStart(selected, rows) + rows;
                    selected = nextPage < total ? nextPage : total - 1;
                    redraw = true;
                } else if (event.keyCode == UI::KeyEXE) {
                    LaunchSelected(fb, selected, status, sizeof(status), running);
                    if (!running) break;
                    redraw = true;
                } else if (event.keyCode == UI::KeyPowerClear ||
                           event.keyCode == UI::KeyBackspace) {
                    running = false; break;
                }
            } else if (event.type == UI::EventTouch &&
                       event.touchAction == UI::TouchDown) {
                const int footerY = fb.Height() - FOOTER_HEIGHT;
                if (event.y >= LIST_TOP &&
                    event.y < LIST_TOP + rows * ROW_HEIGHT &&
                    event.x >= 0 && event.x < fb.Width()) {
                    const int row = DividePositive(event.y - LIST_TOP, ROW_HEIGHT);
                    const int first = selected >= 0 ? PageStart(selected, rows) : 0;
                    const int index = first + row;
                    if (row < rows && index < total) {
                        selected = index;
                        redraw = true;
                    }
                } else if (event.y >= footerY && event.y < fb.Height() &&
                           event.x >= 0 && event.x < fb.Width()) {
                    int action = DividePositive(event.x, slotWidth);
                    if (action > 3) action = 3;
                    if (action == 0 && total > 0) {
                        const int first = PageStart(selected, rows);
                        selected = first >= rows ? first - rows : 0;
                        redraw = true;
                    } else if (action == 1) {
                        LaunchSelected(fb, selected, status, sizeof(status), running);
                        if (!running) break;
                        redraw = true;
                    } else if (action == 2 && total > 0) {
                        const int nextPage = PageStart(selected, rows) + rows;
                        selected = nextPage < total ? nextPage : total - 1;
                        redraw = true;
                    } else if (action == 3) {
                        running = false; break;
                    }
                }
            }
        }
    }
    fb.End();
}
} // namespace LauncherScreen
