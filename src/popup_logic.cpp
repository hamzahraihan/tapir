#include "popup_logic.h"

namespace tapir::popup {

bool hasVisibleQuery(std::wstring_view query) noexcept {
  return !query.empty();
}

bool listVisible(bool compact, bool appsMode, std::wstring_view query) noexcept {
  return !compact || !appsMode || hasVisibleQuery(query);
}

bool needsEmptyState(bool visible, std::size_t rowCount) noexcept {
  return visible && rowCount == 0;
}

}  // namespace tapir::popup
