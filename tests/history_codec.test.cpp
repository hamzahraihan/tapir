#include <doctest.h>

#include <string>
#include <string_view>

#include "history_codec.h"

namespace {

std::string PrefixWith(char magicFourth, unsigned char version,
                       unsigned char flags, unsigned char reserved1 = 0,
                       unsigned char reserved2 = 0) {
  std::string bytes("TPRH");
  bytes[3] = magicFourth;
  bytes.push_back(static_cast<char>(version));
  bytes.push_back(static_cast<char>(flags));
  bytes.push_back(static_cast<char>(reserved1));
  bytes.push_back(static_cast<char>(reserved2));
  return bytes;
}

}  // namespace

TEST_CASE("history codec: prefix round-trips the version and encryption flag") {
  for (const bool encrypted : {false, true}) {
    const std::string bytes = tapir::history::EncodePrefix(encrypted);
    REQUIRE_EQ(bytes.size(), 8u);
    CHECK_EQ(bytes.substr(0, 4), "TPRH");
    CHECK_EQ(static_cast<unsigned char>(bytes[4]), 1u);
    CHECK_EQ(static_cast<unsigned char>(bytes[5]), encrypted ? 1u : 0u);
    CHECK_EQ(static_cast<unsigned char>(bytes[6]), 0u);
    CHECK_EQ(static_cast<unsigned char>(bytes[7]), 0u);

    bool decoded = !encrypted;
    REQUIRE(tapir::history::DecodePrefix(bytes, decoded));
    CHECK_EQ(decoded, encrypted);
  }
}

TEST_CASE("history codec: prefix rejects bad magic, version, reserved bytes and truncation") {
  bool encrypted = false;

  CHECK_FALSE(tapir::history::DecodePrefix(PrefixWith('X', 1, 0), encrypted));
  CHECK_FALSE(tapir::history::DecodePrefix(PrefixWith('R', 2, 0), encrypted));
  CHECK_FALSE(tapir::history::DecodePrefix(PrefixWith('H', 1, 0, 1), encrypted));
  CHECK_FALSE(tapir::history::DecodePrefix(PrefixWith('H', 1, 0, 0, 1), encrypted));

  std::string truncated = tapir::history::EncodePrefix(true);
  truncated.resize(7);
  CHECK_FALSE(tapir::history::DecodePrefix(truncated, encrypted));
}

TEST_CASE("history codec: payload round-trips text, FILETIME and pin flag") {
  const tapir::history::Records original{
      {L"hello", 0x01DCA2B3C4D5E6F7ull, false},
      {L"caf\x00E9 pinned", 0x0100000000000001ull, true},
  };

  const std::string bytes = tapir::history::EncodePayload(original);
  tapir::history::Records parsed;
  REQUIRE(tapir::history::DecodePayload(bytes, parsed));
  REQUIRE_EQ(parsed.size(), 2u);
  CHECK_EQ(parsed[0].text, original[0].text);
  CHECK_EQ(parsed[0].filetime, original[0].filetime);
  CHECK_FALSE(parsed[0].pinned);
  CHECK_EQ(parsed[1].text, original[1].text);
  CHECK_EQ(parsed[1].filetime, original[1].filetime);
  CHECK(parsed[1].pinned);
}

TEST_CASE("history codec: payload uses the settled fixed little-endian layout") {
  // count=1 | len=2 | FILETIME=0x0102030405060708 | pinned=1 | "AB" in UTF-16LE.
  constexpr unsigned char expected[] = {
      0x01, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00,
      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
      0x01,
      0x41, 0x00, 0x42, 0x00,
  };
  const std::string expectedBytes(reinterpret_cast<const char*>(expected),
                                  sizeof(expected));

  const tapir::history::Records records{{L"AB", 0x0102030405060708ull, true}};
  CHECK_EQ(tapir::history::EncodePayload(records), expectedBytes);
}

TEST_CASE("history codec: an empty payload round-trips") {
  const std::string bytes = tapir::history::EncodePayload({});
  REQUIRE_EQ(bytes.size(), 4u);
  tapir::history::Records parsed{{L"sentinel", 1, true}};
  REQUIRE(tapir::history::DecodePayload(bytes, parsed));
  CHECK(parsed.empty());
}

TEST_CASE("history codec: truncated and trailing payload bytes are corruption") {
  const tapir::history::Records records{{L"text", 42, false}};
  const std::string bytes = tapir::history::EncodePayload(records);

  std::string truncated = bytes;
  truncated.pop_back();
  tapir::history::Records parsed{{L"sentinel", 7, true}};
  CHECK_FALSE(tapir::history::DecodePayload(truncated, parsed));
  CHECK_EQ(parsed.size(), 1u);
  CHECK_EQ(parsed[0].text, L"sentinel");

  std::string trailing = bytes;
  trailing.push_back('\0');
  CHECK_FALSE(tapir::history::DecodePayload(trailing, parsed));
}

TEST_CASE("history codec: an implausible record count is rejected without allocating") {
  std::string bytes(4, '\0');
  bytes[0] = '\xFF';
  bytes[1] = '\xFF';
  bytes[2] = '\xFF';
  bytes[3] = '\xFF';

  tapir::history::Records parsed{{L"sentinel", 7, true}};
  CHECK_FALSE(tapir::history::DecodePayload(bytes, parsed));
  REQUIRE_EQ(parsed.size(), 1u);
  CHECK_EQ(parsed[0].text, L"sentinel");
}
