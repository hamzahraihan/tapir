#include "tray.h"

#include "constants.h"

#include <shellapi.h>

#include <string>

namespace tapir::tray {
namespace {

constexpr UINT kTrayIconId = 1;
constexpr const wchar_t* kAnimationsLabel = L"Show/Hide animation";
constexpr const wchar_t* kSettingsLabel = L"Settings";
constexpr const wchar_t* kRestartLabel = L"Restart";
constexpr const wchar_t* kQuitLabel = L"Quit";

HWND g_owner = nullptr;
HMENU g_menu = nullptr;
bool g_animationsChecked = true;

bool appendItem(HMENU menu, Command command, const wchar_t* label, bool checked) {
    UINT flags = MF_STRING;
    if (checked) flags |= MF_CHECKED;
    return AppendMenuW(menu, flags, static_cast<UINT_PTR>(command), label) != 0;
}

bool buildMenu() {
    g_menu = CreatePopupMenu();
    if (g_menu == nullptr) return false;
    if (appendItem(g_menu, Command::Animations, kAnimationsLabel, g_animationsChecked) &&
        appendItem(g_menu, Command::Settings, kSettingsLabel, false) &&
        appendItem(g_menu, Command::Restart, kRestartLabel, false) &&
        appendItem(g_menu, Command::Quit, kQuitLabel, false)) {
        return true;
    }
    DestroyMenu(g_menu);
    g_menu = nullptr;
    return false;
}

void showMenu() {
    if (g_owner == nullptr || g_menu == nullptr) return;
    SetForegroundWindow(g_owner);
    POINT point{};
    if (!GetCursorPos(&point)) return;
    const UINT chosen = static_cast<UINT>(TrackPopupMenu(
        g_menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, 0, g_owner,
        nullptr));
    if (chosen != 0) {
        PostMessageW(g_owner, kCommandMessage, static_cast<WPARAM>(chosen), 0);
    }
    // A menu opened from a notification icon needs a posted message to dismiss
    // when the user clicks outside it.
    PostMessageW(g_owner, WM_NULL, 0, 0);
}

} // namespace

bool create(HWND owner, HICON icon) {
    if (owner == nullptr) return false;
    if (g_owner != nullptr) destroy();
    if (!buildMenu()) return false;

    NOTIFYICONDATAW data{};
    data.cbSize = static_cast<DWORD>(sizeof(data));
    data.hWnd = owner;
    data.uID = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kNotifyMessage;
    data.hIcon = icon;
    wcscpy_s(data.szTip, std::wstring(kAppName).c_str());
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        DestroyMenu(g_menu);
        g_menu = nullptr;
        return false;
    }
    g_owner = owner;
    return true;
}

void destroy() {
    if (g_owner != nullptr) {
        NOTIFYICONDATAW data{};
        data.cbSize = static_cast<DWORD>(sizeof(data));
        data.hWnd = g_owner;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        g_owner = nullptr;
    }
    if (g_menu != nullptr) {
        DestroyMenu(g_menu);
        g_menu = nullptr;
    }
}

Event handleNotify(WPARAM wParam, LPARAM lParam) {
    if (g_owner == nullptr || wParam != kTrayIconId) return Event::None;
    switch (static_cast<UINT>(lParam)) {
    case WM_LBUTTONUP:
        return Event::LeftClick;
    case WM_RBUTTONUP:
        showMenu();
        return Event::None;
    default:
        return Event::None;
    }
}

void setAnimationsChecked(bool checked) {
    g_animationsChecked = checked;
    if (g_menu != nullptr) {
        CheckMenuItem(g_menu, static_cast<UINT>(Command::Animations),
                      MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    }
}

} // namespace tapir::tray
