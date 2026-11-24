#pragma once

#include "hotkey_grammar.h"

#include <windows.h>

namespace tapir::hotkey {

bool registerHotkey(HWND hwnd, int id, const Combination& combo);
void unregisterHotkey(HWND hwnd, int id);

} // namespace tapir::hotkey
