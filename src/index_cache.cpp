#include "index_cache.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace tapir::index_cache {
namespace {

constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxBytes = 16u * 1024u * 1024u;
constexpr std::uint32_t kMaxUnits = 65536;
constexpr std::uint32_t kMaxRecords = 100000;

void appendU32(std::string& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

bool readU32(std::string_view bytes, std::size_t& position, std::uint32_t& value) {
    if (position > bytes.size() || bytes.size() - position < 4) return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(bytes[position + shift / 8]))
                 << shift;
    }
    position += 4;
    return true;
}

void appendString(std::string& out, const std::wstring& value) {
    const std::uint32_t units = static_cast<std::uint32_t>(
        value.size() > kMaxUnits ? kMaxUnits : value.size());
    appendU32(out, units);
    if (units != 0) {
        out.append(reinterpret_cast<const char*>(value.data()),
                   static_cast<std::size_t>(units) * sizeof(wchar_t));
    }
}

bool readString(std::string_view bytes, std::size_t& position, std::wstring& value) {
    std::uint32_t units = 0;
    if (!readU32(bytes, position, units) || units > kMaxUnits) return false;
    const std::size_t needed = static_cast<std::size_t>(units) * sizeof(wchar_t);
    if (position > bytes.size() || bytes.size() - position < needed) return false;
    value.assign(units, L'\0');
    if (needed != 0) {
        std::memcpy(value.data(), bytes.data() + position, needed);
    }
    position += needed;
    return true;
}

}

std::string Encode(std::span<const Record> records) {
    std::string out;
    out.append("TPIR", 4);
    appendU32(out, kVersion);
    appendU32(out, static_cast<std::uint32_t>(records.size()));
    for (const Record& record : records) {
        appendU32(out, record.launchCount);
        appendString(out, record.name);
        appendString(out, record.key);
        appendString(out, record.target);
        appendString(out, record.arguments);
        appendString(out, record.workingDirectory);
        appendString(out, record.aumid);
    }
    return out;
}

bool Decode(std::string_view bytes, std::vector<Record>& out) {
    out.clear();
    if (bytes.size() < 12 || bytes.size() > kMaxBytes ||
        bytes.substr(0, 4) != std::string_view("TPIR", 4)) {
        return false;
    }
    std::size_t position = 4;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!readU32(bytes, position, version) || version != kVersion ||
        !readU32(bytes, position, count) || count > kMaxRecords) {
        return false;
    }
    std::vector<Record> decoded;
    const std::size_t maxBySize = (bytes.size() - position) / 28;
    decoded.reserve(std::min<std::size_t>(count, maxBySize));
    for (std::uint32_t i = 0; i < count; ++i) {
        Record record;
        if (!readU32(bytes, position, record.launchCount) ||
            !readString(bytes, position, record.name) ||
            !readString(bytes, position, record.key) ||
            !readString(bytes, position, record.target) ||
            !readString(bytes, position, record.arguments) ||
            !readString(bytes, position, record.workingDirectory) ||
            !readString(bytes, position, record.aumid)) {
            return false;
        }
        decoded.push_back(std::move(record));
    }
    if (position != bytes.size()) return false;
    out = std::move(decoded);
    return true;
}

} // namespace tapir::index_cache
