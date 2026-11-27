#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace tapir::util {

enum class TextFormatId : unsigned char {
    text,
    placeholder,
    glyph,
    title,
    titleStrong,
    subtitle,
    label,
    key,
    details,
};

struct TextLayoutKey {
    TextFormatId format;
    std::wstring text;
    float maxWidthPx;
    float heightPx;
    bool trim;

    bool operator==(const TextLayoutKey& other) const noexcept {
        return format == other.format && text == other.text &&
               maxWidthPx == other.maxWidthPx && heightPx == other.heightPx &&
               trim == other.trim;
    }
};

struct TextLayoutKeyHash {
    std::size_t operator()(const TextLayoutKey& key) const noexcept {
        std::size_t hash = std::hash<unsigned char>{}(static_cast<unsigned char>(key.format));
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        };
        for (wchar_t ch : key.text) mix(std::hash<wchar_t>{}(ch));
        mix(std::hash<float>{}(key.maxWidthPx));
        mix(std::hash<float>{}(key.heightPx));
        mix(std::hash<bool>{}(key.trim));
        return hash;
    }
};

template <typename Value>
class BoundedTextLayoutCache {
public:
    static constexpr std::size_t kMaxEntries = 512;

    Value* find(const TextLayoutKey& key) noexcept {
        const auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : &it->second;
    }

    void insert(const TextLayoutKey& key, Value value) {
        if (entries_.size() >= kMaxEntries) entries_.clear();
        entries_.insert_or_assign(key, std::move(value));
    }

    void clear() noexcept { entries_.clear(); }
    std::size_t size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<TextLayoutKey, Value, TextLayoutKeyHash> entries_;
};

} // namespace tapir::util
