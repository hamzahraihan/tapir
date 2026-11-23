#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tapir::fuzzy {

// Matched code-unit positions are returned in a fixed array so ranking never
// allocates. Queries longer than this keep the first kMaxMatched positions.
inline constexpr std::size_t kMaxMatched = 64;

struct Entry {
  std::wstring_view name;
  std::wstring_view folded;
  std::uint32_t launchCount = 0;
};

struct Result {
  std::size_t index = 0;
  int score = 0;
  std::uint32_t launchCount = 0;
  std::size_t nameLength = 0;
  std::size_t matchedCount = 0;
  std::uint32_t matched[kMaxMatched]{};
};

// Case folding is per UTF-16 code unit, with an ASCII fast path. Supplementary
// planes and special-case folds are out of scope for v1.
char16_t Fold(char16_t c) noexcept;
void FoldInto(std::wstring_view text, std::wstring& out);

// Returns false when query is not a subsequence of entry.folded.
bool Score(const Entry& entry, std::wstring_view query, Result& out) noexcept;

// Writes at most out.size() results, best first, and returns the count written.
std::size_t Rank(std::span<const Entry> entries, std::wstring_view query,
                 std::span<Result> out) noexcept;

}  // namespace tapir::fuzzy
