#include <doctest.h>
#include <string>
#include <vector>
#include "config.h"

using tapir::Backdrop;
using tapir::Monitor;
using tapir::Rgb;
using tapir::Settings;
using tapir::WindowPosition;
using tapir::defaultSettings;

TEST_CASE("default settings match the spec schema") {
    const Settings s = defaultSettings();

    CHECK(s.general.version == 1);
    CHECK(s.general.hotkey == L"Ctrl+Space");
    CHECK(s.general.animations == true);

    CHECK(s.window.monitor.kind == Monitor::Cursor);
    CHECK(s.window.monitor.index == 0);
    CHECK(s.window.position == WindowPosition::UpperCenter);
    CHECK(s.window.widthDip == 680);
    CHECK(s.window.compact == true);
    CHECK(s.window.maxVisibleRows == 8);

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
    CHECK(b.general.hotkey == L"Ctrl+Space");
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
