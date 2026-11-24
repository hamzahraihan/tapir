#include <doctest.h>
#include <string>
#include <string_view>
#include <vector>
#include "config.h"

using tapir::config::ParseResult;
using tapir::config::Severity;
using tapir::Backdrop;
using tapir::Monitor;
using tapir::Rgb;
using tapir::Settings;
using tapir::WindowPosition;
using tapir::defaultSettings;

TEST_CASE("default settings match the spec schema") {
    const Settings s = defaultSettings();

    CHECK(s.general.version == 1);
    CHECK(s.general.hotkey == L"Alt+Space");
    CHECK(s.general.animations == true);
    CHECK(s.general.startWithWindows == false);

    CHECK(s.window.monitor.kind == Monitor::Cursor);
    CHECK(s.window.monitor.index == 0);
    CHECK(s.window.position == WindowPosition::UpperCenter);
    CHECK(s.window.widthDip == 680);
    CHECK(s.window.compact == true);
    CHECK(s.window.maxVisibleRows == 8);
    CHECK(s.window.detailsPane == false);

    CHECK(s.theme.backdrop == Backdrop::Acrylic);
    CHECK(s.theme.backdropOpacity == 0.80f);
    CHECK(s.theme.font == L"Segoe UI Variable");
    CHECK(s.theme.background == (Rgb{0x1e, 0x1e, 0x2e}));
    CHECK(s.theme.foreground == (Rgb{0xcd, 0xd6, 0xf4}));
    CHECK(s.theme.accent == (Rgb{0xcb, 0xa6, 0xf7}));
    CHECK(s.theme.muted == (Rgb{0x6c, 0x70, 0x86}));
    CHECK(s.theme.surface == (Rgb{0x31, 0x32, 0x44}));
    CHECK(s.theme.selection == (Rgb{0xcb, 0xa6, 0xf7}));

    CHECK(s.history.maxEntries == 500);
    CHECK(s.history.maxEntryBytes == 1048576);
    CHECK(s.history.ignoreProcesses.empty());

    REQUIRE(s.index.sources.size() == 2);
    CHECK(s.index.sources[0] == L"appsfolder");
    CHECK(s.index.sources[1] == L"startmenu");

    CHECK(s.privacy.encryptHistory == true);
}

TEST_CASE("defaultSettings returns an independent value on each call") {
    Settings a = defaultSettings();
    a.general.hotkey = L"Alt+F4";
    a.window.widthDip = 999;
    a.index.sources.push_back(L"mutated");

    const Settings b = defaultSettings();
    CHECK(b.general.hotkey == L"Alt+Space");
    CHECK(b.window.widthDip == 680);
    REQUIRE(b.index.sources.size() == 2);
    CHECK(b.index.sources[0] == L"appsfolder");
    CHECK(b.index.sources[1] == L"startmenu");
}

TEST_CASE("settings can be overridden in memory") {
    Settings s = defaultSettings();
    s.window.position = WindowPosition::Center;
    s.theme.backdrop = Backdrop::None;
    s.theme.selection = Rgb{0xff, 0, 0};

    CHECK(s.window.position == WindowPosition::Center);
    CHECK(s.theme.backdrop == Backdrop::None);
    CHECK(s.theme.selection == (Rgb{0xff, 0, 0}));
}

TEST_CASE("the commented default config parses back to the defaults") {
    const std::string text = tapir::config::defaultText();
    CHECK(text.find("[general]") != std::string::npos);
    CHECK(text.find("[window]") != std::string::npos);
    CHECK(text.find("[theme]") != std::string::npos);
    CHECK(text.find("[history]") != std::string::npos);
    CHECK(text.find("[index]") != std::string::npos);
    CHECK(text.find("[privacy]") != std::string::npos);
    CHECK(text.find("Alt+Space") != std::string::npos);
    CHECK(text.find('#') != std::string::npos);

    const ParseResult parsed = tapir::config::parse(text);
    CHECK(parsed.valid);
    CHECK(parsed.diagnostics.empty());
    CHECK(parsed.settings.general.hotkey == L"Alt+Space");
    CHECK(parsed.settings.general.version == 1);
    CHECK(parsed.settings.general.startWithWindows == false);
    CHECK(parsed.settings.window.widthDip == 680);
    CHECK(parsed.settings.window.detailsPane == false);
    CHECK(parsed.settings.theme.backdropOpacity == 0.80f);
    CHECK(parsed.settings.theme.background == (Rgb{0x1e, 0x1e, 0x2e}));
    CHECK(parsed.settings.privacy.encryptHistory == true);
}

