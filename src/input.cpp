#include "input.h"

#include <algorithm>

namespace tapir::input {
namespace {

bool isSeparator(wchar_t c) noexcept {
    if (c <= 0x20) return true;
    return (c >= L'!' && c <= L'/') ||
           (c >= L':' && c <= L'@') ||
           (c >= L'[' && c <= L'`') ||
           (c >= L'{' && c <= L'~');
}

} // namespace

std::wstring_view TextBuffer::value() const noexcept { return text_; }
std::size_t TextBuffer::caret() const noexcept { return caret_; }

bool TextBuffer::hasSelection() const noexcept { return caret_ != anchor_; }

std::pair<std::size_t, std::size_t> TextBuffer::selection() const noexcept {
    return caret_ < anchor_
        ? std::pair<std::size_t, std::size_t>{caret_, anchor_}
        : std::pair<std::size_t, std::size_t>{anchor_, caret_};
}

void TextBuffer::assign(std::wstring_view text) {
    text_.assign(text);
    caret_ = text_.size();
    anchor_ = caret_;
}

void TextBuffer::clear() {
    text_.clear();
    caret_ = 0;
    anchor_ = 0;
}

void TextBuffer::insert(std::wstring_view text) {
    replace(text);
}

void TextBuffer::replace(std::wstring_view text) {
    const auto [first, last] = selection();
    text_.replace(first, last - first, text);
    caret_ = first + text.size();
    anchor_ = caret_;
}

void TextBuffer::backspace() {
    if (hasSelection()) {
        replace(L"");
        return;
    }
    if (caret_ == 0) return;
    text_.erase(caret_ - 1, 1);
    --caret_;
    anchor_ = caret_;
}

void TextBuffer::deleteForward() {
    if (hasSelection()) {
        replace(L"");
        return;
    }
    if (caret_ >= text_.size()) return;
    text_.erase(caret_, 1);
    anchor_ = caret_;
}

void TextBuffer::moveLeft(bool extend) {
    if (caret_ > 0) --caret_;
    if (!extend) anchor_ = caret_;
}

void TextBuffer::moveRight(bool extend) {
    if (caret_ < text_.size()) ++caret_;
    if (!extend) anchor_ = caret_;
}

void TextBuffer::moveWordLeft(bool extend) {
    std::size_t i = caret_;
    while (i > 0 && isSeparator(text_[i - 1])) --i;
    while (i > 0 && !isSeparator(text_[i - 1])) --i;
    caret_ = i;
    if (!extend) anchor_ = caret_;
}

void TextBuffer::moveWordRight(bool extend) {
    std::size_t i = caret_;
    while (i < text_.size() && !isSeparator(text_[i])) ++i;
    while (i < text_.size() && isSeparator(text_[i])) ++i;
    caret_ = i;
    if (!extend) anchor_ = caret_;
}

void TextBuffer::moveHome(bool extend) {
    caret_ = 0;
    if (!extend) anchor_ = caret_;
}

void TextBuffer::moveEnd(bool extend) {
    caret_ = text_.size();
    if (!extend) anchor_ = caret_;
}

void TextBuffer::selectAll() {
    anchor_ = 0;
    caret_ = text_.size();
}

void TextBuffer::collapseSelection() {
    anchor_ = caret_;
}

std::wstring TextBuffer::selectedText() const {
    const auto [first, last] = selection();
    return text_.substr(first, last - first);
}

} // namespace tapir::input
