#include "app.h"

#include "index.h"

#include <algorithm>
#include <span>
#include <utility>

namespace tapir {
namespace {

std::size_t visibleCount(const AppState& app) noexcept {
    const int configured = app.settings.window.maxVisibleRows;
    if (configured <= 0) return 0;
    return std::min(app.resultCount, static_cast<std::size_t>(configured));
}

void clampSelection(AppState& app) noexcept {
    const std::size_t visible = visibleCount(app);
    if (app.resultCount == 0 || visible == 0) {
        app.selected = 0;
        app.scrollOffset = 0;
        return;
    }
    if (app.selected >= app.resultCount) app.selected = app.resultCount - 1;
    const std::size_t maxScroll = app.resultCount - visible;
    app.scrollOffset = std::min(app.scrollOffset, maxScroll);
    if (app.selected < app.scrollOffset) {
        app.scrollOffset = app.selected;
    } else if (app.selected >= app.scrollOffset + visible) {
        app.scrollOffset = app.selected - visible + 1;
    }
}

} // namespace

AppState::~AppState() = default;

void app::refreshResults(AppState& app) {
    if (app.appsMode && app.appIndex != nullptr) {
        auto fresh = app.appIndex->Current();
        if (fresh != nullptr && fresh != app.snapshot) app.snapshot = std::move(fresh);
    }

    if (app.rankedSnapshot == app.snapshot.get() && app.rankedQuery == app.text.value() &&
        app.rankedAppsMode == app.appsMode) {
        return;
    }
    app.rankedSnapshot = app.snapshot.get();
    app.rankedQuery = app.text.value();
    app.rankedAppsMode = app.appsMode;

    app.resultCount = 0;
    app.selected = 0;
    app.scrollOffset = 0;
    app.hoveredRow = -1;
    if (!app.appsMode || app.snapshot == nullptr) return;

    const auto& entries = app.snapshot->entries;
    app.resultCount = fuzzy::Rank(
        std::span<const fuzzy::Entry>(entries.data(), entries.size()),
        app.text.value(), app.results);
    clampSelection(app);
}

std::size_t app::visibleRowCount(const AppState& app) {
    return visibleCount(app);
}

const fuzzy::Result* app::resultAtRow(const AppState& app, std::size_t row) {
    const std::size_t index = app.scrollOffset + row;
    if (row >= visibleCount(app) || index >= app.resultCount) return nullptr;
    return &app.results[index];
}

const index::App* app::appAtRow(const AppState& app, std::size_t row) {
    const fuzzy::Result* result = resultAtRow(app, row);
    if (result == nullptr) return nullptr;
    return appAtEntry(app, result->index);
}

const index::App* app::appAtEntry(const AppState& app, std::size_t entryIndex) {
    if (app.snapshot == nullptr || entryIndex >= app.snapshot->apps.size()) return nullptr;
    return &app.snapshot->apps[entryIndex];
}

const index::App* app::appAtResult(const AppState& app, std::size_t resultIndex) {
    if (resultIndex >= app.resultCount || resultIndex >= app.results.size()) return nullptr;
    return appAtEntry(app, app.results[resultIndex].index);
}

// The returned view points into app.snapshot's stable string storage; copy it in the
// same message if a snapshot swap can follow.
std::wstring_view app::ghostTitle(const AppState& app) noexcept {
    if (!app.appsMode || app.text.value().empty() || app.resultCount == 0) return {};
    return appAtResult(app, 0)->name;
}

void app::moveSelection(AppState& app, int delta) {
    if (app.resultCount == 0) return;
    const std::int64_t next = static_cast<std::int64_t>(app.selected) + delta;
    app.selected = static_cast<std::size_t>(std::clamp<std::int64_t>(
        next, 0, static_cast<std::int64_t>(app.resultCount - 1)));
    clampSelection(app);
}

void app::scrollResults(AppState& app, int notches) {
    if (app.resultCount == 0) return;
    const std::int64_t next = static_cast<std::int64_t>(app.scrollOffset) + notches;
    const std::size_t maxScroll = app.resultCount - visibleCount(app);
    app.scrollOffset = static_cast<std::size_t>(std::clamp<std::int64_t>(
        next, 0, static_cast<std::int64_t>(maxScroll)));
    app.hoveredRow = -1;
    clampSelection(app);
}

bool app::launchSelected(AppState& app) {
    const index::App* selected = appAtResult(app, app.selected);
    if (selected == nullptr) return false;
    return index::Index::Launch(*selected, app.popup);
}

} // namespace tapir