TEST_CASE("all six tables override their defaults") {
    const ParseResult parsed = tapir::config::parse(R"toml(
[general]
version = 1
hotkey = "Ctrl+K"
animations = false
start_with_windows = true

[window]
monitor = "active"
position = "center"
width = 900
compact = false
max_visible_rows = 12
details_pane = true

[theme]
backdrop = "mica"
backdrop_opacity = 0.5
font = "Consolas"
background = "#010203"
foreground = "#040506"
accent = "#070809"
muted = "#0a0b0c"
surface = "#0d0e0f"
selection = "#101112"

[history]
max_entries = 42
max_entry_bytes = 2048
ignore_processes = ["Vault.exe", "keeper"]

[index]
sources = ["startmenu"]

[privacy]
encrypt_history = false
)toml");

    REQUIRE(parsed.valid);
    CHECK(parsed.diagnostics.empty());
    CHECK(parsed.settings.general.version == 1);
    CHECK(parsed.settings.general.hotkey == L"Ctrl+K");
    CHECK(parsed.settings.general.animations == false);
    CHECK(parsed.settings.general.startWithWindows == true);
    CHECK(parsed.settings.window.monitor.kind == Monitor::Active);
    CHECK(parsed.settings.window.position == WindowPosition::Center);
    CHECK(parsed.settings.window.widthDip == 900);
    CHECK(parsed.settings.window.compact == false);
    CHECK(parsed.settings.window.maxVisibleRows == 12);
    CHECK(parsed.settings.window.detailsPane == true);
    CHECK(parsed.settings.theme.backdrop == Backdrop::Mica);
    CHECK(parsed.settings.theme.backdropOpacity == 0.5f);
    CHECK(parsed.settings.theme.font == L"Consolas");
    CHECK(parsed.settings.theme.background == (Rgb{1, 2, 3}));
    CHECK(parsed.settings.theme.foreground == (Rgb{4, 5, 6}));
    CHECK(parsed.settings.theme.accent == (Rgb{7, 8, 9}));
    CHECK(parsed.settings.theme.muted == (Rgb{10, 11, 12}));
    CHECK(parsed.settings.theme.surface == (Rgb{13, 14, 15}));
    CHECK(parsed.settings.theme.selection == (Rgb{16, 17, 18}));
    CHECK(parsed.settings.history.maxEntries == 42);
    CHECK(parsed.settings.history.maxEntryBytes == 2048);
    REQUIRE(parsed.settings.history.ignoreProcesses.size() == 2);
    CHECK(parsed.settings.history.ignoreProcesses[0] == L"Vault.exe");
    CHECK(parsed.settings.history.ignoreProcesses[1] == L"keeper");
    REQUIRE(parsed.settings.index.sources.size() == 1);
    CHECK(parsed.settings.index.sources[0] == L"startmenu");
    CHECK(parsed.settings.privacy.encryptHistory == false);
}

TEST_CASE("missing keys keep their defaults") {
    const ParseResult parsed = tapir::config::parse("[theme]\nbackdrop = \"none\"\n");
    REQUIRE(parsed.valid);
    CHECK(parsed.diagnostics.empty());
    CHECK(parsed.settings.theme.backdrop == Backdrop::None);
    CHECK(parsed.settings.theme.backdropOpacity == 0.80f);
    CHECK(parsed.settings.general.hotkey == L"Alt+Space");
    CHECK(parsed.settings.general.startWithWindows == false);
    CHECK(parsed.settings.window.widthDip == 680);
    CHECK(parsed.settings.window.detailsPane == false);
    CHECK(parsed.settings.history.maxEntries == 500);
    CHECK(parsed.settings.privacy.encryptHistory == true);
}

TEST_CASE("an integer monitor selects a one-based index") {
    const ParseResult parsed = tapir::config::parse("[window]\nmonitor = 2\n");
    REQUIRE(parsed.valid);
    CHECK(parsed.settings.window.monitor.kind == Monitor::Index);
    CHECK(parsed.settings.window.monitor.index == 1);
}

