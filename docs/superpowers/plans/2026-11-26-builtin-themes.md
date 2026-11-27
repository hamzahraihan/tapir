# Built-in Themes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add five built-in color theme families with dark and light palettes, selectable from config.toml and the Settings Appearance tab, without changing existing configurations' appearance.

**Architecture:** A pure header-only palette module supplies ten named palettes. `config.cpp` resolves the selected palette before applying explicit color overrides, preserving the existing precedence. The Settings UI appends two Cycle fields and refreshes the six color strings when either is cycled. Rendering, hot reload and high-contrast handling stay as they are.

**Tech Stack:** C++20, MSVC, CMake/Ninja, doctest, Win32/Direct2D (untouched for this feature).

## Global Constraints

- No new dependencies and no new file IO.
- The palette module is pure: no Win32 calls, no filesystem access, no allocation in lookup.
- Exactly five presets: `nord`, `catppuccin`, `kanagawa`, `material`, `gruvbox`.
- Exactly two modes: `dark`, `light`.
- Explicit `theme.background`, `foreground`, `accent`, `muted`, `surface`, `selection` keys override the preset.
- `catppuccin` dark must use background `#2b2b2b`; the default config's six color lines stay byte-identical.
- Existing Settings field indices 0-23 must not change.
- `/W4 /WX` clean; all tests pass; no commit unless explicitly requested.

---

### Task 1: Pure palette module and tests

**Files:**
- Modify: `src/config.h:8-16` (add ThemeMode)
- Create: `src/themes.h`
- Create: `tests/themes.test.cpp`
- Modify: `CMakeLists.txt:100-120` (add the test source)

**Interfaces:**
- Consumes: `tapir::Rgb` from `src/config.h`.
- Produces: `tapir::ThemeMode`, `tapir::themes::ThemePalette`, `tapir::themes::builtinPresetNames()`, `tapir::themes::builtinPalette(preset, mode)` for Tasks 2-3.

- [ ] **Step 1: Add ThemeMode to `src/config.h`**

Insert immediately after `struct Rgb`:

```cpp
enum class ThemeMode { Dark, Light };
```

- [ ] **Step 2: Create `src/themes.h`**

