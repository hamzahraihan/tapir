#pragma once

#include <string>
#include <string_view>

namespace tapir::util::startup {

// Pure: returns the quoted REG_SZ payload for the Run value. No arguments are
// appended; the app starts hidden and the single-instance mutex dedupes.
std::wstring commandLine(std::wstring_view exePath);

// Applies the setting and logs a reason-only warning when the registry write fails.
void applyFromSettings(bool enabled);

// Idempotently creates or removes the per-user Run value. On failure, `error`
// receives a short reason only, never a path or clipboard content.
bool setEnabled(bool enabled, std::wstring* error = nullptr);

} // namespace tapir::util::startup
