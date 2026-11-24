#include "popup_logic.h"

#include <cmath>

#include "fuzzy.h"
#include "input.h"

namespace tapir::popup {

std::span<const MoreItem> moreActions(bool appsMode) noexcept {
  static constexpr MoreItem kApps[]{
      {MoreAction::Open, L"Open"},
      {MoreAction::RunAsAdmin, L"Run as administrator"},
      {MoreAction::OpenFileLocation, L"Open file location"},
      {MoreAction::CopyPath, L"Copy path"},
  };
  static constexpr MoreItem kClipboard[]{
      {MoreAction::Paste, L"Paste"},
      {MoreAction::CopyOnly, L"Copy only"},
      {MoreAction::TogglePin, L"Pin / Unpin"},
      {MoreAction::DeleteItem, L"Delete"},
  };
  return appsMode ? kApps : kClipboard;
}

std::size_t filterMoreActions(bool appsMode, std::wstring_view query,
                              std::span<MoreItem> out) noexcept {
  const auto items = moreActions(appsMode);
  std::size_t written = 0;
  for (const MoreItem& item : items) {
    if (written == out.size()) break;
    std::size_t queryIndex = 0;
    for (std::size_t i = 0; i < item.label.size() && queryIndex < query.size(); ++i) {
      if (fuzzy::Fold(item.label[i]) == fuzzy::Fold(query[queryIndex])) ++queryIndex;
    }
    if (queryIndex == query.size()) out[written++] = item;
  }
  return written;
}

EnterAction enterAction(bool appsMode, bool shift) noexcept {
    if (appsMode) return shift ? EnterAction::None : EnterAction::Launch;
    return shift ? EnterAction::CopyOnly : EnterAction::Paste;
}

bool hasVisibleQuery(std::wstring_view query) noexcept {
  return !query.empty();
}

bool listVisible(bool compact, bool appsMode, std::wstring_view query) noexcept {
  return !compact || !appsMode || hasVisibleQuery(query);
}

bool needsEmptyState(bool visible, std::size_t rowCount) noexcept {
  return visible && rowCount == 0;
}

std::wstring_view ghostSuffix(std::wstring_view title, std::wstring_view query) noexcept {
  if (query.empty()) return {};
  std::size_t last = 0;
  std::size_t queryIndex = 0;
  for (std::size_t i = 0; i < title.size() && queryIndex < query.size(); ++i) {
    if (fuzzy::Fold(title[i]) == fuzzy::Fold(query[queryIndex])) {
      last = i;
      ++queryIndex;
    }
  }
  if (queryIndex != query.size() || last + 1 >= title.size()) return {};
  return title.substr(last + 1);
}

bool tryAcceptGhost(input::TextBuffer& text, std::wstring_view completion) noexcept {
  if (completion.empty() || text.hasSelection() || text.caret() != text.value().size()) return false;
  text.assign(completion);
  return true;
}

int interpolateHeight(int fromDip, int toDip, float progress) noexcept {
  return fromDip + static_cast<int>(std::lround((toDip - fromDip) * progress));
}

bool motionAllowed(bool configAnimations, bool clientAreaAnimations) noexcept {
  return configAnimations && clientAreaAnimations;
}

}  // namespace tapir::popup