```cpp
#pragma once

#include "config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <span>
#include <string_view>

namespace tapir {

namespace themes {

struct ThemePalette {
    Rgb background{};
    Rgb foreground{};
    Rgb accent{};
    Rgb muted{};
    Rgb surface{};
    Rgb selection{};
    bool operator==(const ThemePalette&) const = default;
};

inline constexpr std::array<std::wstring_view, 5> kPresetNames{
    L"nord", L"catppuccin", L"kanagawa", L"material", L"gruvbox"};

inline constexpr std::array<ThemePalette, 10> kPalettes{{
    {{0x2e, 0x34, 0x40}, {0xd8, 0xde, 0xe9}, {0x88, 0xc0, 0xd0}, {0xa0, 0xa8, 0xbd}, {0x3b, 0x42, 0x52}, {0x43, 0x4c, 0x5e}},
    {{0xec, 0xef, 0xf4}, {0x2e, 0x34, 0x40}, {0x5e, 0x81, 0xac}, {0x4c, 0x56, 0x6a}, {0xe5, 0xe9, 0xf0}, {0xd8, 0xde, 0xe9}},
    {{0x2b, 0x2b, 0x2b}, {0xcd, 0xd6, 0xf4}, {0xcb, 0xa6, 0xf7}, {0xa6, 0xad, 0xc8}, {0x31, 0x32, 0x44}, {0x45, 0x47, 0x5a}},
    {{0xef, 0xf1, 0xf5}, {0x4c, 0x4f, 0x69}, {0x88, 0x39, 0xef}, {0x5c, 0x5f, 0x85}, {0xe6, 0xe9, 0xef}, {0xcc, 0xd0, 0xda}},
    {{0x1f, 0x1f, 0x28}, {0xdc, 0xd7, 0xba}, {0x7e, 0x9c, 0xd8}, {0xa3, 0xa0, 0x8f}, {0x2a, 0x2a, 0x37}, {0x22, 0x32, 0x49}},
    {{0xf2, 0xec, 0xbc}, {0x54, 0x54, 0x64}, {0x62, 0x4c, 0x83}, {0x66, 0x62, 0x58}, {0xdc, 0xd5, 0xac}, {0xc7, 0xd7, 0xe0}},
    {{0x21, 0x21, 0x21}, {0xee, 0xff, 0xff}, {0x82, 0xaa, 0xff}, {0x78, 0x90, 0x9c}, {0x29, 0x29, 0x29}, {0x3c, 0x44, 0x52}},
    {{0xfa, 0xfa, 0xfa}, {0x37, 0x47, 0x4f}, {0x7c, 0x4d, 0xff}, {0x54, 0x6e, 0x7a}, {0xf0, 0xf0, 0xf0}, {0xd7, 0xdc, 0xe0}},
    {{0x28, 0x28, 0x28}, {0xeb, 0xdb, 0xb2}, {0xd3, 0x86, 0x9b}, {0xa8, 0x99, 0x84}, {0x3c, 0x38, 0x36}, {0x50, 0x49, 0x45}},
    {{0xfb, 0xf1, 0xc7}, {0x3c, 0x38, 0x36}, {0xaf, 0x3a, 0x03}, {0x66, 0x5c, 0x54}, {0xeb, 0xdb, 0xb2}, {0xd5, 0xc4, 0xa1}},
}};

inline std::span<const std::wstring_view> builtinPresetNames() noexcept {
    return kPresetNames;
}

inline bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto left = static_cast<wint_t>(std::towlower(static_cast<std::wint_t>(a[i])));
        const auto right = static_cast<wint_t>(std::towlower(static_cast<std::wint_t>(b[i])));
        if (left != right) return false;
    }
    return true;
}

inline std::optional<ThemePalette> builtinPalette(std::wstring_view preset,
                                                  ThemeMode mode) noexcept {
    const auto found = std::find_if(kPresetNames.begin(), kPresetNames.end(),
                                    [&](std::wstring_view name) { return equalsIgnoreCase(name, preset); });
    if (found == kPresetNames.end()) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(found - kPresetNames.begin()) * 2 +
                              (mode == ThemeMode::Light ? 1u : 0u);
    return kPalettes[index];
}

} // namespace themes
} // namespace tapir
```

The palette array is ordered as dark/light pairs in the same order as `kPresetNames`, exactly as listed in the approved design table.

- [ ] **Step 3: Add the test source to CMakeLists.txt**

Insert `tests/themes.test.cpp` immediately after `tests/text_layout_cache.test.cpp` in the `tapir_tests` source list.

- [ ] **Step 4: Write `tests/themes.test.cpp`**

```cpp
#include <doctest.h>
#include <string_view>
#include "themes.h"

using tapir::Rgb;
using tapir::ThemeMode;
using tapir::themes::ThemePalette;
using tapir::themes::builtinPalette;
using tapir::themes::builtinPresetNames;

TEST_CASE("builtin theme names are the five approved families") {
    CHECK(builtinPresetNames().size() == 5);
    CHECK(builtinPresetNames()[0] == L"nord");
    CHECK(builtinPresetNames()[1] == L"catppuccin");
    CHECK(builtinPresetNames()[2] == L"kanagawa");
    CHECK(builtinPresetNames()[3] == L"material");
    CHECK(builtinPresetNames()[4] == L"gruvbox");
}

TEST_CASE("every preset resolves in dark and light") {
    for (std::wstring_view name : builtinPresetNames()) {
        const auto dark = builtinPalette(name, ThemeMode::Dark);
        const auto light = builtinPalette(name, ThemeMode::Light);
        REQUIRE(dark.has_value());
        REQUIRE(light.has_value());
        CHECK(dark->background != light->background);
        CHECK(dark->foreground != light->foreground);
    }
}

TEST_CASE("preset lookup is case insensitive and rejects unknown names") {
    CHECK(builtinPalette(L"NORD", ThemeMode::Dark).has_value());
    CHECK_FALSE(builtinPalette(L"dracula", ThemeMode::Dark).has_value());
    CHECK_FALSE(builtinPalette(L"", ThemeMode::Light).has_value());
}

TEST_CASE("catppuccin dark keeps the current default background") {
    const auto dark = builtinPalette(L"catppuccin", ThemeMode::Dark);
    REQUIRE(dark.has_value());
    CHECK(dark->background == Rgb{0x2b, 0x2b, 0x2b});
    CHECK(dark->accent == Rgb{0xcb, 0xa6, 0xf7});
}
```

