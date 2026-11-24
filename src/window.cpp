#include "window.h"

#include "clipboard.h"
#include "constants.h"
#include "gfx.h"
#include "util/history_store.h"
#include "hotkey.h"
#include "icons.h"
#include "index.h"
#include "paste_flow.h"
#include "popup_logic.h"
#include "render.h"
#include "util/placement.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

namespace tapir::window {
namespace {

using tapir::kIndexReadyMessage;
using tapir::Settings;

bool motionAllowed(const AppState& app) noexcept;
void snapShow(AppState& app);

AppState* stateFromHwnd(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HMONITOR popupMonitor(const POINT& cursor, const MonitorSelection&) {
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
}

int desiredHeightDip(const AppState& app) noexcept {
    if (app.moreMenuOpen) {
        const int rows = app.moreCount > 0 ? static_cast<int>(app.moreCount) : 1;
        return static_cast<int>(render::kSearchRowHeightDip + 8.0f +
                                static_cast<float>(rows) * render::kMoreMenuRowHeightDip +
                                12.0f);
    }
    const bool listVisible = popup::listVisible(
        app.settings.window.compact, app.appsMode, app.text.value());
    if (!listVisible) return static_cast<int>(render::kSearchRowHeightDip);
    const int rows = static_cast<int>(app::visibleRowCount(app));
    const int shown = rows > 0 ? rows : 1; // the empty state still occupies one row
    return static_cast<int>(render::kSearchRowHeightDip + render::kSectionHeaderHeightDip +
                            static_cast<float>(shown) * render::kResultRowHeightDip +
                            render::kCommandBarHeightDip);
}

void reposition(HWND hwnd, unsigned dpi, const Settings& settings, int heightDip) {
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
        work, settings.window.widthDip, heightDip, dpi, position);

    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void draw(AppState& app) {
    if (app.gfx == nullptr) return;
    app::refreshResults(app);

    const std::wstring_view placeholder = app.moreMenuOpen ? L"Filter actions..."
        : app.appsMode ? L"Search apps..." : L"Search clipboard...";
    const wchar_t glyph = app.appsMode ? static_cast<wchar_t>(0xE721) : static_cast<wchar_t>(0xE723);
    render::PaletteView view;
    view.search.text = app.moreMenuOpen ? app.moreFilter : app.text.value();
    view.search.placeholder = placeholder;
    view.search.caret = app.moreMenuOpen ? app.moreFilter.size() : app.text.caret();
    view.search.selection = app.moreMenuOpen
        ? std::pair<std::size_t, std::size_t>{0, 0} : app.text.selection();
    view.search.focused = app.focused;
    view.search.glyph = glyph;
    view.search.ghost = app.moreMenuOpen ? L""
        : popup::ghostSuffix(app::ghostTitle(app), app.text.value());
    view.search.widthDip = static_cast<float>(app.settings.window.widthDip);
    view.search.heightDip = render::kSearchRowHeightDip;
    view.dpi = app.dpi;

    view.listVisible = !app.moreMenuOpen && popup::listVisible(
        app.settings.window.compact, app.appsMode, app.text.value());

    std::array<render::MenuRowView, kMaxMoreActions> menuRows{};
    if (app.moreMenuOpen) {
        for (std::size_t i = 0; i < app.moreCount && i < menuRows.size(); ++i) {
            menuRows[i] = render::MenuRowView{app.moreItems[i].label, i == app.moreSelected};
        }
        view.menu.rows = menuRows.data();
        view.menu.rowCount = app.moreCount;
    }

    std::array<render::ResultRowView, kMaxResults> rows{};
    const std::size_t visible = std::min(app::visibleRowCount(app), rows.size());
    for (std::size_t i = 0; i < visible; ++i) {
        const fuzzy::Result* result = app::resultAtRow(app, i);
        if (result == nullptr) continue;
        if (app.appsMode) {
            const index::App* found = app::appAtRow(app, i);
            if (found == nullptr) continue;
            rows[i].title = found->name;
            rows[i].subtitle = found->target;
            rows[i].typeLabel = L"App";
            if (view.listVisible && app.gfx != nullptr && app.gfx->iconLoader) {
                rows[i].icon = app.gfx->iconLoader->Get(found->target, app.dpi).Get();
            }
        } else {
            const clipboard::Entry* entry = app::clipboardEntryAtRow(app, i);
            if (entry == nullptr) continue;
            rows[i].title = entry->preview;
            rows[i].typeLabel = L"Text";
        }
        rows[i].matched = result->matched;
        rows[i].matchedCount = result->matchedCount;
        rows[i].selected = i == app.selected - app.scrollOffset;
        rows[i].hovered = static_cast<int>(i) == app.hoveredRow;
    }
    view.rows = rows.data();
    view.rowCount = visible;
    view.sectionHeader = !app.appsMode ? L"Recent"
                       : app.text.value().empty() ? L"Top result" : L"Results";
    view.emptyLabel = app.appsMode ? L"No results" : L"No clipboard history";
    view.showEmptyState = popup::needsEmptyState(view.listVisible, app.resultCount);
    view.commands.modeLabel = app.appsMode ? L"Apps  ·  Tab" : L"Clipboard  ·  Tab";
    view.commands.primaryKey = L"Enter";
    view.commands.primaryLabel = app.appsMode ? L" Open" : L" Paste";
    view.commands.moreKey = L"Ctrl+K";
    view.commands.moreLabel = L" More";
    view.commands.showMore = true;
    gfx::render(*app.gfx, view);
}

constexpr UINT_PTR kCompactTimerId = 2;
constexpr UINT_PTR kReloadTimerId = 3;
constexpr UINT_PTR kHistorySaveTimerId = 4;
constexpr UINT kFrameIntervalMs = 16;
constexpr UINT kReloadDebounceMs = 300;
constexpr UINT kHistorySaveDebounceMs = 500;

void flushHistory(AppState& app) {
    if (app.historySaveTimerActive && app.popup != nullptr) {
        KillTimer(app.popup, kHistorySaveTimerId);
        app.historySaveTimerActive = false;
    }
    if (app.clipboard == nullptr) return;
    history_store::Save(app.settings.privacy.encryptHistory, app.clipboard->ToRecords());
}

void scheduleHistorySave(AppState& app) {
    if (app.popup == nullptr) return;
    KillTimer(app.popup, kHistorySaveTimerId);
    if (SetTimer(app.popup, kHistorySaveTimerId, kHistorySaveDebounceMs, nullptr) == 0) {
        flushHistory(app);
    } else {
        app.historySaveTimerActive = true;
    }
}

void markHistoryChanged(AppState& app) {
    if (app.clipboard == nullptr) return;
    app.clipboardRevision = app.clipboard->revision();
    scheduleHistorySave(app);
}

void stopCompactTimer(AppState& app) {
    if (app.compactTimerActive && app.popup != nullptr) {
        KillTimer(app.popup, kCompactTimerId);
        app.compactTimerActive = false;
    }
}

int currentHeightDip(const AppState& app) noexcept {
    RECT rect{};
    if (app.popup == nullptr || !GetWindowRect(app.popup, &rect)) return 0;
    const std::int64_t px = rect.bottom - rect.top;
    return static_cast<int>((px * 96 + app.dpi / 2) / app.dpi);
}

void snapCompact(AppState& app) {
    if (!app.compactAnimation.active() && !app.compactTimerActive) return;
    stopCompactTimer(app);
    app.compactAnimation.snap();
    reposition(app.popup, app.dpi, app.settings, app.compactToHeightDip);
    draw(app);
}

void tickCompact(AppState& app) {
    if (!app.compactAnimation.active()) {
        stopCompactTimer(app);
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
    const int height = popup::interpolateHeight(
        app.compactFromHeightDip, app.compactToHeightDip,
        app.compactAnimation.progress(now));
    reposition(app.popup, app.dpi, app.settings, height);
    if (!app.compactAnimation.update(now)) {
        stopCompactTimer(app);
        reposition(app.popup, app.dpi, app.settings, app.compactToHeightDip);
        draw(app);
    }
}

void refreshMoreMenu(AppState& app) {
    if (!app.moreMenuOpen) return;
    app.moreCount = popup::filterMoreActions(app.appsMode, app.moreFilter, app.moreItems);
    if (app.moreSelected >= app.moreCount) {
        app.moreSelected = app.moreCount > 0 ? app.moreCount - 1 : 0;
    }
}

void openMoreMenu(AppState& app) {
    app.moreMenuOpen = true;
    app.moreFilter.clear();
    app.moreSelected = 0;
    refreshMoreMenu(app);
}

void onContentChanged(AppState& app) {
    refreshMoreMenu(app);
    snapShow(app);
    app::refreshResults(app);
    if (app.popup == nullptr) return;
    const int target = desiredHeightDip(app);
    const int current = currentHeightDip(app);
    if (target == current) {
        draw(app);
        return;
    }
    if (!motionAllowed(app) || app.gfx == nullptr) {
        stopCompactTimer(app);
        app.compactAnimation.snap();
        reposition(app.popup, app.dpi, app.settings, target);
        draw(app);
        return;
    }
    stopCompactTimer(app);
    app.compactFromHeightDip = current;
    app.compactToHeightDip = target;
    app.compactAnimation.reset();
    const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
    app.compactAnimation.start(now, anim::kCompactMs);
    if (SetTimer(app.popup, kCompactTimerId, kFrameIntervalMs, nullptr) != 0) {
        app.compactTimerActive = true;
    } else {
        app.compactAnimation.snap();
        reposition(app.popup, app.dpi, app.settings, target);
        draw(app);
    }
}

int rowAtPoint(const AppState& app, int xPx, int yPx) noexcept {
    const float scale = static_cast<float>(app.dpi) / 96.0f;
    const float yDip = static_cast<float>(yPx) / scale;
    const float firstRow = render::kSearchRowHeightDip + render::kSectionHeaderHeightDip;
    if (xPx < 0 || yDip < firstRow) return -1;
    const int row = static_cast<int>((yDip - firstRow) / render::kResultRowHeightDip);
    if (row < 0 || row >= static_cast<int>(app::visibleRowCount(app))) return -1;
    return row;
}

constexpr UINT_PTR kShowTimerId = 1;
constexpr float kShowRiseDip = 6.0f;

bool clientAreaAnimationsEnabled() noexcept {
    BOOL enabled = FALSE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != 0 && enabled != FALSE;
}

bool motionAllowed(const AppState& app) noexcept {
    return popup::motionAllowed(app.settings.general.animations,
                                clientAreaAnimationsEnabled());
}

void stopShowTimer(AppState& app) {
    if (app.showTimerActive && app.popup != nullptr) {
        KillTimer(app.popup, kShowTimerId);
        app.showTimerActive = false;
    }
}

float showOffsetPx(const AppState& app, float progress) noexcept {
    return kShowRiseDip * (1.0f - progress) * static_cast<float>(app.dpi) / 96.0f;
}

void setWindowTop(AppState& app, int topPx) {
    if (app.popup == nullptr) return;
    SetWindowPos(app.popup, nullptr, app.showFinalPos.x, topPx, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void snapShow(AppState& app) {
    if (!app.showAnimation.active() && !app.showTimerActive) return;
    stopShowTimer(app);
    app.showAnimation.snap();
    setWindowTop(app, app.showFinalPos.y);
}

void snapActive(AppState& app) {
    snapShow(app);
    snapCompact(app);
}

void tickShow(AppState& app) {
    if (!app.showAnimation.active() || app.gfx == nullptr) {
        stopShowTimer(app);
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
    const float progress = app.showAnimation.progress(now);
    setWindowTop(app, app.showFinalPos.y +
                          static_cast<int>(showOffsetPx(app, progress)));
    if (!app.showAnimation.update(now)) {
        stopShowTimer(app);
        setWindowTop(app, app.showFinalPos.y);
    }
}

void refreshSize(AppState& app) {
    RECT rect{};
    if (app.popup == nullptr || !GetWindowRect(app.popup, &rect)) return;
    gfx::resize(*app.gfx, app.dpi, rect.right - rect.left, rect.bottom - rect.top);
    draw(app);
}

void applySettings(AppState& app) {
    if (app.popup == nullptr) return;
    hotkey::unregisterHotkey(app.popup, app.hotkeyId);
    const auto combo = hotkey::parse(app.settings.general.hotkey);
    if (!combo || !hotkey::registerHotkey(app.popup, app.hotkeyId, *combo)) {
        const std::wstring message =
            std::wstring(kAppName) + L": the hotkey " + app.settings.general.hotkey +
            L" is already in use by another application. " +
            L"Close that application or change the hotkey in config.toml.";
        MessageBoxW(nullptr, message.c_str(), kAppName.data(), MB_OK | MB_ICONWARNING);
    }
    if (app.clipboard != nullptr) {
        app.clipboard->ApplyLimits(app.settings.history);
        scheduleHistorySave(app);
    }
    if (app.appIndex != nullptr) {
        app.appIndex->Restart(app.settings, app.popup, kIndexReadyMessage);
    }
    if (app.gfx != nullptr) gfx::applySettings(*app.gfx, app.popup, app.settings);
    snapActive(app);
    app::refreshResults(app);
    reposition(app.popup, app.dpi, app.settings, desiredHeightDip(app));
    refreshSize(app);
}

void reloadSettings(AppState& app) {
    app.configWatcher.acknowledge();
    const auto loaded = app.configWatcher.refresh();
    if (!loaded.changed) return;
    app.settings = loaded.settings;
    applySettings(app);
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

constexpr DWORD kFocusPollSliceMs = 16;
constexpr UINT kPasteKeyCount = 4;

void drainMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage(0);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

class ThreadAttach {
public:
    ThreadAttach() = default;
    ThreadAttach(const ThreadAttach&) = delete;
    ThreadAttach& operator=(const ThreadAttach&) = delete;
    ~ThreadAttach() { detach(); }

    void attach(DWORD from, DWORD to) {
        if (from == to || attached_) return;
        from_ = from;
        to_ = to;
        attached_ = AttachThreadInput(from_, to_, TRUE) != FALSE;
    }

    void detach() noexcept {
        if (!attached_) return;
        AttachThreadInput(from_, to_, FALSE);
        attached_ = false;
    }

    bool attached() const noexcept { return attached_; }

private:
    DWORD from_ = 0;
    DWORD to_ = 0;
    bool attached_ = false;
};

bool targetHasFocus(HWND target) noexcept {
    const DWORD thread = GetWindowThreadProcessId(target, nullptr);
    if (thread == 0) return false;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread, &info) || info.hwndFocus == nullptr) return false;
    return info.hwndFocus == target || IsChild(target, info.hwndFocus) != FALSE;
}

bool sendCtrlV() noexcept {
    INPUT keys[kPasteKeyCount]{};
    keys[0].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].type = INPUT_KEYBOARD;
    keys[1].ki.wVk = 'V';
    keys[2] = keys[1];
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3] = keys[0];
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(kPasteKeyCount, keys, sizeof(INPUT)) == kPasteKeyCount;
}

class PasteGuard {
public:
    explicit PasteGuard(AppState& app) : app_(app) { app_.pasteActive = true; }
    PasteGuard(const PasteGuard&) = delete;
    PasteGuard& operator=(const PasteGuard&) = delete;
    ~PasteGuard() { app_.pasteActive = false; }

private:
    AppState& app_;
};

void runPasteFlow(AppState& app, paste::Mode mode) {
    const clipboard::Entry* entry = nullptr;
    if (app.clipboard != nullptr && app.selected < app.resultCount) {
        entry = app.clipboard->EntryAt(app.results[app.selected].index);
    }
    if (entry == nullptr) {
        OutputDebugStringA("tapir: paste failed - no selected clipboard item\n");
        return;
    }

    const std::wstring text = entry->text;
    const HWND target = app.previous;
    const DWORD popupThread = GetCurrentThreadId();
    DWORD targetThread = 0;
    ThreadAttach attach;
    PasteGuard guard(app);

    paste::Flow flow(mode);
    paste::Observation observation{};
    while (!flow.done()) {
        observation = {};
        switch (flow.current()) {
        case paste::Action::HidePopup:
            hide(app);
            break;
        case paste::Action::ValidateTarget:
            observation.validTarget = target != nullptr && target != app.popup &&
                                      IsWindow(target) != FALSE &&
                                      GetWindowThreadProcessId(target, &targetThread) != 0;
            break;
        case paste::Action::RestoreDirect:
            observation.restoreSucceeded = SetForegroundWindow(target) != FALSE;
            break;
        case paste::Action::RestoreAttached:
            // Windows restricts SetForegroundWindow when the caller does not own
            // the foreground window; attaching to the target's input queue is the
            // documented workaround. The bounded focus polls below verify the
            // result instead of a one-shot check that could race focus settling.
            attach.attach(popupThread, targetThread);
            SetForegroundWindow(target);
            observation.restoreSucceeded = attach.attached();
            break;
        case paste::Action::PollFocus: {
            const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
            const std::int64_t wait =
                std::min<std::int64_t>(flow.remainingMs(now), kFocusPollSliceMs);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(wait), QS_ALLINPUT);
            drainMessages();
            observation.foregroundOnTarget = GetForegroundWindow() == target;
            observation.targetThreadFocused = targetHasFocus(target);
            break;
        }
        case paste::Action::WriteClipboard:
            observation.clipboardWritten = clipboard::WriteOwnText(text);
            break;
        case paste::Action::SendCtrlV:
            observation.keysSent = sendCtrlV();
            break;
        case paste::Action::DetachThreads:
            attach.detach();
            break;
        case paste::Action::ClearMarker:
            clipboard::clearOwnWriteMarker();
            break;
        case paste::Action::LogFailure: {
            std::string line = "tapir: paste failed - ";
            line += paste::failureText(flow.failure());
            line += "\n";
            OutputDebugStringA(line.c_str());
            break;
        }
        default:
            break;
        }
        flow.observe(observation, static_cast<std::int64_t>(GetTickCount64()));
    }
    attach.detach();
}

void activateSelected(AppState& app, bool copyOnly) {
    if (app.pasteActive) return;
    if (!app.appsMode) {
        runPasteFlow(app, copyOnly ? paste::Mode::CopyOnly : paste::Mode::Paste);
        return;
    }
    if (app::launchSelected(app)) hide(app);
}

void togglePinSelected(AppState& app) {
    if (app.clipboard == nullptr || app.selected >= app.resultCount) return;
    const std::size_t index = app.results[app.selected].index;
    const clipboard::Entry* entry = app.clipboard->EntryAt(index);
    if (entry != nullptr && app.clipboard->SetPinned(index, !entry->pinned)) {
        markHistoryChanged(app);
    }
}

void deleteSelected(AppState& app) {
    if (app.clipboard == nullptr || app.selected >= app.resultCount) return;
    if (app.clipboard->Remove(app.results[app.selected].index)) markHistoryChanged(app);
}

void closeMoreMenu(AppState& app) {
    app.moreMenuOpen = false;
    app.moreCount = 0;
}

void executeMoreAction(HWND hwnd, AppState& app, popup::MoreAction action) {
    closeMoreMenu(app);
    const index::App* selected = app.appsMode ? app::appAtResult(app, app.selected) : nullptr;
    switch (action) {
    case popup::MoreAction::Open:
        if (app::launchSelected(app)) hide(app);
        break;
    case popup::MoreAction::RunAsAdmin:
        if (selected != nullptr) {
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.lpVerb = L"runas";
            info.lpFile = selected->target.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&info) != FALSE) hide(app);
        }
        break;
    case popup::MoreAction::OpenFileLocation:
        if (selected != nullptr) {
            const std::wstring params = L"/select,\"" + selected->target + L"\"";
            const auto result = ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                              params.c_str(), nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<std::intptr_t>(result) > 32) hide(app);
        }
        break;
    case popup::MoreAction::CopyPath:
        if (selected != nullptr) copyToClipboard(hwnd, selected->target);
        break;
    case popup::MoreAction::Paste:
        activateSelected(app, false);
        break;
    case popup::MoreAction::CopyOnly:
        activateSelected(app, true);
        break;
    case popup::MoreAction::TogglePin:
        togglePinSelected(app);
        break;
    case popup::MoreAction::DeleteItem:
        deleteSelected(app);
        break;
    }
}

