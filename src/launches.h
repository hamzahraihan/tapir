#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tapir::launches {

struct Record {
  std::wstring key;
  std::uint32_t count = 0;
};

using Table = std::vector<Record>;

std::string Serialize(const Table& table);

// Returns false for any corrupt, truncated, or unsupported-version input and
// leaves `out` unchanged. Returns true for a valid (possibly empty) table.
bool Parse(std::string_view bytes, Table& out);

std::uint32_t Increment(std::uint32_t count) noexcept;

}  // namespace tapir::launches
