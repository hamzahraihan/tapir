#pragma once

#include <windows.h>

namespace tapir {

struct AppState {
    HWND popup = nullptr;
    HWND previous = nullptr;
    int hotkeyId = 1;
    bool visible = false;
};

} // namespace tapir
