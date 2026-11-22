#include "app.h"
#include "constants.h"
#include "hotkey.h"
#include "window.h"

#include <string>
#include <windows.h>

using tapir::kAppName;

namespace {

std::wstring instanceName(std::wstring_view suffix) {
    return std::wstring(kAppName) + L"-" + std::wstring(suffix);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, instanceName(L"show").c_str());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, instanceName(L"single-instance").c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (showEvent != nullptr) SetEvent(showEvent);
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 0;
    }

    tapir::AppState app;
    const DWORD initialDpi = GetDpiForSystem();
    if (!tapir::window::create(app, initialDpi)) {
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 1;
    }

    const auto combo = tapir::hotkey::parse(tapir::hotkey::kDefaultHotkey);
    if (!combo.has_value() ||
        !tapir::hotkey::registerHotkey(app.popup, app.hotkeyId, *combo)) {
        const std::wstring message =
            std::wstring(kAppName) +
            L": the default hotkey Alt+Space is already in use by another application. " +
            L"Close that application or change the hotkey in config.toml.";
        MessageBoxW(nullptr, message.c_str(), kAppName.data(), MB_OK | MB_ICONWARNING);
    }

    bool running = true;
    while (running) {
        const bool waitOnEvent = showEvent != nullptr;
        HANDLE waits[1] = {showEvent};
        const DWORD result = MsgWaitForMultipleObjects(
            waitOnEvent ? 1 : 0, waits, FALSE, INFINITE, QS_ALLINPUT);

        if (waitOnEvent && result == WAIT_OBJECT_0) {
            app.previous = GetForegroundWindow();
            tapir::window::show(app);
            continue;
        }

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    tapir::hotkey::unregisterHotkey(app.popup, app.hotkeyId);
    tapir::window::destroy(app);
    if (mutex != nullptr) CloseHandle(mutex);
    if (showEvent != nullptr) CloseHandle(showEvent);
    return 0;
}
