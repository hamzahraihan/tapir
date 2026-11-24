#include "config.h"

#include "hotkey_grammar.h"

#define TOML_EXCEPTIONS 0
#include <toml/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace tapir {

Settings defaultSettings() {
    return {};
}

namespace config {
namespace {

enum class Lookup { Missing, Valid, Invalid };

void warn(ParseResult& result, std::string_view key) {
    result.diagnostics.push_back(
        {Severity::Warning, std::string(key) + ": invalid value; using default"});
}

void warnUnknown(ParseResult& result, std::string_view key) {
    result.diagnostics.push_back(
        {Severity::Warning, std::string(key) + ": unknown key"});
}

void report(std::string_view key, Lookup status, ParseResult& result) {
    if (status == Lookup::Invalid) warn(result, key);
}

template <typename T>
Lookup get(const toml::table& table, std::string_view key, T& out) {
    const auto node = table[std::string(key)];
    if (!node) return Lookup::Missing;
    const auto value = node.value<T>();
    if (!value) return Lookup::Invalid;
    out = *value;
    return Lookup::Valid;
}

char asciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

std::wstring widen(std::string_view text) {
    std::wstring out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        unsigned codePoint = 0;
        int trailing = 0;
        if (lead < 0x80) {
            codePoint = lead;
        } else if ((lead & 0xe0) == 0xc0) {
            codePoint = lead & 0x1f;
            trailing = 1;
        } else if ((lead & 0xf0) == 0xe0) {
            codePoint = lead & 0x0f;
            trailing = 2;
        } else if ((lead & 0xf8) == 0xf0) {
            codePoint = lead & 0x07;
            trailing = 3;
        } else {
            out.push_back(0xfffd);
            ++i;
            continue;
        }

        bool complete = i + static_cast<std::size_t>(trailing) < text.size();
        for (int n = 1; complete && n <= trailing; ++n) {
            const auto byte = static_cast<unsigned char>(text[i + static_cast<std::size_t>(n)]);
            if ((byte & 0xc0) != 0x80) {
                complete = false;
            } else {
                codePoint = (codePoint << 6) | (byte & 0x3f);
            }
        }
        if (!complete) {
            out.push_back(0xfffd);
            ++i;
            continue;
        }

        i += static_cast<std::size_t>(trailing) + 1;
        if (codePoint <= 0xffff) {
            out.push_back(static_cast<wchar_t>(codePoint));
        } else {
            codePoint -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xd800 + (codePoint >> 10)));
            out.push_back(static_cast<wchar_t>(0xdc00 + (codePoint & 0x3ff)));
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    const char lower = asciiLower(c);
    if (lower >= 'a' && lower <= 'f') return lower - 'a' + 10;
    return -1;
}

std::optional<Rgb> parseColor(std::string_view text) {
    if (text.size() != 7 || text[0] != '#') return std::nullopt;
    int bytes[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const int high = hexDigit(text[static_cast<std::size_t>(1 + i * 2)]);
        const int low = hexDigit(text[static_cast<std::size_t>(2 + i * 2)]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes[i] = high * 16 + low;
    }
    return Rgb{static_cast<std::uint8_t>(bytes[0]), static_cast<std::uint8_t>(bytes[1]),
               static_cast<std::uint8_t>(bytes[2])};
}

bool readStringArray(const toml::table& table, std::string_view key,
                     std::vector<std::wstring>& out) {
    const auto node = table[std::string(key)];
    if (!node) return true;
    const toml::array* array = node.as_array();
    if (array == nullptr) return false;
    std::vector<std::wstring> values;
    for (const auto& element : *array) {
        const auto value = element.value<std::string>();
        if (!value) return false;
        values.push_back(widen(*value));
    }
    out = std::move(values);
    return true;
}

void readColor(const toml::table& table, std::string_view key, Rgb& out,
               ParseResult& result) {
    std::string raw;
    const Lookup status = get(table, key, raw);
    report(key, status, result);
    if (status != Lookup::Valid) return;
    const auto color = parseColor(raw);
    if (color) {
        out = *color;
    } else {
        warn(result, key);
    }
}

void checkUnknownKeys(const toml::table& table, std::string_view prefix,
                      std::initializer_list<std::string_view> known,
                      ParseResult& result) {
    for (const auto& [key, node] : table) {
        if (std::find(known.begin(), known.end(), std::string_view(key)) != known.end()) {
            continue;
        }
        warnUnknown(result, std::string(prefix) + std::string(key));
    }
}

void parseGeneral(const toml::table& table, Settings& settings, ParseResult& result) {
    std::int64_t version = 0;
    Lookup status = get(table, "version", version);
    report("general.version", status, result);
    if (status == Lookup::Valid) {
        if (version == 1) {
            settings.general.version = 1;
        } else {
            warn(result, "general.version");
        }
    }

    std::string hotkey;
    status = get(table, "hotkey", hotkey);
    report("general.hotkey", status, result);
    if (status == Lookup::Valid) {
        std::wstring wide = widen(hotkey);
        if (hotkey::parse(wide)) {
            settings.general.hotkey = std::move(wide);
        } else {
            warn(result, "general.hotkey");
        }
    }

    bool animations = false;
    status = get(table, "animations", animations);
    report("general.animations", status, result);
    if (status == Lookup::Valid) settings.general.animations = animations;

    bool startWithWindows = false;
    status = get(table, "start_with_windows", startWithWindows);
    report("general.start_with_windows", status, result);
    if (status == Lookup::Valid) settings.general.startWithWindows = startWithWindows;

    checkUnknownKeys(table, "general.",
                     {"version", "hotkey", "animations", "start_with_windows"}, result);
}

void parseWindow(const toml::table& table, Settings& settings, ParseResult& result) {
    const auto monitorNode = table[std::string("monitor")];
    if (monitorNode) {
        if (const auto index = monitorNode.value<std::int64_t>()) {
            if (*index >= 1) {
                settings.window.monitor = {Monitor::Index, static_cast<int>(*index - 1)};
            } else {
                warn(result, "window.monitor");
            }
        } else if (const auto name = monitorNode.value<std::string>()) {
            if (iequals(*name, "cursor")) {
                settings.window.monitor = {Monitor::Cursor, 0};
            } else if (iequals(*name, "active")) {
                settings.window.monitor = {Monitor::Active, 0};
            } else if (iequals(*name, "primary")) {
                settings.window.monitor = {Monitor::Primary, 0};
            } else {
                warn(result, "window.monitor");
            }
        } else {
            warn(result, "window.monitor");
        }
    }

    std::string position;
    Lookup status = get(table, "position", position);
    report("window.position", status, result);
    if (status == Lookup::Valid) {
        if (iequals(position, "upper-center")) {
            settings.window.position = WindowPosition::UpperCenter;
        } else if (iequals(position, "center")) {
            settings.window.position = WindowPosition::Center;
        } else {
            warn(result, "window.position");
        }
    }

    std::int64_t width = 0;
    status = get(table, "width", width);
    report("window.width", status, result);
    if (status == Lookup::Valid) {
        if (width > 0 && width <= std::numeric_limits<int>::max()) {
            settings.window.widthDip = static_cast<int>(width);
        } else {
            warn(result, "window.width");
        }
    }

    bool compact = false;
    status = get(table, "compact", compact);
    report("window.compact", status, result);
    if (status == Lookup::Valid) settings.window.compact = compact;

    std::int64_t rows = 0;
    status = get(table, "max_visible_rows", rows);
    report("window.max_visible_rows", status, result);
    if (status == Lookup::Valid) {
        if (rows > 0 && rows <= std::numeric_limits<int>::max()) {
            settings.window.maxVisibleRows = static_cast<int>(rows);
        } else {
            warn(result, "window.max_visible_rows");
        }
    }

    bool detailsPane = false;
    status = get(table, "details_pane", detailsPane);
    report("window.details_pane", status, result);
    if (status == Lookup::Valid) settings.window.detailsPane = detailsPane;

    checkUnknownKeys(table, "window.",
                     {"monitor", "position", "width", "compact", "max_visible_rows",
                      "details_pane"},
                     result);
}

void parseTheme(const toml::table& table, Settings& settings, ParseResult& result) {
    std::string backdrop;
    Lookup status = get(table, "backdrop", backdrop);
    report("theme.backdrop", status, result);
    if (status == Lookup::Valid) {
        if (iequals(backdrop, "acrylic")) {
            settings.theme.backdrop = Backdrop::Acrylic;
        } else if (iequals(backdrop, "mica")) {
            settings.theme.backdrop = Backdrop::Mica;
        } else if (iequals(backdrop, "none")) {
            settings.theme.backdrop = Backdrop::None;
        } else {
            warn(result, "theme.backdrop");
        }
    }

    double opacity = 0.0;
    status = get(table, "backdrop_opacity", opacity);
    report("theme.backdrop_opacity", status, result);
    if (status == Lookup::Valid) {
        settings.theme.backdropOpacity =
            static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    }

    std::string font;
    status = get(table, "font", font);
    report("theme.font", status, result);
    if (status == Lookup::Valid) settings.theme.font = widen(font);

    readColor(table, "background", settings.theme.background, result);
    readColor(table, "foreground", settings.theme.foreground, result);
    readColor(table, "accent", settings.theme.accent, result);
    readColor(table, "muted", settings.theme.muted, result);
    readColor(table, "surface", settings.theme.surface, result);
    readColor(table, "selection", settings.theme.selection, result);

    checkUnknownKeys(table, "theme.",
                     {"backdrop", "backdrop_opacity", "font", "background",
                      "foreground", "accent", "muted", "surface", "selection"},
                     result);
}

void parseHistory(const toml::table& table, Settings& settings, ParseResult& result) {
    std::int64_t entries = 0;
    Lookup status = get(table, "max_entries", entries);
    report("history.max_entries", status, result);
    if (status == Lookup::Valid) {
        if (entries > 0 && entries <= std::numeric_limits<int>::max()) {
            settings.history.maxEntries = static_cast<int>(entries);
        } else {
            warn(result, "history.max_entries");
        }
    }

    std::int64_t bytes = 0;
    status = get(table, "max_entry_bytes", bytes);
    report("history.max_entry_bytes", status, result);
    if (status == Lookup::Valid) {
        if (bytes > 0 && bytes <= std::numeric_limits<int>::max()) {
            settings.history.maxEntryBytes = static_cast<int>(bytes);
        } else {
            warn(result, "history.max_entry_bytes");
        }
    }

    if (!readStringArray(table, "ignore_processes", settings.history.ignoreProcesses)) {
        warn(result, "history.ignore_processes");
    }

    checkUnknownKeys(table, "history.",
                     {"max_entries", "max_entry_bytes", "ignore_processes"}, result);
}

void parseIndex(const toml::table& table, Settings& settings, ParseResult& result) {
    if (!readStringArray(table, "sources", settings.index.sources)) {
        warn(result, "index.sources");
    }
    checkUnknownKeys(table, "index.", {"sources"}, result);
}

void parsePrivacy(const toml::table& table, Settings& settings, ParseResult& result) {
    bool encrypt = false;
    const Lookup status = get(table, "encrypt_history", encrypt);
    report("privacy.encrypt_history", status, result);
    if (status == Lookup::Valid) settings.privacy.encryptHistory = encrypt;
    checkUnknownKeys(table, "privacy.", {"encrypt_history"}, result);
}

} // namespace

ParseResult parse(std::string_view text) {
    ParseResult result;
    const auto document = toml::parse(text);
    if (!document) {
        result.valid = false;
        result.diagnostics.push_back({Severity::Error, "config.toml: malformed TOML"});
        return result;
    }

    const toml::table& root = document.table();
    const std::initializer_list<std::string_view> tables =
        {"general", "window", "theme", "history", "index", "privacy"};
    checkUnknownKeys(root, "", tables, result);

    if (const auto node = root["general"]) {
        if (const auto table = node.as_table()) {
            parseGeneral(*table, result.settings, result);
        } else {
            warn(result, "general");
        }
    }
    if (const auto node = root["window"]) {
        if (const auto table = node.as_table()) {
            parseWindow(*table, result.settings, result);
        } else {
            warn(result, "window");
        }
    }
    if (const auto node = root["theme"]) {
        if (const auto table = node.as_table()) {
            parseTheme(*table, result.settings, result);
        } else {
            warn(result, "theme");
        }
    }
    if (const auto node = root["history"]) {
        if (const auto table = node.as_table()) {
            parseHistory(*table, result.settings, result);
        } else {
            warn(result, "history");
        }
    }
    if (const auto node = root["index"]) {
        if (const auto table = node.as_table()) {
            parseIndex(*table, result.settings, result);
        } else {
            warn(result, "index");
        }
    }
    if (const auto node = root["privacy"]) {
        if (const auto table = node.as_table()) {
            parsePrivacy(*table, result.settings, result);
        } else {
            warn(result, "privacy");
        }
    }
    return result;
}

std::string defaultText() {
    return R"toml(# tapir configuration. Missing keys use the defaults below.
# Invalid values revert only that key; unknown keys are logged and ignored.

[general]
version = 1                 # configuration schema version
hotkey = "Alt+Space"        # Ctrl, Alt, Shift, Win joined by + with one key
animations = true
start_with_windows = false  # opt in to launching tapir at sign-in

[window]
monitor = "cursor"          # cursor, active, primary, or a 1-based monitor index
position = "upper-center"   # upper-center or center
width = 680                 # width in DIP
compact = true
max_visible_rows = 8
details_pane = false        # opt in to the clipboard details pane

[theme]
backdrop = "acrylic"        # acrylic, mica, or none
backdrop_opacity = 0.80     # 0.0 to 1.0
font = "Segoe UI Variable"
background = "#1e1e2e"
foreground = "#cdd6f4"
accent = "#cba6f7"
muted = "#6c7086"
surface = "#313244"
selection = "#cba6f7"

[history]
max_entries = 500
max_entry_bytes = 1048576
ignore_processes = []

[index]
sources = ["appsfolder", "startmenu"]

[privacy]
encrypt_history = true
)toml";
}

} // namespace config
} // namespace tapir