- [ ] **Step 5: Run the failing test, then verify it passes**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected before Step 2: compilation fails on missing `themes.h`. After all steps: all tests pass.

### Task 2: Config keys, resolution and precedence

**Files:**
- Modify: `src/config.h:29-59`
- Modify: `src/config.cpp:284-324`, `src/config.cpp:451-460`

**Interfaces:**
- Consumes: `tapir::themes::builtinPalette`, `tapir::ThemeMode` from Task 1.
- Produces: `Settings::Theme::preset` and `Settings::Theme::mode` for Task 3.

- [ ] **Step 1: Extend the theme model**

`ThemeMode` already exists in `src/config.h` from Task 1. In `Settings::Theme`, after `std::wstring font`, add:

```cpp
        std::wstring preset = L"catppuccin";
        ThemeMode mode = ThemeMode::Dark;
```

- [ ] **Step 2: Parse mode, preset, then colors**

In `src/config.cpp`, add `#include "themes.h"`. In `parseTheme`, immediately after `report("theme.font", status, result);` and before the `readColor` calls, insert:

```cpp
    std::string mode;
    status = get(table, "mode", mode);
    report("theme.mode", status, result);
    if (status == Lookup::Valid) {
        if (iequals(mode, "dark")) settings.theme.mode = ThemeMode::Dark;
        else if (iequals(mode, "light")) settings.theme.mode = ThemeMode::Light;
        else warn(result, "theme.mode");
    }

    std::string preset;
    status = get(table, "preset", preset);
    report("theme.preset", status, result);
    if (status == Lookup::Valid) {
        const auto palette = tapir::themes::builtinPalette(widen(preset), settings.theme.mode);
        if (palette) {
            settings.theme.preset = widen(preset);
            settings.theme.background = palette->background;
            settings.theme.foreground = palette->foreground;
            settings.theme.accent = palette->accent;
            settings.theme.muted = palette->muted;
            settings.theme.surface = palette->surface;
            settings.theme.selection = palette->selection;
        } else {
            warn(result, "theme.preset");
        }
    }
```

Add `"preset", "mode"` to the `checkUnknownKeys` list in `parseTheme`.

- [ ] **Step 3: Document the keys in the default config**

In `config::defaultText()`, add under `[theme]` after `font`:

```
preset = "catppuccin"       # nord, catppuccin, kanagawa, material, or gruvbox
mode = "dark"               # dark or light; explicit colors below override the preset
```

Do not change the six existing color lines.

- [ ] **Step 4: Add config tests**

In `tests/config.test.cpp`, add:

```cpp
TEST_CASE("theme preset and mode resolve before color overrides") {
    const auto result = tapir::config::parse(
        "[theme]\npreset = \"nord\"\nmode = \"light\"\n");
    REQUIRE(result.valid);
    CHECK(result.settings.theme.preset == L"nord");
    CHECK(result.settings.theme.mode == tapir::ThemeMode::Light);
    CHECK(result.settings.theme.background == Rgb{0xec, 0xef, 0xf4});
}

TEST_CASE("explicit color overrides a preset") {
    const auto result = tapir::config::parse(
        "[theme]\npreset = \"gruvbox\"\nmode = \"dark\"\naccent = \"#123456\"\n");
    REQUIRE(result.valid);
    CHECK(result.settings.theme.accent == Rgb{0x12, 0x34, 0x56});
}

TEST_CASE("unknown preset warns and keeps the default") {
    const auto result = tapir::config::parse("[theme]\npreset = \"dracula\"\n");
    CHECK(result.settings.theme.preset == L"catppuccin");
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const auto& d) { return d.severity == Severity::Warning; }));
}
```

