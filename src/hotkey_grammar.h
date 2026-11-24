#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tapir::hotkey {

enum Modifier : unsigned {
    None  = 0,
    Ctrl  = 1,
    Alt   = 2,
    Shift = 4,
    Win   = 8,
};

struct Combination {
    unsigned modifiers = None;
    std::wstring key;
};

std::optional<Combination> parse(std::wstring_view text);

} // namespace tapir::hotkey
