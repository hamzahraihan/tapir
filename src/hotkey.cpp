#include "hotkey.h"

#include <algorithm>
#include <cwctype>

namespace tapir::hotkey {
namespace {

bool asciiEquals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

std::optional<unsigned> modifierBit(std::wstring_view token) {
    if (asciiEquals(token, L"ctrl")) return Ctrl;
    if (asciiEquals(token, L"alt")) return Alt;
    if (asciiEquals(token, L"shift")) return Shift;
    if (asciiEquals(token, L"win")) return Win;
    return std::nullopt;
}

bool validKey(std::wstring_view key) {
    if (key.size() == 1) {
        const wchar_t c = static_cast<wchar_t>(std::towupper(key.front()));
        return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
    }
    if (key.size() == 2 || key.size() == 3) {
        if (!asciiEquals(key.substr(0, 1), L"f")) return false;
        const int n = (key[1] - L'0') * 10 + (key.size() == 3 ? key[2] - L'0' : 0);
        return n >= 1 && n <= 12;
    }
    return asciiEquals(key, L"space");
}

std::wstring normalizeKey(std::wstring_view key) {
    std::wstring out(key);
    for (wchar_t& c : out) c = static_cast<wchar_t>(std::towupper(c));
    return out;
}

UINT nativeKey(const std::wstring& key) {
    if (key == L"SPACE") return VK_SPACE;
    if (key[0] == L'F') {
        int n = 0;
        for (std::size_t i = 1; i < key.size(); ++i) n = n * 10 + (key[i] - L'0');
        return static_cast<UINT>(VK_F1 + n - 1);
    }
    return static_cast<UINT>(key.front());
}

} // namespace

std::optional<Combination> parse(std::wstring_view text) {
    Combination result;
    bool haveKey = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(L'+', start);
        const std::wstring_view token =
            text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        if (token.empty()) return std::nullopt;

        if (const auto bit = modifierBit(token)) {
            if ((result.modifiers & *bit) != 0) return std::nullopt;
            result.modifiers |= *bit;
        } else if (!haveKey && validKey(token)) {
            result.key = normalizeKey(token);
            haveKey = true;
        } else {
            return std::nullopt;
        }

        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    if (!haveKey) return std::nullopt;
    return result;
}

bool registerHotkey(HWND hwnd, int id, const Combination& combo) {
    UINT nativeModifiers = 0;
    if (combo.modifiers & Ctrl)  nativeModifiers |= MOD_CONTROL;
    if (combo.modifiers & Alt)   nativeModifiers |= MOD_ALT;
    if (combo.modifiers & Shift) nativeModifiers |= MOD_SHIFT;
    if (combo.modifiers & Win)   nativeModifiers |= MOD_WIN;
    return RegisterHotKey(hwnd, id, nativeModifiers, nativeKey(combo.key)) != 0;
}

void unregisterHotkey(HWND hwnd, int id) {
    UnregisterHotKey(hwnd, id);
}

} // namespace tapir::hotkey
