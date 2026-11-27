#include "window.h"

#include "clipboard.h"
#include "config_writer.h"
#include "constants.h"
#include "diagnostics.h"
#include "gfx.h"
#include "settings_ui.h"
#include "tray.h"
#include "util/history_store.h"
#include "hotkey.h"
#include "icons.h"
#include "index.h"
#include "paste_flow.h"
#include "popup_logic.h"
#include "render.h"
#include "util/details_layout.h"
#include "util/latency_probe.h"
#include "util/monitor_policy.h"
#include "util/placement.h"
#include "util/startup.h"

#include <psapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tapir::window {
namespace {

using tapir::kIndexReadyMessage;
using tapir::kProfileMessage;

bool motionAllowed(const AppState& app) noexcept;
void snapShow(AppState& app);

AppState* stateFromHwnd(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

std::filesystem::path probeOutputPath() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetEnvironmentVariableW(
        L"TAPIR_PROBE_OUT", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data());
}

bool cursorOverTaskbar() {
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar == nullptr) return false;
    RECT rect{};
    POINT cursor{};
    return GetWindowRect(taskbar, &rect) && GetCursorPos(&cursor) && PtInRect(&rect, cursor);
}

struct MonitorEntry {
    HMONITOR handle = nullptr;
    monitor_policy::Descriptor descriptor{};
};

std::vector<MonitorEntry> g_monitors;
std::vector<monitor_policy::Descriptor> g_descriptors;
std::size_t g_primaryIndex = 0;

unsigned dpiForMonitor(HMONITOR handle) noexcept {
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) &&
        dpiX != 0) {
        return static_cast<unsigned>(dpiX);
    }
    return 96;
}

void rebuildDescriptors() {
    g_descriptors.clear();
    for (const auto& entry : g_monitors) g_descriptors.push_back(entry.descriptor);
}

void overrideDpi(HMONITOR handle, unsigned dpi) noexcept {
    for (auto& entry : g_monitors) {
        if (entry.handle == handle) entry.descriptor.dpi = dpi;
    }
    rebuildDescriptors();
}

BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM lParam) {
    auto* entries = reinterpret_cast<std::vector<MonitorEntry>*>(lParam);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) return TRUE;

    const unsigned dpi = dpiForMonitor(handle);

    MonitorEntry entry;
    entry.handle = handle;
    entry.descriptor.work = {info.rcWork.left, info.rcWork.top,
                             info.rcWork.right, info.rcWork.bottom};
    entry.descriptor.dpi = dpi;
    entry.descriptor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    entries->push_back(entry);
    return TRUE;
}

void refreshMonitors() {
    g_monitors.clear();
    g_descriptors.clear();
    g_primaryIndex = 0;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor,
                        reinterpret_cast<LPARAM>(&g_monitors));
    for (std::size_t i = 0; i < g_monitors.size(); ++i) {
        if (g_monitors[i].descriptor.primary) g_primaryIndex = i;
    }
    rebuildDescriptors();
}

void ensureMonitors() {
    if (g_monitors.empty()) refreshMonitors();
}

bool detailsShown(const AppState& app) noexcept {
    return !app.appsMode && app.settings.window.detailsPane &&
           app.selected < app.resultCount;
}

details_layout::Widths popupWidths(const AppState& app) noexcept {
    const bool show = detailsShown(app);
    return details_layout::partition(
        app.settings.window.widthDip + (show ? details_layout::kWidthDip : 0), show);
}

placement::Rect activeWindowRect(const AppState& app) noexcept {
    HWND target = app.previous;
    if (target == nullptr || IsWindow(target) == FALSE) target = GetForegroundWindow();
    RECT rect{};
    if (target != nullptr && GetWindowRect(target, &rect)) {
        return {rect.left, rect.top, rect.right, rect.bottom};
    }
    return {};
}

struct MonitorChoice {
    placement::Rect work{};
    unsigned dpi = 96;
    bool valid = false;
};

