#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "anim.h"
#include "config.h"
#include "fuzzy.h"
#include "gfx.h"
#include "input.h"

namespace tapir {

namespace index {
struct App;
class Index;
struct Snapshot;
} // namespace index

namespace clipboard {
class History;
struct Entry;
} // namespace clipboard

inline constexpr std::size_t kMaxResults = 256;
inline constexpr unsigned int kIndexReadyMessage = WM_APP + 1;

struct AppState {
    ~AppState();

    HWND popup = nullptr;
    HWND previous = nullptr;
    int hotkeyId = 1;
    bool visible = false;
    Settings settings = defaultSettings();
    std::unique_ptr<gfx::Context> gfx;
    input::TextBuffer text;
    bool appsMode = true;
    bool focused = false;
    bool pasteActive = false;
    unsigned dpi = 96;
    anim::Timeline showAnimation;
    bool showTimerActive = false;
    anim::Timeline compactAnimation;
    bool compactTimerActive = false;
    int compactFromHeightDip = 0;
    int compactToHeightDip = 0;

    std::unique_ptr<index::Index> appIndex;
    std::shared_ptr<const index::Snapshot> snapshot;
    std::unique_ptr<clipboard::History> clipboard;
    std::uint64_t clipboardRevision = 0;
    std::array<fuzzy::Result, kMaxResults> results{};
    std::size_t resultCount = 0;
    std::size_t selected = 0;
    std::size_t scrollOffset = 0;
    int hoveredRow = -1;
    std::wstring rankedQuery;
    const index::Snapshot* rankedSnapshot = nullptr;
    bool rankedAppsMode = true;
    std::uint64_t rankedClipboardRevision = 0;
};

namespace app {

void refreshResults(AppState& app);
std::size_t visibleRowCount(const AppState& app);
const fuzzy::Result* resultAtRow(const AppState& app, std::size_t row);
const index::App* appAtRow(const AppState& app, std::size_t row);
const clipboard::Entry* clipboardEntryAtRow(const AppState& app, std::size_t row);
const index::App* appAtEntry(const AppState& app, std::size_t entryIndex);
const index::App* appAtResult(const AppState& app, std::size_t resultIndex);
std::wstring_view ghostTitle(const AppState& app) noexcept;
void moveSelection(AppState& app, int delta);
void scrollResults(AppState& app, int notches);
bool launchSelected(AppState& app);

} // namespace app
} // namespace tapir
