#include <doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "index_cache.h"

using tapir::index_cache::Decode;
using tapir::index_cache::Encode;
using tapir::index_cache::Record;

namespace {

Record sample(std::wstring name, std::uint32_t count) {
    Record record;
    record.name = std::move(name);
    record.key = L"aumid:" + record.name;
    record.target = L"shell:AppsFolder\\" + record.name;
    record.arguments = L"--flag \"quoted\"";
    record.workingDirectory = L"C:\\Program Files\\App";
    record.aumid = record.name + L"!app";
    record.launchCount = count;
    return record;
}

} // namespace

TEST_CASE("empty snapshot round-trips") {
    const std::string bytes = Encode({});
    std::vector<Record> decoded;
    CHECK(Decode(bytes, decoded));
    CHECK(decoded.empty());
}

TEST_CASE("records round-trip with unicode and launch counts") {
    const std::vector<Record> records{sample(L"Visual Studio Code", 7),
                                      sample(L"\u30d5\u30a1\u30a4\u30eb\u30de\u30cd\u30fc\u30b8\u30e3\u30fc", 0)};
    const std::string bytes = Encode(records);
    std::vector<Record> decoded;
    REQUIRE(Decode(bytes, decoded));
    REQUIRE(decoded.size() == 2);
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(decoded[i].name == records[i].name);
        CHECK(decoded[i].key == records[i].key);
        CHECK(decoded[i].target == records[i].target);
        CHECK(decoded[i].arguments == records[i].arguments);
        CHECK(decoded[i].workingDirectory == records[i].workingDirectory);
        CHECK(decoded[i].aumid == records[i].aumid);
        CHECK(decoded[i].launchCount == records[i].launchCount);
    }
}

TEST_CASE("malformed bytes are rejected") {
    std::vector<Record> decoded;
    CHECK_FALSE(Decode(std::string_view(), decoded));
    CHECK_FALSE(Decode(std::string_view("TP", 2), decoded));
    CHECK_FALSE(Decode(std::string_view("XXXX\x01\x00\x00\x00\x00\x00\x00\x00", 13), decoded));
    const std::string badVersion("TPIR\x02\x00\x00\x00\x00\x00\x00\x00", 12);
    CHECK_FALSE(Decode(badVersion, decoded));
    const std::vector<Record> one{sample(L"app", 1)};
    const std::string truncated = Encode(one).substr(0, 20);
    CHECK_FALSE(Decode(truncated, decoded));
    const std::string huge("TPIR\x01\x00\x00\x00\x01\x00\x00\x00\xff\xff\x00\x00", 16);
    CHECK_FALSE(Decode(huge, decoded));
}

TEST_CASE("decode clears previous output on failure") {
    std::vector<Record> decoded{sample(L"old", 1)};
    CHECK_FALSE(Decode(std::string_view("garbage"), decoded));
    CHECK(decoded.empty());
}
