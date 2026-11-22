#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

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

inline constexpr std::wstring_view kDefaultHotkey = L"Alt+Space";

std::optional<Combination> parse(std::wstring_view text);
bool registerHotkey(HWND hwnd, int id, const Combination& combo);
void unregisterHotkey(HWND hwnd, int id);

} // namespace tapir::hotkey
