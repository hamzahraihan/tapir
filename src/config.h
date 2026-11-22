#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tapir {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    bool operator==(const Rgb&) const = default;
};

enum class Monitor { Cursor, Active, Primary, Index };

struct MonitorSelection {
    Monitor kind = Monitor::Cursor;
    int index = 0;
};

enum class WindowPosition { UpperCenter, Center };

enum class Backdrop { Acrylic, Mica, None };

struct Settings {
    struct General {
        int version = 1;
        std::wstring hotkey = L"Ctrl+Space";
        bool animations = true;
    } general;

    struct Window {
        MonitorSelection monitor;
        WindowPosition position = WindowPosition::UpperCenter;
        int widthDip = 680;
        bool compact = true;
        int maxVisibleRows = 8;
    } window;

    struct Theme {
        Backdrop backdrop = Backdrop::Acrylic;
        float backdropOpacity = 0.80f;
        std::wstring font = L"Segoe UI Variable";
        Rgb background{0x1e, 0x1e, 0x2e};
        Rgb foreground{0xcd, 0xd6, 0xf4};
        Rgb accent{0xcb, 0xa6, 0xf7};
        Rgb muted{0x6c, 0x70, 0x86};
        Rgb surface{0x31, 0x32, 0x44};
        Rgb selection{0xcb, 0xa6, 0xf7};
    } theme;

    struct History {
        int maxEntries = 500;
        int maxEntryBytes = 1048576;
        std::vector<std::wstring> ignoreProcesses;
    } history;

    struct Index {
        std::vector<std::wstring> sources{L"appsfolder", L"startmenu"};
    } index;

    struct Privacy {
        bool encryptHistory = true;
    } privacy;
};

Settings defaultSettings();

} // namespace tapir