MonitorChoice selectPopupMonitor(const AppState& app) {
    ensureMonitors();

    POINT cursor{};
    GetCursorPos(&cursor);

    if (!g_monitors.empty()) {
        const std::size_t chosen = monitor_policy::select(
            app.settings.window.monitor,
            monitor_policy::Point{static_cast<int>(cursor.x), static_cast<int>(cursor.y)},
            activeWindowRect(app), g_descriptors, g_primaryIndex);
        const std::size_t index = chosen < g_monitors.size()
            ? chosen
            : (g_primaryIndex < g_monitors.size() ? g_primaryIndex : 0);
        return {g_monitors[index].descriptor.work, g_monitors[index].descriptor.dpi, true};
    }

    HMONITOR handle = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) return {};
    unsigned dpi = 96;
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
        dpi = static_cast<unsigned>(dpiX);
    }
    return {{info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom}, dpi, true};
}

MonitorChoice syncDpiToMonitor(AppState& app) {
    const MonitorChoice choice = selectPopupMonitor(app);
    if (choice.valid) app.dpi = choice.dpi;
    return choice;
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

int maxHeightDip(const AppState& app) noexcept {
    const int rows = std::max(1, app.settings.window.maxVisibleRows);
    const int listHeight = static_cast<int>(
        render::kSearchRowHeightDip + render::kSectionHeaderHeightDip +
        static_cast<float>(rows) * render::kResultRowHeightDip +
        render::kCommandBarHeightDip);
    const int menuHeight = static_cast<int>(
        render::kSearchRowHeightDip + 8.0f +
        static_cast<float>(kMaxMoreActions) * render::kMoreMenuRowHeightDip + 12.0f);
    return std::max(listHeight, menuHeight);
}

void reposition(AppState& app, int heightDip) {
    HWND hwnd = app.popup;
    if (hwnd == nullptr) return;

    const MonitorChoice choice = syncDpiToMonitor(app);
    if (!choice.valid) return;

    placement::Position position = placement::Position::UpperCenter;
    switch (app.settings.window.position) {
    case WindowPosition::UpperCenter:
        position = placement::Position::UpperCenter;
        break;
    case WindowPosition::Center:
        position = placement::Position::Center;
        break;
    }

    const details_layout::Widths widths = popupWidths(app);
    const placement::Rect r = placement::compute(
        choice.work, widths.listDip + widths.paneDip, heightDip, choice.dpi, position);

    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (app.backdrop) {
        const RECT backdropRect{r.left, r.top, r.right, r.bottom};
        backdrop::position(*app.backdrop, backdropRect);
    }
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
    const details_layout::Widths widths = popupWidths(app);
    view.search.widthDip = static_cast<float>(widths.listDip);
    view.search.heightDip = render::kSearchRowHeightDip;
    view.dpi = app.dpi;

    view.details.show = widths.paneDip > 0;
    if (view.details.show) {
        const clipboard::Entry* entry = app::clipboardEntryAtRow(
            app, static_cast<int>(app.selected - app.scrollOffset));
        if (entry != app.detailsEntry) {
            app.detailsEntry = entry;
            app.detailsScrollDip = 0;
        }
        view.details.text = entry != nullptr ? entry->text : std::wstring_view{};
        view.details.label = L"Details";
        view.details.emptyLabel = L"(empty)";
        view.details.scrollDip = app.detailsScrollDip;
        view.details.widthDip = static_cast<float>(widths.paneDip);
        view.details.heightDip = static_cast<float>(desiredHeightDip(app));
    }

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
    view.emptyLabel = app.appsMode
                          ? (app.appIndex != nullptr && !app.appIndex->Ready()
                                 ? L"Loading apps..."
                                 : L"No results")
                          : L"No clipboard history";
    view.showEmptyState = popup::needsEmptyState(view.listVisible, app.resultCount);
    view.search.hint = app.moreMenuOpen ? L"Actions"
                       : app.appsMode ? L"Apps" : L"Clipboard";
    view.search.hintKey = L"Tab";
    view.commands.modeLabel = L"";
    view.commands.filterGlyph = static_cast<wchar_t>(0xE71C);
    view.commands.primaryKey = L"Enter";
    view.commands.primaryLabel = app.appsMode ? L" Open" : L" Paste";
    view.commands.moreKey = L"Ctrl+K";
    view.commands.moreLabel = L" More";
    view.commands.showMore = true;
    gfx::render(*app.gfx, view);
    app.probe.finishUi();
}

constexpr UINT_PTR kCompactTimerId = 2;
constexpr UINT_PTR kReloadTimerId = 3;
constexpr UINT_PTR kHistorySaveTimerId = 4;
constexpr UINT_PTR kTrimTimerId = 5;
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

int currentWidthDip(const AppState& app) noexcept {
    RECT rect{};
    if (app.popup == nullptr || !GetWindowRect(app.popup, &rect)) return 0;
    const std::int64_t px = rect.right - rect.left;
    return static_cast<int>((px * 96 + app.dpi / 2) / app.dpi);
}

void snapCompact(AppState& app) {
    if (!app.compactAnimation.active() && !app.compactTimerActive) return;
    stopCompactTimer(app);
    app.compactAnimation.snap();
    reposition(app, app.compactToHeightDip);
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
    reposition(app, height);
    if (!app.compactAnimation.update(now)) {
        stopCompactTimer(app);
        reposition(app, app.compactToHeightDip);
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
    app.probe.markInput();
    app::refreshResults(app);
    if (app.popup == nullptr) return;
    const int target = desiredHeightDip(app);
    const int current = currentHeightDip(app);
    if (target == current) {
        const details_layout::Widths widths = popupWidths(app);
        if (currentWidthDip(app) != widths.listDip + widths.paneDip) reposition(app, target);
        draw(app);
        return;
    }
    if (!motionAllowed(app) || app.gfx == nullptr) {
        stopCompactTimer(app);
        app.compactAnimation.snap();
        reposition(app, target);
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
        // Render the target content once up front. The swap chain is max-sized
        // and clipped by the window, so resize frames only need to move it.
        draw(app);
    } else {
        app.compactAnimation.snap();
        reposition(app, target);
        draw(app);
    }
}

int rowAtPoint(const AppState& app, int xPx, int yPx) noexcept {
    const float scale = static_cast<float>(app.dpi) / 96.0f;
    const float xDip = static_cast<float>(xPx) / scale;
    const float yDip = static_cast<float>(yPx) / scale;
    const float firstRow = render::kSearchRowHeightDip + render::kSectionHeaderHeightDip;
    if (xPx < 0 || yDip < firstRow) return -1;
    const details_layout::Widths widths = popupWidths(app);
    if (xDip >= static_cast<float>(widths.listDip)) return -1;
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

void refreshSize(AppState& app, bool drawContent = true) {
    RECT rect{};
    if (app.popup == nullptr || !GetWindowRect(app.popup, &rect)) return;
    // Height changes only clip the max-sized swap chain; resizing it here
    // leaves the new area unpainted until the compositor catches up.
    const int widthPx = rect.right - rect.left;
    if (app.gfx->widthPx != widthPx) {
        gfx::resize(*app.gfx, app.dpi, widthPx, app.gfx->heightPx);
        drawContent = true;
    }
    if (drawContent) draw(app);
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
    tapir::util::startup::applyFromSettings(app.settings.general.startWithWindows);
    if (app.clipboard != nullptr) {
        app.clipboard->ApplyLimits(app.settings.history);
        scheduleHistorySave(app);
    }
    if (app.appIndex != nullptr) {
        app.appIndex->Restart(app.settings, app.popup, kIndexReadyMessage);
    }
    if (app.gfx != nullptr)     gfx::applySettings(*app.gfx, app.popup, app.settings);
    if (app.backdrop) {
        backdrop::setVisible(*app.backdrop,
                             app.visible && app.settings.theme.backdrop == Backdrop::Acrylic);
    }
    snapActive(app);
    app::refreshResults(app);
    reposition(app, desiredHeightDip(app));
    refreshSize(app);
}

void reloadSettings(AppState& app) {
    const auto loaded = app.configWatcher.refresh();
    if (!loaded.changed) return;
    app.settings = loaded.settings;
    applySettings(app);
    tray::setAnimationsChecked(app.settings.general.animations);
}

std::string readConfigText(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return config::defaultText();
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool writeConfigText(const std::wstring& path, const std::string& text) {
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

AppState* g_settingsOwner = nullptr;

void commitConfig(AppState& app, std::span<const config_writer::Value> values) {
    const std::wstring path = util::config::configPath();
    const std::string original = readConfigText(path);
    const std::string patched = config_writer::patch(original, values);
    const auto loaded = config::parse(patched);
    if (!loaded.valid) {
        diagnostics::append(util::config::logPath(), "refusing invalid config write");
        return;
    }
    app.selfWritePending = true;
    app.configWatcher.acknowledge();
    if (!writeConfigText(path, patched)) {
        app.selfWritePending = false;
        diagnostics::append(util::config::logPath(), "config write failed");
        return;
    }
    if (loaded.settings.privacy.encryptHistory != app.settings.privacy.encryptHistory) {
        const bool backedUp = CopyFileW(util::config::historyPath().c_str(),
                                        util::config::historyBackupPath().c_str(), FALSE) != FALSE;
        const bool converted = backedUp &&
            history_store::Save(loaded.settings.privacy.encryptHistory, history_store::Load());
        if (!converted) {
            writeConfigText(path, original);
            app.configWatcher.acknowledge();
            app.selfWritePending = true;
            diagnostics::append(util::config::logPath(),
                                "history encryption conversion failed; config reverted");
            return;
        }
    }
    app.settings = loaded.settings;
    applySettings(app);
    tray::setAnimationsChecked(app.settings.general.animations);
}

void onSettingsDone(const settings::Result& result) {
    AppState* app = g_settingsOwner;
    if (app == nullptr) return;
    if (app->backdrop) {
        backdrop::setVisible(*app->backdrop,
                             app->visible && app->settings.theme.backdrop == Backdrop::Acrylic);
    }
    if (!result.ok) return;
    commitConfig(*app, result.values);
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
        if (app) app->probe.begin(static_cast<unsigned>(app->text.value().size()));
        if (app) snapActive(*app);
        if (app && handleEditing(hwnd, *app, message, wParam)) {
            onContentChanged(*app);
            if (app) app->probe.endKeystroke();
            return 0;
        }
        if (app) app->probe.endKeystroke();
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
            const int notches = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            const details_layout::Widths widths = popupWidths(*app);
            RECT rect{};
            const bool overPane = widths.paneDip > 0 && GetWindowRect(hwnd, &rect) &&
                (GET_X_LPARAM(lParam) - rect.left) * 96 >=
                    widths.listDip * static_cast<int>(app->dpi);
            if (overPane) {
                const float next = app->detailsScrollDip +
                    static_cast<float>(notches) * 48.0f;
                app->detailsScrollDip = std::max(0.0f, next);
                draw(*app);
            } else {
                app::scrollResults(*app, notches);
                onContentChanged(*app);
            }
        }
        return 0;
    case WM_TIMER:
        if (app && wParam == kShowTimerId) tickShow(*app);
        else if (app && wParam == kCompactTimerId) tickCompact(*app);
        else if (app && wParam == kReloadTimerId) {
            KillTimer(hwnd, kReloadTimerId);
            if (app->selfWritePending) {
                app->selfWritePending = false;
                return 0;
            }
            reloadSettings(*app);
        }
        else if (app && wParam == kHistorySaveTimerId) {
            app->historySaveTimerActive = false;
            flushHistory(*app);
        }
        else if (wParam == kTrimTimerId) {
            EmptyWorkingSet(GetCurrentProcess());
        }
        return 0;
    case kSettingsChangedMessage:
        if (app && app->popup != nullptr) {
            KillTimer(hwnd, kReloadTimerId);
            SetTimer(hwnd, kReloadTimerId, kReloadDebounceMs, nullptr);
        }
        return 0;
    case kIndexReadyMessage:
        if (app) {
            app->probeIndexReady = true;
            onContentChanged(*app);
        }
        return 0;
    case tray::kNotifyMessage:
        if (app) {
            const tray::Event event = tray::handleNotify(wParam, lParam);
            if (event == tray::Event::LeftClick) {
                if (app->visible) {
                    hide(*app);
                } else if (GetTickCount64() - app->deactivationTick >= 300) {
                    show(*app);
                }
                app->deactivationTick = 0;
            }
        }
        return 0;
    case tray::kCommandMessage:
        if (app) {
            switch (static_cast<tray::Command>(wParam)) {
            case tray::Command::Animations: {
                const bool next = !app->settings.general.animations;
                const std::array<config_writer::Value, 1> values{{
                    {"general.animations", next ? "true" : "false"}}};
                commitConfig(*app, values);
                break;
            }
            case tray::Command::Settings:
    g_settingsOwner = app;
    if (app->backdrop) backdrop::setVisible(*app->backdrop, false);
    settings::open(app->popup, app->settings, &onSettingsDone);
                break;
            case tray::Command::Restart:
                app->restartRequested = true;
                PostQuitMessage(0);
                break;
            case tray::Command::Quit:
                PostQuitMessage(0);
                break;
            }
        }
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
            if (app) {
                if (app->visible && cursorOverTaskbar()) {
                    app->deactivationTick = GetTickCount64();
                }
                hide(*app);
            }
        }
        return 0;
    case WM_DISPLAYCHANGE:
        refreshMonitors();
        if (app && app->visible) {
            reposition(*app, desiredHeightDip(*app));
            refreshSize(*app);
        }
        return 0;
    case WM_DPICHANGED: {
        const unsigned dpi = static_cast<unsigned>(LOWORD(wParam));
        if (app) {
            // A scale change does not necessarily send WM_DISPLAYCHANGE, so the
            // cached monitor DPIs must be refreshed before placement uses them.
            refreshMonitors();
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                const POINT point{suggested->left, suggested->top};
                overrideDpi(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), dpi);
            }
            app->dpi = dpi;
            reposition(*app, desiredHeightDip(*app));
            RECT rect{};
            if (GetWindowRect(hwnd, &rect)) app->showFinalPos = {rect.left, rect.top};
            refreshSize(*app);
        }
        return 0;
    }
    case WM_SIZE:
        if (app && app->gfx != nullptr) refreshSize(*app, !app->compactTimerActive);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case icons::kIconReadyMessage:
        // A background worker finished shell icon extraction; convert and
        // cache the HBITMAPs on the UI thread, then repaint the rows.
        if (app && app->gfx && app->gfx->iconLoader) {
            app->gfx->iconLoader->DrainReady();
            draw(*app);
        }
        return 0;
    case kProfileMessage: {
        if (app == nullptr) return 0;
        // All file I/O stays in this handler, outside the WM_CHAR timed path.
        if (wParam == 1) {
            app->probe.enable(true);
            app->probe.reset();
            return 0;
        }
        if (wParam == 4) {
            app->probe.reset();
            return 0;
        }
        const std::filesystem::path out = probeOutputPath();
        if (out.empty()) return 0;
        if (wParam == 2) {
            std::ofstream dump(out, std::ios::binary | std::ios::trunc);
            if (dump) {
                dump << "queryLen\tpublishedQueryLen\tentries\tresults\tioCalls"
                        "\tinputUs\tmatchDoneUs\tsortDoneUs\tuiDoneUs\tvalid\n";
                for (const auto& sample : app->probe.samples) {
                    if (!sample.valid) continue;
                    dump << sample.queryLen << '\t' << sample.publishedQueryLen << '\t'
                         << sample.entries << '\t' << sample.results << '\t'
                         << sample.ioCalls << '\t' << sample.inputUs << '\t'
                         << sample.matchDoneUs << '\t' << sample.sortDoneUs << '\t'
                         << sample.uiDoneUs << '\t' << 1 << '\n';
                }
            }
            app->probe.enable(false);
            return 0;
        }
        if (wParam == 3) {
            const std::filesystem::path status(out.wstring() + L".status");
            std::ofstream dump(status, std::ios::binary | std::ios::trunc);
            if (dump) {
                dump << "indexReady=" << (app->probeIndexReady ? 1 : 0) << '\n'
                     << "indexEntries="
                     << (app->snapshot != nullptr ? app->snapshot->entries.size() : 0)
                     << '\n'
                     << "results=" << app->resultCount << '\n'
                     << "publishedQueryLen=" << app->text.value().size() << '\n'
                     << "ioCalls=" << g_probeIoCalls.load(std::memory_order_relaxed)
                     << '\n';
            }
            return 0;
        }
        return 0;
    }
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
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr),
                             MAKEINTRESOURCEW(kIconResourceId));
        wc.lpszClassName = kAppName.data();
        wc.hbrBackground = nullptr;
        if (RegisterClassW(&wc) == 0) return false;
        registered = true;
    }

    refreshMonitors();
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR cursorMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    UINT cursorDpiX = 0;
    UINT cursorDpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(cursorMonitor, MDT_EFFECTIVE_DPI, &cursorDpiX, &cursorDpiY)) &&
        cursorDpiX != 0) {
        app.dpi = static_cast<unsigned>(cursorDpiX);
    } else {
        app.dpi = dpi;
    }

    app.popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
        kAppName.data(), kAppName.data(),
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandleW(nullptr), &app);
    if (app.popup == nullptr) return false;
    ::AddClipboardFormatListener(app.popup);

    reposition(app, maxHeightDip(app));
    RECT rect{};
    GetWindowRect(app.popup, &rect);
    app.gfx = gfx::create(app.popup, app.dpi, rect.right - rect.left, rect.bottom - rect.top,
                          app.settings);
    app.backdrop = backdrop::create(app.popup);
    if (app.backdrop) backdrop::position(*app.backdrop, rect);
    const HICON icon = LoadIconW(GetModuleHandleW(nullptr),
                                 MAKEINTRESOURCEW(kIconResourceId));
    if (!tray::create(app.popup, icon)) {
        diagnostics::append(util::config::logPath(), "tray icon registration failed");
    }
    tray::setAnimationsChecked(app.settings.general.animations);
    ShowWindow(app.popup, SW_HIDE);
    return true;
}

void destroy(AppState& app) {
    flushHistory(app);
    stopShowTimer(app);
    stopCompactTimer(app);
    app.showAnimation.reset();
    app.compactAnimation.reset();
    settings::close();
    tray::destroy();
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
    KillTimer(app.popup, kTrimTimerId);
    stopCompactTimer(app);
    app.compactAnimation.reset();
    app::refreshResults(app);
    syncDpiToMonitor(app);
    reposition(app, desiredHeightDip(app));
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
    if (app.backdrop && app.settings.theme.backdrop == Backdrop::Acrylic) {
        HIGHCONTRASTW hc{};
        hc.cbSize = sizeof(hc);
        const bool highContrast =
            SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0 &&
            (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        backdrop::setVisible(*app.backdrop, !highContrast);
    }
    SetWindowPos(app.popup, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    if (app.backdrop) backdrop::setVisible(*app.backdrop, false);
    SetTimer(app.popup, kTrimTimerId, 250, nullptr);
    app.focused = false;
    app.visible = false;
}

void toggle(AppState& app) {
    if (app.visible) hide(app);
    else show(app);
}

} // namespace tapir::window