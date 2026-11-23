#include "window.h"

#include "constants.h"
#include "gfx.h"
#include "render.h"
#include "util/placement.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <cstring>
#include <string>
#include <string_view>

namespace tapir::window {
namespace {

using tapir::Settings;

AppState* stateFromHwnd(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HMONITOR popupMonitor(const POINT& cursor, const MonitorSelection&) {
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
}

void reposition(HWND hwnd, unsigned dpi, const Settings& settings) {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = popupMonitor(cursor, settings.window.monitor);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    const placement::Rect work{
        info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom
    };

    placement::Position position = placement::Position::UpperCenter;
    switch (settings.window.position) {
    case WindowPosition::UpperCenter:
        position = placement::Position::UpperCenter;
        break;
    case WindowPosition::Center:
        position = placement::Position::Center;
        break;
    }

    const placement::Rect r = placement::compute(
        work, settings.window.widthDip, placement::kCollapsedHeightDip, dpi, position);

    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void draw(AppState& app) {
    if (app.gfx == nullptr) return;
    const std::wstring_view placeholder = app.appsMode ? L"Search apps..." : L"Search clipboard...";
    const wchar_t glyph = app.appsMode ? static_cast<wchar_t>(0xE721) : static_cast<wchar_t>(0xE723);
    render::SearchRowView view;
    view.text = app.text.value();
    view.placeholder = placeholder;
    view.caret = app.text.caret();
    view.selection = app.text.selection();
    view.focused = app.focused;
    view.glyph = glyph;
    view.widthDip = static_cast<float>(app.settings.window.widthDip);
    view.heightDip = static_cast<float>(placement::kCollapsedHeightDip);
    view.dpi = app.dpi;
    gfx::render(*app.gfx, view);
}

constexpr UINT_PTR kShowTimerId = 1;
constexpr float kShowRiseDip = 6.0f;
constexpr UINT kFrameIntervalMs = 16;

bool clientAreaAnimationsEnabled() noexcept {
    BOOL enabled = FALSE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != 0 && enabled != FALSE;
}

float showOffsetPx(const AppState& app, float progress) noexcept {
    const float dip = kShowRiseDip * (1.0f - progress);
    return dip * static_cast<float>(app.dpi) / 96.0f;
}

void stopShowTimer(AppState& app) {
    if (app.showTimerActive && app.popup != nullptr) {
        KillTimer(app.popup, kShowTimerId);
        app.showTimerActive = false;
    }
}

void snapShow(AppState& app) {
    if (!app.showAnimation.active() && !app.showTimerActive) return;
    stopShowTimer(app);
    app.showAnimation.snap();
    if (app.gfx != nullptr) gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
    draw(app);
}

bool motionAllowed(const AppState& app) noexcept {
    return app.settings.general.animations && clientAreaAnimationsEnabled();
}

void tickShow(AppState& app) {
    if (!app.showAnimation.active() || app.gfx == nullptr) {
        stopShowTimer(app);
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
    const float progress = app.showAnimation.progress(now);
    gfx::setVisualState(*app.gfx, progress, showOffsetPx(app, progress));
    draw(app);
    if (!app.showAnimation.update(now)) {
        stopShowTimer(app);
        gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
        draw(app);
    }
}

void refreshSize(AppState& app) {
    RECT rect{};
    if (app.popup == nullptr || !GetWindowRect(app.popup, &rect)) return;
    gfx::resize(*app.gfx, app.dpi, rect.right - rect.left, rect.bottom - rect.top);
    draw(app);
}

bool copyToClipboard(HWND hwnd, const std::wstring& text) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(hwnd)) {
            Sleep(30);
            continue;
        }
        EmptyClipboard();
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr) {
            CloseClipboard();
            return false;
        }
        wchar_t* dest = static_cast<wchar_t*>(GlobalLock(memory));
        if (dest != nullptr) {
            memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(memory);
        }
        SetClipboardData(CF_UNICODETEXT, memory);
        CloseClipboard();
        return true;
    }
    return false;
}

std::wstring pasteFromClipboard(HWND hwnd) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(hwnd)) {
            Sleep(30);
            continue;
        }
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        std::wstring result;
        if (data != nullptr) {
            const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(data));
            if (src != nullptr) {
                result = src;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        return result;
    }
    return {};
}

