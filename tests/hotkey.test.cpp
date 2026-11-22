#include <doctest.h>
#include <string>
#include "config.h"
#include "hotkey.h"

using tapir::defaultSettings;
using tapir::hotkey::parse;
using tapir::hotkey::Alt;
using tapir::hotkey::Ctrl;
using tapir::hotkey::Shift;

TEST_CASE("hotkey parses the configured default Ctrl+Space") {
    const auto c = parse(defaultSettings().general.hotkey);
    REQUIRE(c.has_value());
    CHECK(c->modifiers == Ctrl);
    CHECK(c->key == L"SPACE");
}

TEST_CASE("hotkey names are case-insensitive") {
    const auto c = parse(L"ctrl+shift+K");
    REQUIRE(c.has_value());
    CHECK(c->modifiers == (Ctrl | Shift));
    CHECK(c->key == L"K");
}

TEST_CASE("hotkey accepts digits and function keys") {
    const auto f1 = parse(L"Win+F1");
    REQUIRE(f1.has_value());
    CHECK(f1->modifiers == tapir::hotkey::Win);
    CHECK(f1->key == L"F1");
    const auto digit = parse(L"Alt+2");
    REQUIRE(digit.has_value());
    CHECK(digit->key == L"2");
}

TEST_CASE("hotkey rejects malformed combinations") {
    CHECK_FALSE(parse(L"Alt").has_value());
    CHECK_FALSE(parse(L"Alt+").has_value());
    CHECK_FALSE(parse(L"Alt+Alt+Space").has_value());
    CHECK_FALSE(parse(L"Alt+K+Space").has_value());
    CHECK_FALSE(parse(L"Alt+Space++K").has_value());
    CHECK_FALSE(parse(L"Foo+Space").has_value());
    CHECK_FALSE(parse(L"Alt+Spacebar").has_value());
}

TEST_CASE("hotkey accepts every function key F1 through F12") {
    for (int n = 1; n <= 12; ++n) {
        const auto c = parse(L"Alt+F" + std::to_wstring(n));
        CHECK(c.has_value());
        if (c.has_value()) {
            CHECK(c->key == L"F" + std::to_wstring(n));
        }
    }
}

TEST_CASE("hotkey rejects out-of-range and malformed function keys") {
    CHECK_FALSE(parse(L"Alt+F0").has_value());
    CHECK_FALSE(parse(L"Alt+F13").has_value());
    CHECK_FALSE(parse(L"Alt+F0:").has_value());
}
