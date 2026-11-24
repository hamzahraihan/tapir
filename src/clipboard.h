#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "fuzzy.h"

namespace tapir::clipboard {

// One captured clipboard entry. `text` is the raw copy preserved for a future
// paste; `preview` is the single-line display and search form; `folded` is the
// case-folded preview used by the fuzzy scorer. For #24 the list is in-memory.
struct Entry {
  std::wstring text;
  std::wstring preview;
  std::wstring folded;
};

// Privacy-relevant facts observed from a clipboard payload. CanUploadToCloudClipboard
// is deliberately absent: it never blocks capture and is ignored by design.
struct PrivacyFacts {
  bool excludePresent = false;        // ExcludeClipboardContentFromMonitorProcessing present
  bool canIncludePresent = false;     // CanIncludeInClipboardHistory present
  bool canIncludeValueValid = false;  // the DWORD was read with the expected size
  std::uint32_t canIncludeValue = 0;
};

enum class Privacy { Allow, Block };

// Pure privacy gate. An Exclude format blocks outright; a present
// CanIncludeInClipboardHistory blocks unless it is readable and exactly 1 (any
// other value fails closed); everything else is allowed. CanUploadToCloudClipboard
// is ignored.
Privacy EvaluatePrivacy(const PrivacyFacts& facts) noexcept;

// Case-insensitive match of an executable file name against the ignore list.
bool IsIgnored(std::wstring_view executableName,
               const std::vector<std::wstring>& ignoreList);

// In-memory clipboard history, used on the UI thread only. Entries are ordered
// newest first, so index 0 is the most recent copy. Caps and dedupe apply on
// every Push. `Rank` keeps an empty query in recency order (newest first) while
// any non-empty query is scored through the shared fuzzy engine.
class History {
 public:
  explicit History(const Settings::History& limits);

  // Stores `text` unless it is empty, oversized, or a consecutive duplicate of
  // the newest entry. A repeat copies to the top. Returns true when the history
  // changed (the caller refreshes the visible list).
  bool Push(std::wstring text);

  const std::vector<Entry>& Entries() const noexcept { return entries_; }
  std::size_t Count() const noexcept { return entries_.size(); }
  const Entry* EntryAt(std::size_t index) const noexcept;
  std::size_t Rank(std::wstring_view query,
                   std::span<fuzzy::Result> out) const noexcept;
  std::uint64_t revision() const noexcept { return revision_; }

 private:
  void Rebuild();

  std::size_t maxEntries_ = 0;
  std::size_t maxEntryBytes_ = 0;
  std::vector<Entry> entries_;       // newest first
  std::vector<fuzzy::Entry> fuzzy_;  // views into entries_.{preview, folded}
  std::uint64_t revision_ = 0;
};

// Reads the current clipboard, applies the privacy gate and the capture-only
// ignore list, and stores the text in `out` when allowed. Returns true when the
// history changed. A locked clipboard after brief retries is skipped (false).
bool CaptureUpdate(const Settings::History& limits, History& out);

}  // namespace tapir::clipboard