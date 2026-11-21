#pragma once

#include <ui/ui.hpp>

struct DemoSettings {
    bool showPreview;
    int previewBytes;
    int letterCase;
    bool showFileSize;
};

extern DemoSettings g_demoSettings;

void DemoStart(UI::Application &app);
void DemoFileSelected(UI::Application &app, const char *path);
void DemoCustomSettings(UI::Application &app);
void DemoResetSettings(UI::Application &app);
