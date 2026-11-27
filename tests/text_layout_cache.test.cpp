#include <doctest.h>
#include "util/text_layout_cache.h"

using tapir::util::BoundedTextLayoutCache;
using tapir::util::TextFormatId;
using tapir::util::TextLayoutKey;

namespace {

TextLayoutKey key(TextFormatId format, std::wstring text, float width,
                  float height, bool trim = true) {
    return TextLayoutKey{format, std::move(text), width, height, trim};
}

} // namespace

TEST_CASE("layout key distinguishes format, text, width, height and trim") {
    const auto base = key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f);
    CHECK(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::titleStrong, L"Visual Studio Code", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 401.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 25.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f, false));
}

TEST_CASE("cache returns the inserted value for an identical key") {
    BoundedTextLayoutCache<int> cache;
    cache.insert(key(TextFormatId::subtitle, L"App", 100.0f, 20.0f), 42);
    const int* found = cache.find(key(TextFormatId::subtitle, L"App", 100.0f, 20.0f));
    REQUIRE(found != nullptr);
    CHECK(*found == 42);
    CHECK(cache.find(key(TextFormatId::label, L"App", 100.0f, 20.0f)) == nullptr);
}

TEST_CASE("cache clears every entry when the 512 entry cap is reached") {
    BoundedTextLayoutCache<int> cache;
    for (std::size_t i = 0; i < BoundedTextLayoutCache<int>::kMaxEntries; ++i) {
        cache.insert(key(TextFormatId::text, std::to_wstring(i), 100.0f, 20.0f),
                     static_cast<int>(i));
    }
    CHECK(cache.size() == 512);
    cache.insert(key(TextFormatId::text, L"overflow", 100.0f, 20.0f), 999);
    CHECK(cache.size() == 1);
    CHECK(cache.find(key(TextFormatId::text, L"0", 100.0f, 20.0f)) == nullptr);
    const int* found = cache.find(key(TextFormatId::text, L"overflow", 100.0f, 20.0f));
    REQUIRE(found != nullptr);
    CHECK(*found == 999);
}

TEST_CASE("explicit clear removes all entries") {
    BoundedTextLayoutCache<int> cache;
    cache.insert(key(TextFormatId::text, L"a", 10.0f, 10.0f), 1);
    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(cache.find(key(TextFormatId::text, L"a", 10.0f, 10.0f)) == nullptr);
}
