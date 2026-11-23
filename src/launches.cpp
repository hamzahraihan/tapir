#include "launches.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace tapir::launches {
namespace {

constexpr std::string_view kMagic = "TAPIRLSH";
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kRecordHeaderSize = 8;

void AppendU32(std::string& bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
}

bool ReadU32(std::string_view bytes, std::size_t& offset,
             std::uint32_t& value) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < 4) return false;
  value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(bytes[offset + i]))
             << (i * 8);
  }
  offset += 4;
  return true;
}

}  // namespace

std::string Serialize(const Table& table) {
  std::string bytes;
  bytes.reserve(kHeaderSize + table.size() * kRecordHeaderSize);
  bytes.append(kMagic);
  AppendU32(bytes, kVersion);
  AppendU32(bytes, static_cast<std::uint32_t>(table.size()));
  for (const Record& record : table) {
    AppendU32(bytes, static_cast<std::uint32_t>(record.key.size()));
    AppendU32(bytes, record.count);
    for (const wchar_t unit : record.key) {
      const auto value = static_cast<std::uint16_t>(unit);
      bytes.push_back(static_cast<char>(value & 0xFFu));
      bytes.push_back(static_cast<char>((value >> 8) & 0xFFu));
    }
  }
  return bytes;
}

bool Parse(std::string_view bytes, Table& out) {
  if (bytes.size() < kHeaderSize) return false;
  if (bytes.substr(0, kMagic.size()) != kMagic) return false;

  std::size_t offset = kMagic.size();
  std::uint32_t version = 0;
  std::uint32_t recordCount = 0;
  if (!ReadU32(bytes, offset, version) || version != kVersion) return false;
  if (!ReadU32(bytes, offset, recordCount)) return false;

  // Every record needs at least a key-length and a count field. Guarding here
  // prevents a corrupt recordCount from driving a huge reserve/allocation.
  if (recordCount > (bytes.size() - offset) / kRecordHeaderSize) return false;

  Table parsed;
  parsed.reserve(recordCount);
  for (std::uint32_t i = 0; i < recordCount; ++i) {
    std::uint32_t keyLength = 0;
    std::uint32_t count = 0;
    if (!ReadU32(bytes, offset, keyLength)) return false;
    if (!ReadU32(bytes, offset, count)) return false;

    const std::size_t keyBytes = static_cast<std::size_t>(keyLength) * 2;
    if (keyBytes > bytes.size() - offset) return false;

    std::wstring key;
    key.resize(keyLength);
    for (std::uint32_t j = 0; j < keyLength; ++j) {
      const auto low = static_cast<unsigned char>(bytes[offset + j * 2]);
      const auto high = static_cast<unsigned char>(bytes[offset + j * 2 + 1]);
      key[j] = static_cast<wchar_t>(
          static_cast<std::uint16_t>(low | (high << 8)));
    }
    offset += keyBytes;
    parsed.push_back(Record{std::move(key), count});
  }

  if (offset != bytes.size()) return false;
  out = std::move(parsed);
  return true;
}

std::uint32_t Increment(std::uint32_t count) noexcept {
  return count == std::numeric_limits<std::uint32_t>::max()
             ? count
             : count + 1;
}

}  // namespace tapir::launches
