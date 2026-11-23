#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tapir::icons {

// Small LRU cache. Entries are never moved after insertion: a stamp records
// recency instead of reordering, so a returned T* stays valid until erased.
template <typename T>
class Cache {
 public:
  explicit Cache(std::size_t maxEntries) noexcept : maxEntries_(maxEntries) {}

  Cache(const Cache&) = delete;
  Cache& operator=(const Cache&) = delete;

  T* Find(const std::wstring& key) {
    for (Entry& entry : entries_) {
      if (entry.key == key) {
        entry.used = ++clock_;
        return std::addressof(entry.value);
      }
    }
    return nullptr;
  }

  bool Contains(const std::wstring& key) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& entry) { return entry.key == key; });
  }

  void Put(const std::wstring& key, T value) {
    if (maxEntries_ == 0) return;
    for (Entry& entry : entries_) {
      if (entry.key == key) {
        entry.value = std::move(value);
        entry.used = ++clock_;
        return;
      }
    }
    if (entries_.size() >= maxEntries_) {
      const auto victim = std::min_element(
          entries_.begin(), entries_.end(),
          [](const Entry& a, const Entry& b) { return a.used < b.used; });
      entries_.erase(victim);
    }
    entries_.push_back(Entry{key, std::move(value), ++clock_});
  }

  std::size_t Size() const noexcept { return entries_.size(); }

  void Clear() noexcept { entries_.clear(); }

 private:
  struct Entry {
    std::wstring key;
    T value;
    std::uint64_t used;
  };

  std::list<Entry> entries_;
  std::size_t maxEntries_;
  std::uint64_t clock_ = 0;
};

template <typename CacheT>
std::vector<std::wstring> MissingKeys(const CacheT& cache,
                                      std::span<const std::wstring> keys) {
  std::vector<std::wstring> missing;
  for (const std::wstring& key : keys) {
    if (cache.Contains(key)) continue;
    if (std::find(missing.begin(), missing.end(), key) == missing.end()) {
      missing.push_back(key);
    }
  }
  return missing;
}

}  // namespace tapir::icons
