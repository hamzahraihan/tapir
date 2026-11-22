#pragma once

#include <windows.h>
#include "config.h"

namespace tapir {

struct AppState {
    HWND popup = nullptr;
    HWND previous = nullptr;
    int hotkeyId = 1;
    bool visible = false;
    Settings settings = defaultSettings();
};

} // namespace tapir
