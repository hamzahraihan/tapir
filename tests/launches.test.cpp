#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "launches.h"

namespace {

using tapir::launches::Record;
using tapir::launches::Table;

void AppendU32(std::string& bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
}

std::string Header(std::uint32_t recordCount,
                   std::uint32_t version = 1,
                   std::string_view magic = "TAPIRLSH") {
  std::string bytes(magic);
  AppendU32(bytes, version);
  AppendU32(bytes, recordCount);
  return bytes;
}

void AppendRecord(std::string& bytes, std::wstring_view key,
                  std::uint32_t count) {
  AppendU32(bytes, static_cast<std::uint32_t>(key.size()));
  AppendU32(bytes, count);
  for (const wchar_t unit : key) {
    const auto value = static_cast<std::uint16_t>(unit);
    bytes.push_back(static_cast<char>(value & 0xFFu));
    bytes.push_back(static_cast<char>((value >> 8) & 0xFFu));
  }
}

Table Sentinel() { return Table{{L"sentinel", 99}}; }

void CheckUnchanged(const Table& table) {
  REQUIRE_EQ(table.size(), 1u);
  CHECK_EQ(table[0].key, L"sentinel");
  CHECK_EQ(table[0].count, 99u);
}

}  // namespace

TEST_CASE("launches: round-trip preserves keys and counts") {
  const Table original{{L"Visual Studio Code", 7}, {L"Caf\x00E9", 3}};
  const std::string bytes = tapir::launches::Serialize(original);

  Table parsed;
  REQUIRE(tapir::launches::Parse(bytes, parsed));
  REQUIRE_EQ(parsed.size(), 2u);
  CHECK_EQ(parsed[0].key, L"Visual Studio Code");
  CHECK_EQ(parsed[0].count, 7u);
  CHECK_EQ(parsed[1].key, L"Caf\x00E9");
  CHECK_EQ(parsed[1].count, 3u);
}

TEST_CASE("launches: empty table round-trips") {
  const std::string bytes = tapir::launches::Serialize(Table{});
  CHECK_EQ(bytes.size(), 16u);

  Table parsed = Sentinel();
  REQUIRE(tapir::launches::Parse(bytes, parsed));
  CHECK(parsed.empty());
}

TEST_CASE("launches: truncated bytes are rejected without changing output") {
  std::string bytes = Header(1);
  AppendRecord(bytes, L"app", 5);

  for (const std::size_t length : {0u, 8u, 15u, 17u, 20u, 23u}) {
    if (length >= bytes.size()) continue;
    Table parsed = Sentinel();
    CHECK_FALSE(tapir::launches::Parse(std::string_view(bytes).substr(0, length),
                                       parsed));
    CheckUnchanged(parsed);
  }
}

TEST_CASE("launches: bad magic is rejected without changing output") {
  const std::string bytes = Header(0, 1, "TAPIRLSX");
  Table parsed = Sentinel();
  CHECK_FALSE(tapir::launches::Parse(bytes, parsed));
  CheckUnchanged(parsed);
}

TEST_CASE("launches: bad version is rejected without changing output") {
  const std::string bytes = Header(0, 2);
  Table parsed = Sentinel();
  CHECK_FALSE(tapir::launches::Parse(bytes, parsed));
  CheckUnchanged(parsed);
}

TEST_CASE("launches: oversized declared key length is rejected") {
  std::string bytes = Header(1);
  AppendU32(bytes, std::numeric_limits<std::uint32_t>::max());
  AppendU32(bytes, 5);
  bytes.push_back('a');

  Table parsed = Sentinel();
  CHECK_FALSE(tapir::launches::Parse(bytes, parsed));
  CheckUnchanged(parsed);
}

TEST_CASE("launches: trailing bytes are rejected") {
  std::string bytes = Header(0);
  bytes.push_back('x');

  Table parsed = Sentinel();
  CHECK_FALSE(tapir::launches::Parse(bytes, parsed));
  CheckUnchanged(parsed);
}

TEST_CASE("launches: Increment starts at one and saturates") {
  CHECK_EQ(tapir::launches::Increment(0), 1u);
  CHECK_EQ(tapir::launches::Increment(41), 42u);
  CHECK_EQ(tapir::launches::Increment(std::numeric_limits<std::uint32_t>::max()),
           std::numeric_limits<std::uint32_t>::max());
}
