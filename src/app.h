#pragma once

#include <windows.h>
#include <memory>
#include "config.h"
#include "gfx.h"
#include "input.h"

namespace tapir {

struct AppState {
    HWND popup = nullptr;
    HWND previous = nullptr;
    int hotkeyId = 1;
    bool visible = false;
    Settings settings = defaultSettings();
    std::unique_ptr<gfx::Context> gfx;
    input::TextBuffer text;
    bool appsMode = true;
    bool focused = false;
    unsigned dpi = 96;
};

} // namespace tapir
