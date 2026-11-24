#include <doctest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "clipboard.h"
#include "config.h"

namespace {

using tapir::clipboard::Entry;
using tapir::clipboard::EvaluatePrivacy;
using tapir::clipboard::History;
using tapir::clipboard::IsIgnored;
using tapir::clipboard::Privacy;
using tapir::clipboard::PrivacyFacts;

tapir::Settings::History Limits(std::size_t maxEntries, std::size_t maxEntryBytes) {
  tapir::Settings::History limits;
  limits.maxEntries = static_cast<int>(maxEntries);
  limits.maxEntryBytes = static_cast<int>(maxEntryBytes);
  return limits;
}

std::wstring PreviewOf(const History& history, std::size_t index) {
  const Entry* entry = history.EntryAt(index);
  return entry == nullptr ? std::wstring() : entry->preview;
}

}  // namespace

TEST_CASE("clipboard: privacy gate blocks when the exclude format is present") {
  PrivacyFacts facts;
  facts.excludePresent = true;
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);

  // Even an explicit allow from CanIncludeInClipboardHistory cannot override it.
  facts.canIncludePresent = true;
  facts.canIncludeValueValid = true;
  facts.canIncludeValue = 1;
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);
}

TEST_CASE("clipboard: privacy gate honours CanIncludeInClipboardHistory") {
  PrivacyFacts facts;
  facts.canIncludePresent = true;
  facts.canIncludeValueValid = true;
  facts.canIncludeValue = 0;
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);

  facts.canIncludeValue = 1;
  CHECK(EvaluatePrivacy(facts) == Privacy::Allow);
}

TEST_CASE("clipboard: privacy gate fails closed on an unreadable history flag") {
  PrivacyFacts facts;
  facts.canIncludePresent = true;
  facts.canIncludeValueValid = false;  // could not read the DWORD
  facts.canIncludeValue = 1;           // value would allow, but it is invalid
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);
}

TEST_CASE("clipboard: privacy gate fails closed on an unexpected history flag value") {
  PrivacyFacts facts;
  facts.canIncludePresent = true;
  facts.canIncludeValueValid = true;

  facts.canIncludeValue = 2;  // not a documented 0/1 value
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);

  facts.canIncludeValue = 0xFFFFFFFFu;  // any other value must also block
  CHECK(EvaluatePrivacy(facts) == Privacy::Block);

  facts.canIncludeValue = 1;  // the only value that allows
  CHECK(EvaluatePrivacy(facts) == Privacy::Allow);
}

TEST_CASE("clipboard: privacy gate allows a plain payload with no special formats") {
  PrivacyFacts facts;  // nothing present
  CHECK(EvaluatePrivacy(facts) == Privacy::Allow);
}

TEST_CASE("clipboard: ignore list matches executable names case-insensitively") {
  const std::vector<std::wstring> ignore{L"KeePass.exe", L"1Password.exe"};
  CHECK(IsIgnored(L"keepass.exe", ignore));
  CHECK(IsIgnored(L"KEEPASS.EXE", ignore));
  CHECK(IsIgnored(L"1password.exe", ignore));
  CHECK_FALSE(IsIgnored(L"notepad.exe", ignore));
  CHECK_FALSE(IsIgnored(L"keepass.exe", {}));  // empty list ignores nothing
}

TEST_CASE("clipboard: a fresh copy is stored at the top") {
  History history(Limits(500, 1 << 20));
  REQUIRE(history.Push(L"first"));
  REQUIRE(history.Push(L"second"));
  REQUIRE_EQ(history.Count(), 2u);
  CHECK_EQ(PreviewOf(history, 0), L"second");
  CHECK_EQ(PreviewOf(history, 1), L"first");
}

TEST_CASE("clipboard: a consecutive duplicate collapses without changing history") {
  History history(Limits(500, 1 << 20));
  REQUIRE(history.Push(L"same"));
  const std::uint64_t before = history.revision();
  CHECK_FALSE(history.Push(L"same"));
  REQUIRE_EQ(history.Count(), 1u);
  CHECK_EQ(history.revision(), before);  // no change reported to the UI
}

TEST_CASE("clipboard: a repeat copy moves to the top") {
  History history(Limits(500, 1 << 20));
  history.Push(L"a");
  history.Push(L"b");
  history.Push(L"c");
  REQUIRE_EQ(history.Count(), 3u);

  REQUIRE(history.Push(L"a"));  // previously oldest, now newest
  REQUIRE_EQ(history.Count(), 3u);
  CHECK_EQ(PreviewOf(history, 0), L"a");
  CHECK_EQ(PreviewOf(history, 1), L"c");
  CHECK_EQ(PreviewOf(history, 2), L"b");
}

TEST_CASE("clipboard: the entry cap drops the oldest copies") {
  History history(Limits(3, 1 << 20));
  history.Push(L"one");
  history.Push(L"two");
  history.Push(L"three");
  history.Push(L"four");  // should evict "one"
  REQUIRE_EQ(history.Count(), 3u);
  CHECK_EQ(PreviewOf(history, 0), L"four");
  CHECK_EQ(PreviewOf(history, 1), L"three");
  CHECK_EQ(PreviewOf(history, 2), L"two");
}

TEST_CASE("clipboard: an oversized copy is skipped") {
  History history(Limits(500, 8));  // 8-byte cap
  CHECK_FALSE(history.Push(L"123456789"));  // 9 wchar_t = 18 bytes, too big
  REQUIRE_EQ(history.Count(), 0u);
  CHECK(history.Push(L"1234"));  // 4 wchar_t = 8 bytes, fits
  REQUIRE_EQ(history.Count(), 1u);
}

TEST_CASE("clipboard: an empty copy is not stored") {
  History history(Limits(500, 1 << 20));
  CHECK_FALSE(history.Push(L""));
  CHECK_EQ(history.Count(), 0u);
}

TEST_CASE("clipboard: an empty query ranks in recency order") {
  History history(Limits(500, 1 << 20));
  history.Push(L"a longer first entry");
  history.Push(L"x");  // shorter, but newer, so still on top
  history.Push(L"medium text");

  std::array<tapir::fuzzy::Result, 8> out{};
  const std::size_t count = history.Rank(L"", out);
  REQUIRE_EQ(count, 3u);
  CHECK_EQ(PreviewOf(history, out[0].index), L"medium text");
  CHECK_EQ(PreviewOf(history, out[1].index), L"x");
  CHECK_EQ(PreviewOf(history, out[2].index), L"a longer first entry");
}

TEST_CASE("clipboard: a query filters through the fuzzy scorer and reports matches") {
  History history(Limits(500, 1 << 20));
  history.Push(L"Visual Studio Code");
  history.Push(L"Visual Studio 2022");
  history.Push(L"Unrelated text");

  std::array<tapir::fuzzy::Result, 8> out{};
  const std::size_t count = history.Rank(L"vsc", out);
  REQUIRE_EQ(count, 1u);
  const Entry* match = history.EntryAt(out[0].index);
  REQUIRE(match != nullptr);
  CHECK_EQ(match->preview, L"Visual Studio Code");
  CHECK_GT(out[0].matchedCount, 0u);
}

TEST_CASE("clipboard: a preview collapses control characters to single spaces") {
  History history(Limits(500, 1 << 20));
  history.Push(L"line one\r\nline\ttwo");
  CHECK_EQ(PreviewOf(history, 0), L"line one line two");
}