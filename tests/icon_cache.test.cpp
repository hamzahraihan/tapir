#include <doctest.h>

#include <cstddef>
#include <span>
#include <string>
#include <unknwn.h>
#include <vector>
#include <wrl/client.h>

#include "icon_cache.h"

namespace {

using tapir::icons::Cache;
using tapir::icons::MissingKeys;

struct FakeUnknown : IUnknown {
  ULONG refs = 1;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
    *ppv = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs;
    if (remaining == 0) delete this;
    return remaining;
  }
};

}  // namespace

TEST_CASE("icons: a new cache is empty") {
  Cache<int> cache(4);
  CHECK_EQ(cache.Size(), 0u);
  CHECK(cache.Find(L"missing") == nullptr);
}

TEST_CASE("icons: Put then Find returns the stored value") {
  Cache<int> cache(4);
  cache.Put(L"alpha", 42);
  int* value = cache.Find(L"alpha");
  REQUIRE(value != nullptr);
  CHECK_EQ(*value, 42);
  CHECK_EQ(cache.Size(), 1u);
}

TEST_CASE("icons: Put evicts the least recently used entry at the limit") {
  Cache<int> cache(2);
  cache.Put(L"a", 1);
  cache.Put(L"b", 2);
  cache.Put(L"c", 3);
  CHECK_EQ(cache.Size(), 2u);
  CHECK(cache.Find(L"a") == nullptr);
  CHECK(cache.Find(L"b") != nullptr);
  CHECK(cache.Find(L"c") != nullptr);
}

TEST_CASE("icons: Find refreshes recency so an older entry is not evicted") {
  Cache<int> cache(2);
  cache.Put(L"a", 1);
  cache.Put(L"b", 2);
  REQUIRE(cache.Find(L"a") != nullptr);
  cache.Put(L"c", 3);
  CHECK(cache.Find(L"b") == nullptr);
  CHECK(cache.Find(L"a") != nullptr);
  CHECK(cache.Find(L"c") != nullptr);
}

TEST_CASE("icons: updating an existing key keeps the entry and refreshes recency") {
  Cache<int> cache(2);
  cache.Put(L"a", 1);
  cache.Put(L"b", 2);
  cache.Put(L"a", 9);
  CHECK_EQ(cache.Size(), 2u);
  int* value = cache.Find(L"a");
  REQUIRE(value != nullptr);
  CHECK_EQ(*value, 9);
  cache.Put(L"c", 3);
  CHECK(cache.Find(L"b") == nullptr);
  CHECK(cache.Find(L"a") != nullptr);
}

TEST_CASE("icons: a zero-capacity cache never stores entries") {
  Cache<int> cache(0);
  cache.Put(L"a", 1);
  CHECK_EQ(cache.Size(), 0u);
  CHECK(cache.Find(L"a") == nullptr);
}

TEST_CASE("icons: Clear removes all entries") {
  Cache<int> cache(4);
  cache.Put(L"a", 1);
  cache.Put(L"b", 2);
  cache.Clear();
  CHECK_EQ(cache.Size(), 0u);
  CHECK(cache.Find(L"a") == nullptr);
  CHECK(cache.Find(L"b") == nullptr);
}

TEST_CASE("icons: cached ComPtr stays valid across repeated finds") {
  Cache<Microsoft::WRL::ComPtr<IUnknown>> cache(4);
  Microsoft::WRL::ComPtr<IUnknown> value;
  value.Attach(new FakeUnknown());
  cache.Put(L"a", value);
  for (int i = 0; i < 3; ++i) {
    auto* found = cache.Find(L"a");
    REQUIRE(found != nullptr);
    CHECK(found->Get() != nullptr);
  }
}

TEST_CASE("icons: MissingKeys returns unique uncached keys in first-appearance order") {
  Cache<int> cache(4);
  cache.Put(L"b", 2);
  const std::wstring keys[] = {L"a", L"b", L"a", L"c", L"b", L"d"};
  const std::vector<std::wstring> missing = MissingKeys(cache, keys);
  REQUIRE_EQ(missing.size(), 3u);
  CHECK_EQ(missing[0], L"a");
  CHECK_EQ(missing[1], L"c");
  CHECK_EQ(missing[2], L"d");
}

TEST_CASE("icons: MissingKeys returns empty for empty input") {
  Cache<int> cache(4);
  const std::span<const std::wstring> empty{};
  CHECK(MissingKeys(cache, empty).empty());
}

TEST_CASE("icons: scrolling loads only the newly visible keys") {
  Cache<int> cache(8);
  const std::wstring first[] = {L"a", L"b", L"c", L"d"};
  const std::vector<std::wstring> firstMissing = MissingKeys(cache, first);
  REQUIRE_EQ(firstMissing.size(), 4u);
  for (const std::wstring& key : firstMissing) cache.Put(key, 1);

  const std::wstring scrolled[] = {L"c", L"d", L"e", L"f"};
  const std::vector<std::wstring> scrolledMissing = MissingKeys(cache, scrolled);
  REQUIRE_EQ(scrolledMissing.size(), 2u);
  CHECK_EQ(scrolledMissing[0], L"e");
  CHECK_EQ(scrolledMissing[1], L"f");
}
