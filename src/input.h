#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace tapir::input {

class TextBuffer {
public:
    std::wstring_view value() const noexcept;
    std::size_t caret() const noexcept;
    bool hasSelection() const noexcept;
    std::pair<std::size_t, std::size_t> selection() const noexcept; // [first,last) normalized
    void assign(std::wstring_view text);            // resets caret/anchor to end
    void clear();
    void insert(std::wstring_view text);            // replaces selection, caret after inserted
    void backspace();                                 // delete selection or one code unit left
    void deleteForward();                             // delete selection or one code unit right
    void moveLeft(bool extend);
    void moveRight(bool extend);
    void moveWordLeft(bool extend);
    void moveWordRight(bool extend);
    void moveHome(bool extend);
    void moveEnd(bool extend);
    void selectAll();
    void collapseSelection();                        // collapse to active caret end
    std::wstring selectedText() const;
    void replace(std::wstring_view text);            // replace selection, caret after

private:
    std::wstring text_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
};

} // namespace tapir::input