bool handleEditing(HWND hwnd, AppState& app, UINT message, WPARAM wParam) {
    if (message == WM_CHAR) {
        const wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch >= 0x20 && ch != 0x7F) {
            app.text.insert(std::wstring_view(&ch, 1));
        }
        return true;
    }
    if (message != WM_KEYDOWN) return false;

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (wParam) {
    case VK_BACK:
        app.text.backspace();
        break;
    case VK_DELETE:
        app.text.deleteForward();
        break;
    case VK_LEFT:
        if (ctrl) app.text.moveWordLeft(shift);
        else app.text.moveLeft(shift);
        break;
    case VK_RIGHT:
        if (ctrl) app.text.moveWordRight(shift);
        else app.text.moveRight(shift);
        break;
    case VK_HOME:
        app.text.moveHome(shift);
        break;
    case VK_END:
        app.text.moveEnd(shift);
        break;
    case 'A':
        if (ctrl) app.text.selectAll();
        else return false;
        break;
    case 'C':
        if (ctrl) copyToClipboard(hwnd, app.text.selectedText());
        else return false;
        break;
    case 'X':
        if (ctrl) {
            copyToClipboard(hwnd, app.text.selectedText());
            app.text.replace(L"");
        } else return false;
        break;
    case 'V':
        if (ctrl) app.text.replace(pasteFromClipboard(hwnd));
        else return false;
        break;
    case VK_TAB:
        app.appsMode = !app.appsMode;
        app.text.clear();
        break;
    default:
        return false;
    }
    return true;
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
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (app) draw(*app);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_HOTKEY:
        if (app) toggle(*app);
        return 0;
    case WM_KEYDOWN:
        if (app) snapShow(*app);
        if (wParam == VK_ESCAPE) {
            if (app) hide(*app);
            return 0;
        }
        if (app && handleEditing(hwnd, *app, message, wParam)) {
            draw(*app);
            return 0;
        }
        break;
    case WM_CHAR:
        if (app) snapShow(*app);
        if (app && handleEditing(hwnd, *app, message, wParam)) {
            draw(*app);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kShowTimerId && app) tickShow(*app);
        return 0;
    case WM_SETTINGCHANGE:
        if (app && !motionAllowed(*app)) snapShow(*app);
        return 0;
    case WM_SETFOCUS:
        if (app) { app->focused = true; draw(*app); }
        return 0;
    case WM_KILLFOCUS:
        if (app) { app->focused = false; draw(*app); }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (app) hide(*app);
        }
        return 0;
    case WM_DPICHANGED: {
        const unsigned dpi = static_cast<unsigned>(LOWORD(wParam));
        if (app) {
            app->dpi = dpi;
            reposition(hwnd, dpi, app->settings);
            refreshSize(*app);
        }
        return 0;
    }
    case WM_SIZE:
        if (app && app->gfx != nullptr) refreshSize(*app);
        return 0;
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
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.lpszClassName = kAppName.data();
        wc.hbrBackground = nullptr;
        if (RegisterClassW(&wc) == 0) return false;
        registered = true;
    }

    app.dpi = dpi;
    app.popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
        kAppName.data(), kAppName.data(),
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandleW(nullptr), &app);
    if (app.popup == nullptr) return false;

    reposition(app.popup, dpi, app.settings);
    RECT rect{};
    GetWindowRect(app.popup, &rect);
    app.gfx = gfx::create(app.popup, dpi, rect.right - rect.left, rect.bottom - rect.top,
                          app.settings);
    ShowWindow(app.popup, SW_HIDE);
    return true;
}

void destroy(AppState& app) {
    stopShowTimer(app);
    app.showAnimation.reset();
    if (app.popup != nullptr) {
        DestroyWindow(app.popup);
        app.popup = nullptr;
    }
    app.visible = false;
}

void show(AppState& app) {
    if (app.popup == nullptr || app.visible) return;
    app.previous = GetForegroundWindow();
    draw(app);
    ShowWindow(app.popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(app.popup);
    app.focused = true;
    draw(app);
    app.visible = true;

    app.showAnimation.reset();
    if (app.gfx != nullptr && motionAllowed(app)) {
        const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
        app.showAnimation.start(now, anim::kShowMs);
        gfx::setVisualState(*app.gfx, 0.0f, showOffsetPx(app, 0.0f));
        if (SetTimer(app.popup, kShowTimerId, kFrameIntervalMs, nullptr) != 0) {
            app.showTimerActive = true;
        } else {
            app.showAnimation.snap();
            gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
        }
    } else if (app.gfx != nullptr) {
        gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
    }
}

void hide(AppState& app) {
    if (app.popup == nullptr || !app.visible) return;
    stopShowTimer(app);
    app.showAnimation.snap();
    if (app.gfx != nullptr) gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
    if (app.gfx != nullptr) gfx::trim(*app.gfx);
    ShowWindow(app.popup, SW_HIDE);
    app.focused = false;
    app.visible = false;
}

void toggle(AppState& app) {
    if (app.visible) hide(app);
    else show(app);
}

} // namespace tapir::window