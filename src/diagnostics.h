#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tapir::diagnostics {

inline constexpr std::size_t kLogMaxBytes = 64 * 1024;

std::string capOldestHalf(std::string_view content,
                          std::size_t limitBytes = kLogMaxBytes);

void append(const std::wstring& logPath, std::string_view line);

} // namespace tapir::diagnostics
