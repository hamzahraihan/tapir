#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tapir::history {

struct Record {
  std::wstring text;
  std::uint64_t filetime = 0;
  bool pinned = false;
};

using Records = std::vector<Record>;

inline constexpr std::size_t kPrefixSize = 8;

// Plaintext prefix: "TPRH" | version 1 | flags (bit 0 = payload encrypted) |
// two reserved zero bytes. Bits 1-7 of the flag byte are unspecified by the
// settled format and are ignored on read.
std::string EncodePrefix(bool encrypted);
bool DecodePrefix(std::string_view bytes, bool& encrypted);

// Payload is little-endian: u32 count, then per record u32 UTF-16 code-unit
// length, u64 FILETIME, one pinned byte, and UTF-16LE text with no NUL.
std::string EncodePayload(const Records& records);
bool DecodePayload(std::string_view bytes, Records& out);

}  // namespace tapir::history
