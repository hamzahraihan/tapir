#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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
    bool operator==(const MonitorSelection&) const = default;
};

enum class WindowPosition { UpperCenter, Center };

enum class Backdrop { Acrylic, Mica, None };

struct Settings {
    struct General {
        int version = 1;
        std::wstring hotkey = L"Alt+Space";
        bool animations = true;
        bool startWithWindows = false;
        bool operator==(const General&) const = default;
    } general;

    struct Window {
        MonitorSelection monitor;
        WindowPosition position = WindowPosition::UpperCenter;
        int widthDip = 680;
        bool compact = true;
        int maxVisibleRows = 8;
        bool detailsPane = false;
        bool operator==(const Window&) const = default;
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
        bool operator==(const Theme&) const = default;
    } theme;

    struct History {
        int maxEntries = 500;
        int maxEntryBytes = 1048576;
        std::vector<std::wstring> ignoreProcesses;
        bool operator==(const History&) const = default;
    } history;

    struct Index {
        std::vector<std::wstring> sources{L"appsfolder", L"startmenu"};
        bool operator==(const Index&) const = default;
    } index;

    struct Privacy {
        bool encryptHistory = true;
        bool operator==(const Privacy&) const = default;
    } privacy;

    bool operator==(const Settings&) const = default;
};

Settings defaultSettings();

namespace config {

enum class Severity { Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Warning;
    std::string message;
};

struct ParseResult {
    Settings settings;
    std::vector<Diagnostic> diagnostics;
    bool valid = true;
};

ParseResult parse(std::string_view text);

std::string defaultText();

} // namespace config
} // namespace tapir