TEST_CASE("invalid values revert only their own key and warn") {
    const ParseResult parsed = tapir::config::parse(R"toml(
[general]
version = 2
hotkey = "Alt+Alt+Space"
animations = "no"
start_with_windows = "yes"

[window]
monitor = "nowhere"
position = "bottom"
width = "wide"
compact = "maybe"
max_visible_rows = 0
details_pane = "yes"

[theme]
backdrop = "glass"
backdrop_opacity = "opaque"
font = 7
background = "#ggg"
foreground = "#cdd6f4"

[history]
max_entries = -1
max_entry_bytes = "big"
ignore_processes = [1]

[index]
sources = [true]

[privacy]
encrypt_history = "no"
)toml");

    REQUIRE(parsed.valid);
    CHECK(parsed.settings.general.version == 1);
    CHECK(parsed.settings.general.hotkey == L"Alt+Space");
    CHECK(parsed.settings.general.animations == true);
    CHECK(parsed.settings.general.startWithWindows == false);
    CHECK(parsed.settings.window.monitor.kind == Monitor::Cursor);
    CHECK(parsed.settings.window.position == WindowPosition::UpperCenter);
    CHECK(parsed.settings.window.widthDip == 680);
    CHECK(parsed.settings.window.compact == true);
    CHECK(parsed.settings.window.maxVisibleRows == 8);
    CHECK(parsed.settings.window.detailsPane == false);
    CHECK(parsed.settings.theme.backdrop == Backdrop::Acrylic);
    CHECK(parsed.settings.theme.backdropOpacity == 0.80f);
    CHECK(parsed.settings.theme.font == L"Segoe UI Variable");
    CHECK(parsed.settings.theme.background == (Rgb{0x1e, 0x1e, 0x2e}));
    CHECK(parsed.settings.theme.foreground == (Rgb{0xcd, 0xd6, 0xf4}));
    CHECK(parsed.settings.history.maxEntries == 500);
    CHECK(parsed.settings.history.maxEntryBytes == 1048576);
    CHECK(parsed.settings.history.ignoreProcesses.empty());
    REQUIRE(parsed.settings.index.sources.size() == 2);
    CHECK(parsed.settings.index.sources[0] == L"appsfolder");
    CHECK(parsed.settings.index.sources[1] == L"startmenu");
    CHECK(parsed.settings.privacy.encryptHistory == true);
    CHECK(parsed.diagnostics.size() == 19);
    for (const auto& diagnostic : parsed.diagnostics) {
        CHECK(diagnostic.severity == Severity::Warning);
    }
}

TEST_CASE("out of range opacity clamps to the unit interval") {
    const ParseResult high = tapir::config::parse("[theme]\nbackdrop_opacity = 1.5\n");
    const ParseResult low = tapir::config::parse("[theme]\nbackdrop_opacity = -0.5\n");
    REQUIRE(high.valid);
    REQUIRE(low.valid);
    CHECK(high.settings.theme.backdropOpacity == 1.0f);
    CHECK(low.settings.theme.backdropOpacity == 0.0f);
}

TEST_CASE("unknown keys and tables warn without changing settings") {
    const ParseResult parsed = tapir::config::parse(R"toml(
[general]
animations = false
bogus = 1

[unknown]
key = "value"
)toml");

    REQUIRE(parsed.valid);
    CHECK(parsed.settings.general.animations == false);
    CHECK(parsed.settings.general.hotkey == L"Alt+Space");
    REQUIRE(parsed.diagnostics.size() == 2);
    for (const auto& diagnostic : parsed.diagnostics) {
        CHECK(diagnostic.severity == Severity::Warning);
    }
}

TEST_CASE("malformed TOML is rejected with an error diagnostic") {
    const ParseResult parsed = tapir::config::parse("[general\nhotkey = \"Alt+Space\"\n");
    CHECK_FALSE(parsed.valid);
    CHECK(parsed.settings.general.hotkey == L"Alt+Space");
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics[0].severity == Severity::Error);
}

TEST_CASE("oversized counts revert to defaults with a warning") {
    const ParseResult parsed = tapir::config::parse(R"toml(
[window]
width = 99999999999
[history]
max_entries = 99999999999
)toml");

    REQUIRE(parsed.valid);
    CHECK(parsed.settings.window.widthDip == 680);
    CHECK(parsed.settings.history.maxEntries == 500);
    CHECK(parsed.diagnostics.size() == 2);
}

TEST_CASE("warning messages do not echo configuration values") {
    const ParseResult parsed = tapir::config::parse("[history]\nmax_entries = \"SECRET-CLIPBOARD-TEXT\"\n");
    REQUIRE_FALSE(parsed.diagnostics.empty());
    for (const auto& diagnostic : parsed.diagnostics) {
        CHECK(diagnostic.message.find("SECRET-CLIPBOARD-TEXT") == std::string::npos);
    }
}
