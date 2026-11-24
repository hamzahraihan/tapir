#pragma once

#include <cstddef>
#include <string_view>

namespace tapir::input {
class TextBuffer;
}

namespace tapir::popup {

enum class EnterAction { None, Launch, Paste, CopyOnly };

EnterAction enterAction(bool appsMode, bool shift) noexcept;
bool hasVisibleQuery(std::wstring_view query) noexcept;
bool listVisible(bool compact, bool appsMode, std::wstring_view query) noexcept;
bool needsEmptyState(bool visible, std::size_t rowCount) noexcept;
std::wstring_view ghostSuffix(std::wstring_view title, std::wstring_view query) noexcept;
bool tryAcceptGhost(input::TextBuffer& text, std::wstring_view completion) noexcept;
int interpolateHeight(int fromDip, int toDip, float progress) noexcept;
bool motionAllowed(bool configAnimations, bool clientAreaAnimations) noexcept;

}  // namespace tapir::popup