bool handleMoreMenu(HWND hwnd, AppState& app, WPARAM key) {
    if (!app.moreMenuOpen) return false;
    switch (key) {
    case VK_UP:
        if (app.moreCount > 0) {
            app.moreSelected = (app.moreSelected + app.moreCount - 1) % app.moreCount;
        }
        return true;
    case VK_DOWN:
        if (app.moreCount > 0) {
            app.moreSelected = (app.moreSelected + 1) % app.moreCount;
        }
        return true;
    case VK_PRIOR:
    case VK_NEXT: {
        const std::int64_t delta = key == VK_PRIOR ? -4 : 4;
        const std::int64_t next = static_cast<std::int64_t>(app.moreSelected) + delta;
        app.moreSelected = app.moreCount > 0
            ? static_cast<std::size_t>(std::clamp<std::int64_t>(
                  next, 0, static_cast<std::int64_t>(app.moreCount) - 1))
            : 0;
        return true;
    }
    case VK_BACK:
        if (!app.moreFilter.empty()) app.moreFilter.pop_back();
        return true;
    case VK_RETURN:
        if (app.moreCount > 0) {
            executeMoreAction(hwnd, app, app.moreItems[app.moreSelected].action);
        }
        return true;
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case 'A':
    case 'C':
    case 'X':
    case 'V':
    case 'P':
        return true;
    default:
        return false;
    }
}

