#include <doctest.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "fuzzy.h"

namespace {

using tapir::fuzzy::Entry;
using tapir::fuzzy::Result;

template <std::size_t N>
std::size_t ScoreAll(std::array<Entry, N>& entries, std::wstring_view query,
                     std::array<Result, N>& out) {
  return tapir::fuzzy::Rank(entries, query, out);
}

Result ScoreOne(std::wstring_view name, std::wstring_view folded,
                std::wstring_view query, std::uint32_t launches = 0) {
  Result result;
  const Entry entry{name, folded, launches};
  REQUIRE(tapir::fuzzy::Score(entry, query, result));
  return result;
}

}  // namespace

TEST_CASE("fuzzy: start and word-start bonuses with a gap penalty") {
  const Result r = ScoreOne(L"Visual Studio Code", L"visual studio code", L"vs");
  CHECK_EQ(r.score, 73);  // 2*10 + 40 start + 25 word start - 6*2 gap
  REQUIRE_EQ(r.matchedCount, 2);
  CHECK_EQ(r.matched[0], 0);
  CHECK_EQ(r.matched[1], 7);
}

TEST_CASE("fuzzy: camelCase boundary rewards a word start") {
  const Result r = ScoreOne(L"VisualStudioCode", L"visualstudiocode", L"vc");
  CHECK_EQ(r.score, 65);  // 2*10 + 40 start + 25 camel boundary - 20 capped gap
}

TEST_CASE("fuzzy: exact and prefix bonuses stack with ordinary components") {
  CHECK_EQ(ScoreOne(L"code", L"code", L"code").score, 325);   // +200 exact
  CHECK_EQ(ScoreOne(L"codex", L"codex", L"code").score, 225); // +100 prefix
}

TEST_CASE("fuzzy: leading and gap penalties are capped") {
  CHECK_EQ(ScoreOne(L"aaaaaaaaaaaaaaaaaaaaab", L"aaaaaaaaaaaaaaaaaaaaab", L"ab").score, 40);
  CHECK_EQ(ScoreOne(L"aaaaaaaaaaaaaaaaaaaab", L"aaaaaaaaaaaaaaaaaaaab", L"b").score, -5);
}

TEST_CASE("fuzzy: matching is case-insensitive") {
  CHECK_EQ(ScoreOne(L"Visual Studio Code", L"visual studio code", L"VSC").score,
           ScoreOne(L"Visual Studio Code", L"visual studio code", L"vsc").score);
}

TEST_CASE("fuzzy: folds non-ASCII uppercase by code unit") {
  const Result upper = ScoreOne(L"Caf\x00C9", L"caf\x00e9", L"caf\x00e9");
  const Result lower = ScoreOne(L"Caf\x00E9", L"caf\x00e9", L"caf\x00e9");
  CHECK_EQ(upper.score, lower.score);
}

TEST_CASE("fuzzy: a missing subsequence is not a match") {
  Result result;
  const Entry entry{L"Visual Studio 2022", L"visual studio 2022", 0};
  CHECK_FALSE(tapir::fuzzy::Score(entry, L"vsc", result));
}

TEST_CASE("fuzzy: launch count gives a capped additive boost") {
  const Result once = ScoreOne(L"code", L"code", L"code", 1);
  const Result heavy = ScoreOne(L"code", L"code", L"code", 1000);
  CHECK_EQ(once.score, 329); // 325 + 4*log2(2)
  CHECK_EQ(heavy.score, 345); // 325 + capped 20
}

TEST_CASE("fuzzy: vsc ranks Visual Studio Code first") {
  std::array<Entry, 2> entries{{
      {L"Visual Studio Code", L"visual studio code", 0},
      {L"Visual Studio 2022", L"visual studio 2022", 0},
  }};
  std::array<Result, 2> out{};
  const std::size_t count = ScoreAll(entries, L"vsc", out);
  REQUIRE_EQ(count, 1);
  CHECK_EQ(entries[out[0].index].name, L"Visual Studio Code");
}

TEST_CASE("fuzzy: vs ranks Visual Studio Code above Visual Studio 2022") {
  std::array<Entry, 2> entries{{
      {L"Visual Studio Code", L"visual studio code", 0},
      {L"Visual Studio 2022", L"visual studio 2022", 0},
  }};
  std::array<Result, 2> out{};
  REQUIRE_EQ(ScoreAll(entries, L"vs", out), 2);
  CHECK_EQ(entries[out[0].index].name, L"Visual Studio Code");
  CHECK_EQ(entries[out[1].index].name, L"Visual Studio 2022");
}

TEST_CASE("fuzzy: ties resolve by launch count, then name length, then index") {
  std::array<Entry, 3> entries{{
      {L"zzzz", L"aa", 0},
      {L"aa", L"aa", 0},
      {L"bb", L"aa", 3},
  }};
  std::array<Result, 3> out{};
  REQUIRE_EQ(ScoreAll(entries, L"aa", out), 3);
  CHECK_EQ(out[0].index, 2);
  CHECK_EQ(out[1].index, 1);
  CHECK_EQ(out[2].index, 0);
}

TEST_CASE("fuzzy: an empty query ranks by launch count") {
  std::array<Entry, 3> entries{{
      {L"alpha", L"alpha", 0},
      {L"beta", L"beta", 5},
      {L"gamma", L"gamma", 0},
  }};
  std::array<Result, 3> out{};
  REQUIRE_EQ(ScoreAll(entries, L"", out), 3);
  CHECK_EQ(out[0].index, 1);
}