Add `#include <algorithm>` if the file does not already have it.

- [ ] **Step 5: Verify**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: all tests pass.

### Task 3: Settings Appearance controls

**Files:**
- Modify: `src/settings_ui.cpp:26-61`, `:220-253`, `:202-218`, `:672-684`

**Interfaces:**
- Consumes: `builtinPresetNames`, `builtinPalette`, `ThemeMode` from Tasks 1-2; existing `hexColor` at line 154.
- Produces: two new config keys written by the existing `accept()` path.

- [ ] **Step 1: Append the two fields**

At the end of `kFields`, after `{"privacy.encrypt_history", ...}`, add:

```cpp
    {"theme.preset", L"Theme", 1, Kind::Cycle},
    {"theme.mode", L"Theme mode", 1, Kind::Cycle},
```

They are indices 24 and 25. Do not insert or reorder any existing entry.

- [ ] **Step 2: Populate their values**

In `setValues`, after `state.values[23] = ...`, add:

```cpp
    state.values[24] = s.theme.preset;
    state.values[25] = s.theme.mode == ThemeMode::Dark ? L"dark" : L"light";
```

- [ ] **Step 3: Provide cycle options**

In `cycleOptions`, add before `default:`:

```cpp
    case 24:
        for (std::wstring_view name : tapir::themes::builtinPresetNames()) {
            out.emplace_back(name);
        }
        break;
    case 25:
        out = {L"dark", L"light"};
        break;
```

- [ ] **Step 4: Refresh the six colors when a theme control changes**

In `cycleField`, after `state.values[field] = options[index];`, insert:

```cpp
    if (field == 24 || field == 25) {
        const ThemeMode mode = state.values[25] == L"light" ? ThemeMode::Light : ThemeMode::Dark;
        const auto palette = tapir::themes::builtinPalette(state.values[24], mode);
        if (palette) {
            state.values[7] = hexColor(palette->background);
            state.values[8] = hexColor(palette->foreground);
            state.values[9] = hexColor(palette->accent);
            state.values[10] = hexColor(palette->muted);
            state.values[11] = hexColor(palette->surface);
            state.values[12] = hexColor(palette->selection);
        }
    }
```

- [ ] **Step 5: Verify**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: build clean, all tests pass. Then open Settings from the tray and confirm the Appearance tab shows Theme and Theme mode controls; cycle each and confirm the six color strings change.

### Task 4: Documentation and final verification

**Files:**
- Modify: `README.md` (default config block and configuration notes)

- [ ] **Step 1: Document the keys**

In the README default config block, add `preset = "catppuccin"` and `mode = "dark"` under `[theme]`, and add a sentence under the fixed-value list stating that `theme.preset` accepts the five names and `theme.mode` accepts `dark` or `light`, and that explicit color keys override the preset.

- [ ] **Step 2: Release build**

Run: `cmake --build build --config Release --target tapir`
Expected: exit 0 with `/W4 /WX` and no warnings. If `LNK1104` occurs because a running `tapir.exe` holds the output, report it rather than killing the process.

- [ ] **Step 3: Visual check**

Using a temporary `TAPIR_CONFIG_DIR`, launch with `preset = "nord", mode = "dark"`, then with `preset = "nord", mode = "light"`, and confirm the popup recolors in each case without a restart. Confirm a config with the original six colors and no preset key still looks unchanged.

- [ ] **Step 4: Report**

Report what changed, test and build results, and any pre-existing issues noticed. Do not commit.

## Self-review

- Spec coverage: palette table → Task 1; config keys and override rule → Task 2; Settings controls → Task 3; documentation and verification → Task 4; high-contrast and rendering untouched by all tasks.
- Placeholder scan: every code step contains the full code; no "similar to" or TODO steps.
- Type consistency: `ThemeMode` is declared once in `config.h` in Task 1 and consumed in Tasks 1-3; `ThemePalette` and both lookup functions keep the same names; field indices 24/25 match in all Settings steps.
