#include "window.h"

#include "constants.h"
#include "util/placement.h"

#include <shellscalingapi.h>
#include <windowsx.h>

namespace tapir::window {
namespace {

AppState* stateFromHwnd(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void reposition(HWND hwnd, unsigned dpi) {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    const placement::Rect work{
        info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom
    };
    const placement::Rect r = placement::compute(
        work, placement::kDefaultWidthDip, placement::kCollapsedHeightDip, dpi,
        placement::Position::UpperCenter);

    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* app = stateFromHwnd(hwnd);

    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        app = stateFromHwnd(hwnd);
        if (app) app->popup = hwnd;
        return 0;
    }
    case WM_HOTKEY:
        if (app) toggle(*app);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (app) hide(*app);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (app) hide(*app);
        }
        return 0;
    case WM_DPICHANGED: {
        const unsigned dpi = static_cast<unsigned>(LOWORD(wParam));
        reposition(hwnd, dpi);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

bool create(AppState& app, unsigned dpi) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kAppName.data();
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassW(&wc) == 0) return false;
        registered = true;
    }

    app.popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kAppName.data(), kAppName.data(),
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandleW(nullptr), &app);
    if (app.popup == nullptr) return false;

    reposition(app.popup, dpi);
    ShowWindow(app.popup, SW_HIDE);
    return true;
}

void destroy(AppState& app) {
    if (app.popup != nullptr) {
        DestroyWindow(app.popup);
        app.popup = nullptr;
    }
    app.visible = false;
}

void show(AppState& app) {
    if (app.popup == nullptr || app.visible) return;
    app.previous = GetForegroundWindow();
    ShowWindow(app.popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(app.popup);
    app.visible = true;
}

void hide(AppState& app) {
    if (app.popup == nullptr || !app.visible) return;
    ShowWindow(app.popup, SW_HIDE);
    app.visible = false;
}

void toggle(AppState& app) {
    if (app.visible) hide(app);
    else show(app);
}

} // namespace tapir::window
