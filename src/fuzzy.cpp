#include "fuzzy.h"

#include <algorithm>
#include <bit>

namespace tapir::fuzzy {
namespace {

// Settled scoring table from issue #8 / spec issue #16.
constexpr int kMatchedChar = 10;
constexpr int kStart = 40;
constexpr int kWordStart = 25;
constexpr int kConsecutive = 15;
constexpr int kConsecutiveCap = 45;
constexpr int kGapPenalty = -2;
constexpr int kGapCap = -20;
constexpr int kLeadingPenalty = -1;
constexpr int kLeadingCap = -15;
constexpr int kExact = 200;
constexpr int kPrefix = 100;
constexpr int kLaunchCap = 20;

bool IsSeparator(char16_t c) noexcept {
  return c == L' ' || c == L'-' || c == L'_' || c == L'.';
}

bool IsDigit(char16_t c) noexcept { return c >= L'0' && c <= L'9'; }
bool IsUpper(char16_t c) noexcept { return c >= L'A' && c <= L'Z'; }
bool IsLower(char16_t c) noexcept { return c >= L'a' && c <= L'z'; }

bool IsWordStart(std::wstring_view name, std::size_t i) noexcept {
  if (i == 0) return false;
  const char16_t current = name[i];
  if (IsDigit(current) || IsSeparator(current)) return false;
  const char16_t previous = name[i - 1];
  return IsSeparator(previous) || (IsUpper(current) && IsLower(previous));
}

int LaunchBoost(std::uint32_t launchCount) noexcept {
  if (launchCount == 0) return 0;
  const auto levels = std::bit_width(static_cast<std::uint64_t>(launchCount) + 1) - 1;
  return std::min(kLaunchCap, static_cast<int>(4 * levels));
}

bool EqualsFolded(std::wstring_view folded, std::wstring_view query) noexcept {
  if (folded.size() != query.size()) return false;
  for (std::size_t i = 0; i < folded.size(); ++i) {
    if (folded[i] != Fold(query[i])) return false;
  }
  return true;
}

bool StartsWithFolded(std::wstring_view folded, std::wstring_view query) noexcept {
  if (folded.size() <= query.size()) return false;
  for (std::size_t i = 0; i < query.size(); ++i) {
    if (folded[i] != Fold(query[i])) return false;
  }
  return true;
}

bool CanMatchRest(std::wstring_view folded, std::wstring_view query,
                  std::size_t queryIndex, std::size_t from) noexcept {
  for (std::size_t i = from; i < folded.size() && queryIndex < query.size(); ++i) {
    if (folded[i] == Fold(query[queryIndex])) ++queryIndex;
  }
  return queryIndex == query.size();
}

bool Better(const Result& a, const Result& b) noexcept {
  if (a.score != b.score) return a.score > b.score;
  if (a.launchCount != b.launchCount) return a.launchCount > b.launchCount;
  if (a.nameLength != b.nameLength) return a.nameLength < b.nameLength;
  return a.index < b.index;
}

}  // namespace

char16_t Fold(char16_t c) noexcept {
  if (c >= L'A' && c <= L'Z') return static_cast<char16_t>(c + (L'a' - L'A'));
  if (c < 128) return c;
  if ((c >= 0x00C0 && c <= 0x00D6) || (c >= 0x00D8 && c <= 0x00DE)) {
    return static_cast<char16_t>(c + 32);
  }
  if (c >= 0x0100 && c <= 0x012E && (c & 1) == 0) return static_cast<char16_t>(c + 1);
  if ((c >= 0x0391 && c <= 0x03A1) || (c >= 0x03A3 && c <= 0x03AB)) {
    return static_cast<char16_t>(c + 32);
  }
  if (c >= 0x0410 && c <= 0x042F) return static_cast<char16_t>(c + 32);
  return c;
}

void FoldInto(std::wstring_view text, std::wstring& out) {
  out.clear();
  out.reserve(text.size());
  for (const char16_t c : text) out.push_back(Fold(c));
}

bool Score(const Entry& entry, std::wstring_view query, Result& out) noexcept {
  out.score = 0;
  out.launchCount = entry.launchCount;
  out.nameLength = entry.name.size();
  out.matchedCount = 0;

  const std::wstring_view folded = entry.folded;
  std::size_t firstMatch = folded.size();
  std::size_t previousMatch = 0;
  std::size_t searchFrom = 0;
  int gapTotal = 0;
  int consecutiveTotal = 0;

  for (std::size_t queryIndex = 0; queryIndex < query.size(); ++queryIndex) {
    std::size_t fallback = folded.size();
    std::size_t wordStart = folded.size();
    for (std::size_t i = searchFrom; i < folded.size(); ++i) {
      if (folded[i] != Fold(query[queryIndex])) continue;
      if (!CanMatchRest(folded, query, queryIndex + 1, i + 1)) continue;
      if (fallback == folded.size()) fallback = i;
      if (IsWordStart(entry.name, i)) {
        wordStart = i;
        break;
      }
    }
    const std::size_t match = wordStart != folded.size() ? wordStart : fallback;
    if (match == folded.size()) {
      out.matchedCount = 0;
      return false;
    }

    if (out.matchedCount < kMaxMatched) {
      out.matched[out.matchedCount] = static_cast<std::uint32_t>(match);
      ++out.matchedCount;
    }

    if (queryIndex == 0) {
      firstMatch = match;
    } else {
      gapTotal += static_cast<int>(match - previousMatch - 1);
      if (match == previousMatch + 1) {
        consecutiveTotal = std::min(kConsecutiveCap, consecutiveTotal + kConsecutive);
      }
    }
    if (IsWordStart(entry.name, match)) out.score += kWordStart;
    previousMatch = match;
    searchFrom = match + 1;
  }

  if (firstMatch == 0) out.score += kStart;
  out.score += static_cast<int>(query.size()) * kMatchedChar;
  out.score += consecutiveTotal;
  out.score += std::max(kGapCap, static_cast<int>(std::min(0, gapTotal * kGapPenalty)));
  if (firstMatch != folded.size()) {
    out.score += std::max(kLeadingCap, static_cast<int>(firstMatch) * kLeadingPenalty);
  }
  if (query.empty()) {
    // No ordinary components; exact/prefix bonuses do not apply to an empty query.
  } else if (EqualsFolded(folded, query)) {
    out.score += kExact;
  } else if (StartsWithFolded(folded, query)) {
    out.score += kPrefix;
  }
  out.score += LaunchBoost(entry.launchCount);
  return true;
}

std::size_t Rank(std::span<const Entry> entries, std::wstring_view query,
                 std::span<Result> out) noexcept {
  if (out.empty()) return 0;

  std::size_t count = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    Result candidate;
    if (!Score(entries[i], query, candidate)) continue;
    candidate.index = i;

    if (count == out.size() && !Better(candidate, out[count - 1])) continue;
    std::size_t position = std::min(count, out.size() - 1);
    while (position > 0 && Better(candidate, out[position - 1])) {
      out[position] = out[position - 1];
      --position;
    }
    out[position] = candidate;
    if (count < out.size()) ++count;
  }
  return count;
}

}  // namespace tapir::fuzzy
