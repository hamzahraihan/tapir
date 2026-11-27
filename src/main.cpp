#include "app.h"
#include "clipboard.h"
#include "constants.h"
#include "util/history_store.h"
#include "util/startup.h"
#include "hotkey.h"
#include "index.h"
#include "window.h"

#include <string>
#include <windows.h>
#include <winrt/base.h>

using tapir::kAppName;

namespace {

std::wstring instanceName(std::wstring_view suffix) {
    return std::wstring(kAppName) + L"-" + std::wstring(suffix);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, instanceName(L"show").c_str());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, instanceName(L"single-instance").c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (showEvent != nullptr) SetEvent(showEvent);
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 0;
    }

    tapir::AppState app;
    app.configWatcher.start(app.settings);
    app.settings = app.configWatcher.settings();
    tapir::util::startup::applyFromSettings(app.settings.general.startWithWindows);
    app.appIndex = std::make_unique<tapir::index::Index>();
    app.appIndex->LoadCached();
    app.clipboard = std::make_unique<tapir::clipboard::History>(app.settings.history);
    app.clipboard->Load(tapir::history_store::Load());
    app.clipboardRevision = app.clipboard->revision();
    app.rankedQuery.reserve(256);
    const DWORD initialDpi = GetDpiForSystem();
    if (!tapir::window::create(app, initialDpi)) {
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 1;
    }
    app.appIndex->Start(app.settings, app.popup, tapir::kIndexReadyMessage);

    const std::wstring& hotkeyText = app.settings.general.hotkey;
    const auto combo = tapir::hotkey::parse(hotkeyText);
    if (!combo.has_value() ||
        !tapir::hotkey::registerHotkey(app.popup, app.hotkeyId, *combo)) {
        const std::wstring message =
            std::wstring(kAppName) + L": the hotkey " + hotkeyText +
            L" is already in use by another application. " +
            L"Close that application or change the hotkey in config.toml.";
        MessageBoxW(nullptr, message.c_str(), kAppName.data(), MB_OK | MB_ICONWARNING);
    }

    bool running = true;
    while (running) {
        HANDLE waits[2] = {nullptr, nullptr};
        DWORD count = 0;
        if (showEvent != nullptr) waits[count++] = showEvent;
        if (app.configWatcher.handle() != nullptr) waits[count++] = app.configWatcher.handle();
        const DWORD result = MsgWaitForMultipleObjects(count, waits, FALSE, INFINITE, QS_ALLINPUT);

        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) {
            HANDLE signalled = waits[result - WAIT_OBJECT_0];
            if (signalled == app.configWatcher.handle()) {
                // Re-arm before debouncing: a still-signaled change notification
                // would keep resetting the reload timer forever.
                app.configWatcher.acknowledge();
                PostMessageW(app.popup, tapir::kSettingsChangedMessage, 0, 0);
            } else {
                app.previous = GetForegroundWindow();
                tapir::window::show(app);
            }
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

    if (app.restartRequested) {
        wchar_t executable[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (length != 0 && length < MAX_PATH) {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (CreateProcessW(executable, nullptr, nullptr, nullptr, FALSE, 0,
                               nullptr, nullptr, &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            } else {
                const std::wstring message =
                    std::wstring(kAppName) + L" could not restart itself.";
                MessageBoxW(nullptr, message.c_str(), kAppName.data(),
                            MB_OK | MB_ICONERROR);
            }
        }
    }
    return 0;
}
