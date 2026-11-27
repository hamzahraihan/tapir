#include <doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include "themes.h"

using tapir::Rgb;
using tapir::ThemeMode;
using tapir::themes::ThemePalette;
using tapir::themes::builtinPalette;
using tapir::themes::builtinPresetNames;

namespace {

double luminance(Rgb color) {
    const auto channel = [](std::uint8_t value) {
        const double s = static_cast<double>(value) / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) +
           0.0722 * channel(color.b);
}

double contrast(Rgb a, Rgb b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

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

TEST_CASE("muted text clears WCAG AA on every preset background") {
    for (std::wstring_view name : builtinPresetNames()) {
        for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
            const auto palette = builtinPalette(name, mode);
            REQUIRE(palette.has_value());
            CHECK(contrast(palette->muted, palette->background) >= 4.5);
        }
    }
}

TEST_CASE("catppuccin dark keeps the current default background") {
    const auto dark = builtinPalette(L"catppuccin", ThemeMode::Dark);
    REQUIRE(dark.has_value());
    CHECK(dark->background == Rgb{0x2b, 0x2b, 0x2b});
    CHECK(dark->accent == Rgb{0xcb, 0xa6, 0xf7});
}
