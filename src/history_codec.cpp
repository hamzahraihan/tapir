#include "history_codec.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace tapir::history {
namespace {

constexpr std::string_view kMagic = "TPRH";
constexpr unsigned char kVersion = 1;
constexpr unsigned char kEncryptedFlag = 0x01;
constexpr std::size_t kCountSize = 4;
constexpr std::size_t kRecordHeaderSize = 4 + 8 + 1;

void AppendU32(std::string& bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
}

void AppendU64(std::string& bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
}

bool ReadU32(std::string_view bytes, std::size_t& offset, std::uint32_t& value) {
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

bool ReadU64(std::string_view bytes, std::size_t& offset, std::uint64_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 8) return false;
  value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(
                 static_cast<unsigned char>(bytes[offset + i]))
             << (i * 8);
  }
  offset += 8;
  return true;
}

}  // namespace

std::string EncodePrefix(bool encrypted) {
  std::string bytes(kMagic);
  bytes.push_back(static_cast<char>(kVersion));
  bytes.push_back(static_cast<char>(encrypted ? kEncryptedFlag : 0));
  bytes.push_back('\0');
  bytes.push_back('\0');
  return bytes;
}

bool DecodePrefix(std::string_view bytes, bool& encrypted) {
  if (bytes.size() != kPrefixSize) return false;
  if (bytes.substr(0, kMagic.size()) != kMagic) return false;
  const auto version = static_cast<unsigned char>(bytes[4]);
  if (version != kVersion) return false;
  if (bytes[6] != '\0' || bytes[7] != '\0') return false;
  encrypted = (static_cast<unsigned char>(bytes[5]) & kEncryptedFlag) != 0;
  return true;
}

std::string EncodePayload(const Records& records) {
  std::string bytes;
  std::size_t expected = kCountSize;
  for (const Record& record : records) {
    expected += kRecordHeaderSize + record.text.size() * 2;
  }
  bytes.reserve(expected);

  AppendU32(bytes, static_cast<std::uint32_t>(records.size()));
  for (const Record& record : records) {
    AppendU32(bytes, static_cast<std::uint32_t>(record.text.size()));
    AppendU64(bytes, record.filetime);
    bytes.push_back(record.pinned ? '\1' : '\0');
    for (const wchar_t unit : record.text) {
      const auto value = static_cast<std::uint16_t>(unit);
      bytes.push_back(static_cast<char>(value & 0xFFu));
      bytes.push_back(static_cast<char>((value >> 8) & 0xFFu));
    }
  }
  return bytes;
}

bool DecodePayload(std::string_view bytes, Records& out) {
  std::size_t offset = 0;
  std::uint32_t count = 0;
  if (!ReadU32(bytes, offset, count)) return false;
  if (count > (bytes.size() - offset) / kRecordHeaderSize) return false;

  Records parsed;
  parsed.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t length = 0;
    std::uint64_t filetime = 0;
    if (!ReadU32(bytes, offset, length)) return false;
    if (!ReadU64(bytes, offset, filetime)) return false;
    if (offset >= bytes.size()) return false;
    const bool pinned = bytes[offset++] != '\0';

    const std::size_t remaining = bytes.size() - offset;
    if (length > remaining / 2) return false;
    const std::size_t textBytes = static_cast<std::size_t>(length) * 2;
    std::wstring text;
    text.resize(length);
    for (std::uint32_t j = 0; j < length; ++j) {
      const auto low = static_cast<unsigned char>(bytes[offset + j * 2]);
      const auto high = static_cast<unsigned char>(bytes[offset + j * 2 + 1]);
      text[j] = static_cast<wchar_t>(static_cast<std::uint16_t>(low | (high << 8)));
    }
    offset += textBytes;
    parsed.push_back(Record{std::move(text), filetime, pinned});
  }

  if (offset != bytes.size()) return false;
  out = std::move(parsed);
  return true;
}

}  // namespace tapir::history
