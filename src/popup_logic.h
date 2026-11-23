#pragma once

#include <cstddef>
#include <string_view>

namespace tapir::input {
class TextBuffer;
}

namespace tapir::popup {

bool hasVisibleQuery(std::wstring_view query) noexcept;
bool listVisible(bool compact, bool appsMode, std::wstring_view query) noexcept;
bool needsEmptyState(bool visible, std::size_t rowCount) noexcept;
std::wstring_view ghostSuffix(std::wstring_view title, std::wstring_view query) noexcept;
bool tryAcceptGhost(input::TextBuffer& text, std::wstring_view completion) noexcept;

}  // namespace tapir::popup
