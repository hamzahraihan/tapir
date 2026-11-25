#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tapir::index_cache {

struct Record {
    std::wstring name;
    std::wstring key;
    std::wstring target;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring aumid;
    std::uint32_t launchCount = 0;
};

std::string Encode(std::span<const Record> records);
bool Decode(std::string_view bytes, std::vector<Record>& out);

} // namespace tapir::index_cache
