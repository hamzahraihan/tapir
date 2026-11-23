#pragma once

#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tapir::icons {

template <typename T>
class Cache {
 public:
  explicit Cache(std::size_t maxEntries) noexcept : maxEntries_(maxEntries) {}

  Cache(const Cache&) = delete;
  Cache& operator=(const Cache&) = delete;

  T* Find(const std::wstring& key) {
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    entries_.splice(entries_.begin(), entries_, it->second);
    return &it->second->value;
  }

  bool Contains(const std::wstring& key) const {
    return index_.find(key) != index_.end();
  }

  void Put(const std::wstring& key, T value) {
    if (maxEntries_ == 0) return;
    const auto it = index_.find(key);
    if (it != index_.end()) {
      it->second->value = std::move(value);
      entries_.splice(entries_.begin(), entries_, it->second);
      return;
    }
    if (index_.size() >= maxEntries_) {
      index_.erase(entries_.back().key);
      entries_.pop_back();
    }
    entries_.push_front(Entry{key, std::move(value)});
    index_.emplace(entries_.front().key, entries_.begin());
  }

  std::size_t Size() const noexcept { return index_.size(); }

  void Clear() noexcept {
    entries_.clear();
    index_.clear();
  }

 private:
  struct Entry {
    std::wstring key;
    T value;
  };

  std::list<Entry> entries_;
  std::unordered_map<std::wstring, typename std::list<Entry>::iterator> index_;
  std::size_t maxEntries_;
};

template <typename CacheT>
std::vector<std::wstring> MissingKeys(const CacheT& cache,
                                      std::span<const std::wstring> keys) {
  std::vector<std::wstring> missing;
  for (const std::wstring& key : keys) {
    if (cache.Contains(key)) continue;
    bool seen = false;
    for (const std::wstring& existing : missing) {
      if (existing == key) {
        seen = true;
        break;
      }
    }
    if (!seen) missing.push_back(key);
  }
  return missing;
}

}  // namespace tapir::icons
