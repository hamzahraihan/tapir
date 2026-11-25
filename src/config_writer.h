#pragma once

#include <span>
#include <string>
#include <string_view>

namespace tapir::config_writer {

// A single dotted configuration key and the TOML literal that should replace it.
// For example {"general.animations", "false"} or {"theme.accent", "\"#cba6f7\""}.
struct Value {
    std::string key;
    std::string text;
};

// Returns `original` with each dotted key's value replaced in place. Comments,
// blank lines, unknown keys, key order, indentation, and line endings elsewhere
// are preserved. Missing keys are appended to their table; missing tables are
// created at the end of the file. Pure: no I/O and no global state.
std::string patch(std::string_view original, std::span<const Value> values);

} // namespace tapir::config_writer
