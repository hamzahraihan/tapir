#pragma once

#include <cstddef>
#include <string_view>

namespace tapir::popup {

bool hasVisibleQuery(std::wstring_view query) noexcept;
bool listVisible(bool compact, bool appsMode, std::wstring_view query) noexcept;
bool needsEmptyState(bool visible, std::size_t rowCount) noexcept;

}  // namespace tapir::popup