bool handleEditing(HWND hwnd, AppState& app, UINT message, WPARAM wParam) {
    if (message == WM_CHAR) {
        const wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch >= 0x20 && ch != 0x7F) {
            if (app.moreMenuOpen) app.moreFilter.push_back(ch);
            else app.text.insert(std::wstring_view(&ch, 1));
        }
        return true;
    }
    if (message != WM_KEYDOWN) return false;

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (handleMoreMenu(hwnd, app, wParam)) return true;
    switch (wParam) {
    case VK_BACK:
        app.text.backspace();
        break;
    case VK_DELETE:
        if (app.appsMode) {
            app.text.deleteForward();
        } else if (ctrl && shift) {
            if (app.clipboard != nullptr && app.clipboard->ClearUnpinned()) {
                markHistoryChanged(app);
            }
        } else {
            deleteSelected(app);
        }
        break;
    case VK_LEFT:
        if (ctrl) app.text.moveWordLeft(shift);
        else app.text.moveLeft(shift);
        break;
    case VK_RIGHT:
        if (!ctrl && !shift && popup::tryAcceptGhost(app.text, app::ghostTitle(app))) break;
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
    case 'K':
        if (!ctrl) return false;
        if (app.moreMenuOpen) {
            closeMoreMenu(app);
        } else {
            openMoreMenu(app);
        }
        break;
    case 'P':
        if (!ctrl || app.appsMode) return false;
        togglePinSelected(app);
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
        closeMoreMenu(app);
        app.appsMode = !app.appsMode;
        app.text.clear();
        break;
    case VK_RETURN:
        switch (popup::enterAction(app.appsMode, shift)) {
        case popup::EnterAction::None:
            break;
        case popup::EnterAction::Launch:
            if (app::launchSelected(app)) hide(app);
            break;
        case popup::EnterAction::Paste:
            activateSelected(app, false);
            break;
        case popup::EnterAction::CopyOnly:
            activateSelected(app, true);
            break;
        }
        break;
    case VK_UP:
        app::moveSelection(app, -1);
        break;
    case VK_DOWN:
        app::moveSelection(app, 1);
        break;
    case VK_PRIOR:
        app::moveSelection(app, -static_cast<int>(app::visibleRowCount(app)));
        break;
    case VK_NEXT:
        app::moveSelection(app, static_cast<int>(app::visibleRowCount(app)));
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
        if (app) snapActive(*app);
        if (wParam == VK_ESCAPE) {
            if (app && app->moreMenuOpen) {
                closeMoreMenu(*app);
                onContentChanged(*app);
            } else if (app && !app->text.value().empty()) {
                app->text.clear();
                onContentChanged(*app);
            } else if (app) {
                hide(*app);
            }
            return 0;
        }
        if (app && handleEditing(hwnd, *app, message, wParam)) {
            onContentChanged(*app);
            return 0;
        }
        break;
    case WM_CHAR:
        if (app) snapActive(*app);
        if (app && handleEditing(hwnd, *app, message, wParam)) {
            onContentChanged(*app);
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        if (app) {
            const int row = rowAtPoint(*app, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (row != app->hoveredRow) {
                app->hoveredRow = row;
                draw(*app);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (app) {
            const int row = rowAtPoint(*app, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (row >= 0) {
                app->selected = app->scrollOffset + static_cast<std::size_t>(row);
                draw(*app);
                activateSelected(*app, false);
            } else {
                SetFocus(hwnd);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (app) {
            app::scrollResults(*app, -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
            onContentChanged(*app);
        }
        return 0;
    case WM_TIMER:
        if (app && wParam == kShowTimerId) tickShow(*app);
        else if (app && wParam == kCompactTimerId) tickCompact(*app);
        else if (app && wParam == kReloadTimerId) {
            KillTimer(hwnd, kReloadTimerId);
            reloadSettings(*app);
        }
        else if (app && wParam == kHistorySaveTimerId) {
            app->historySaveTimerActive = false;
            flushHistory(*app);
        }
        return 0;
    case kSettingsChangedMessage:
        if (app && app->popup != nullptr) {
            KillTimer(hwnd, kReloadTimerId);
            SetTimer(hwnd, kReloadTimerId, kReloadDebounceMs, nullptr);
        }
        return 0;
    case kIndexReadyMessage:
        if (app) onContentChanged(*app);
        return 0;
    case WM_CLIPBOARDUPDATE:
        if (app != nullptr && app->clipboard != nullptr &&
            clipboard::CaptureUpdate(app->settings.history, *app->clipboard)) {
            markHistoryChanged(*app);
            if (app->visible && !app->appsMode) onContentChanged(*app);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (app && !motionAllowed(*app)) snapCompact(*app);
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
            reposition(hwnd, dpi, app->settings, desiredHeightDip(*app));
            RECT rect{};
            if (GetWindowRect(hwnd, &rect)) app->showFinalPos = {rect.left, rect.top};
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
    ::AddClipboardFormatListener(app.popup);

    reposition(app.popup, dpi, app.settings, desiredHeightDip(app));
    RECT rect{};
    GetWindowRect(app.popup, &rect);
    app.gfx = gfx::create(app.popup, dpi, rect.right - rect.left, rect.bottom - rect.top,
                          app.settings);
    ShowWindow(app.popup, SW_HIDE);
    return true;
}

void destroy(AppState& app) {
    flushHistory(app);
    stopShowTimer(app);
    stopCompactTimer(app);
    app.showAnimation.reset();
    app.compactAnimation.reset();
    if (app.popup != nullptr) {
        ::RemoveClipboardFormatListener(app.popup);
        DestroyWindow(app.popup);
        app.popup = nullptr;
    }
    app.visible = false;
}

void show(AppState& app) {
    if (app.popup == nullptr || app.visible) return;
    app.previous = GetForegroundWindow();
    stopCompactTimer(app);
    app.compactAnimation.reset();
    app::refreshResults(app);
    reposition(app.popup, app.dpi, app.settings, desiredHeightDip(app));
    RECT rect{};
    if (GetWindowRect(app.popup, &rect)) app.showFinalPos = {rect.left, rect.top};
    if (app.gfx != nullptr && motionAllowed(app)) {
        const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
        app.showAnimation.reset();
        app.showAnimation.start(now, anim::kShowMs);
        setWindowTop(app, app.showFinalPos.y +
                              static_cast<int>(showOffsetPx(app, 0.0f)));
        if (SetTimer(app.popup, kShowTimerId, kFrameIntervalMs, nullptr) != 0) {
            app.showTimerActive = true;
        } else {
            app.showAnimation.snap();
            setWindowTop(app, app.showFinalPos.y);
        }
    }
    draw(app);
    ShowWindow(app.popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(app.popup);
    app.focused = true;
    draw(app);
    app.visible = true;
    if (app.gfx != nullptr) gfx::setVisualState(*app.gfx, 1.0f, 0.0f);
}

void hide(AppState& app) {
    if (app.popup == nullptr || !app.visible) return;
    closeMoreMenu(app);
    stopShowTimer(app);
    app.showAnimation.snap();
    setWindowTop(app, app.showFinalPos.y);
    stopCompactTimer(app);
    app.compactAnimation.reset();
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