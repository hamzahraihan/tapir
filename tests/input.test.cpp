#include <doctest.h>
#include <string>
#include <utility>
#include "input.h"

using tapir::input::TextBuffer;

TEST_CASE("TextBuffer default state is empty") {
    TextBuffer b;
    CHECK(b.value().empty());
    CHECK(b.caret() == 0);
    CHECK_FALSE(b.hasSelection());
    CHECK(b.selection() == std::pair<std::size_t, std::size_t>{0, 0});
    CHECK(b.selectedText().empty());
}

TEST_CASE("assign sets text and caret to end; clear empties") {
    TextBuffer b;
    b.assign(L"abc");
    CHECK(b.value() == L"abc");
    CHECK(b.caret() == 3);
    CHECK_FALSE(b.hasSelection());

    b.clear();
    CHECK(b.value().empty());
    CHECK(b.caret() == 0);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("insert into empty buffer") {
    TextBuffer b;
    b.insert(L"hi");
    CHECK(b.value() == L"hi");
    CHECK(b.caret() == 2);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("insert mid-text") {
    TextBuffer b;
    b.assign(L"ab");
    b.moveLeft(false);
    b.insert(L"X");
    CHECK(b.value() == L"aXb");
    CHECK(b.caret() == 2);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("backspace at start is no-op") {
    TextBuffer b;
    b.assign(L"ab");
    b.moveHome(false);
    b.backspace();
    CHECK(b.value() == L"ab");
    CHECK(b.caret() == 0);
}

TEST_CASE("backspace deletes selection") {
    TextBuffer b;
    b.assign(L"abc");
    b.moveHome(false);
    b.moveRight(true); // select "a"
    b.backspace();
    CHECK(b.value() == L"bc");
    CHECK(b.caret() == 0);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("backspace deletes one code unit left") {
    TextBuffer b;
    b.assign(L"abc");
    b.backspace();
    CHECK(b.value() == L"ab");
    CHECK(b.caret() == 2);
}

TEST_CASE("deleteForward at end is no-op") {
    TextBuffer b;
    b.assign(L"ab");
    b.deleteForward();
    CHECK(b.value() == L"ab");
    CHECK(b.caret() == 2);
}

TEST_CASE("deleteForward deletes selection or one code unit right") {
    TextBuffer b;
    b.assign(L"abc");
    b.moveHome(false);
    b.moveRight(true); // select "a"
    b.deleteForward();
    CHECK(b.value() == L"bc");
    CHECK(b.caret() == 0);

    TextBuffer c;
    c.assign(L"abc");
    c.moveHome(false);
    c.deleteForward();
    CHECK(c.value() == L"bc");
    CHECK(c.caret() == 0);
}

TEST_CASE("moveLeft and moveRight clamp at boundaries") {
    TextBuffer b;
    b.assign(L"ab");
    b.moveLeft(false);
    b.moveLeft(false);
    b.moveLeft(false);
    CHECK(b.caret() == 0);
    b.moveRight(false);
    b.moveRight(false);
    b.moveRight(false);
    CHECK(b.caret() == 2);
}

TEST_CASE("moveHome and moveEnd") {
    TextBuffer b;
    b.assign(L"abc");
    b.moveHome(false);
    CHECK(b.caret() == 0);
    b.moveEnd(false);
    CHECK(b.caret() == 3);
}

TEST_CASE("moveWordLeft and moveWordRight across spaces and punctuation") {
    TextBuffer b;
    b.assign(L"foo bar-baz");
    // caret at end (11)
    b.moveWordLeft(false);
    CHECK(b.caret() == 8); // start of "baz"? "bar-baz": positions: foo(0-2) space(3) bar(4-6) -(7) baz(8-10)
    b.moveWordLeft(false);
    CHECK(b.caret() == 4); // start of "bar"
    b.moveWordLeft(false);
    CHECK(b.caret() == 0); // start of "foo"

    TextBuffer c;
    c.assign(L"foo bar-baz");
    c.moveHome(false);
    c.moveWordRight(false);
    CHECK(c.caret() == 4);
}

TEST_CASE("selection ordering after backward drag") {
    TextBuffer b;
    b.assign(L"abcde");
    b.moveEnd(false);
    // drag left selecting "de"? moveLeft with extend twice
    b.moveLeft(true);
    b.moveLeft(true);
    CHECK(b.hasSelection());
    CHECK(b.selection() == std::pair<std::size_t, std::size_t>{3, 5});
    CHECK(b.caret() == 3);
}

TEST_CASE("selectedText returns selected substring") {
    TextBuffer b;
    b.assign(L"abcde");
    b.moveHome(false);
    b.moveRight(true);
    b.moveRight(true);
    CHECK(b.selectedText() == L"ab");
}

TEST_CASE("selectAll and replace") {
    TextBuffer b;
    b.assign(L"abc");
    b.selectAll();
    CHECK(b.hasSelection());
    CHECK(b.selection() == std::pair<std::size_t, std::size_t>{0, 3});
    b.replace(L"XYZ");
    CHECK(b.value() == L"XYZ");
    CHECK(b.caret() == 3);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("collapseSelection collapses to caret end") {
    TextBuffer b;
    b.assign(L"abcde");
    b.moveHome(false);
    b.moveRight(true);
    b.moveRight(true); // select "ab", caret=2, anchor=0
    b.collapseSelection();
    CHECK_FALSE(b.hasSelection());
    CHECK(b.caret() == 2);
}

TEST_CASE("surrogate pairs are treated as code units without crashing") {
    TextBuffer b;
    b.assign(std::wstring{L"\xD83D\xDE00"}); // 2 code units
    CHECK(b.value().size() == 2);
    b.backspace();
    CHECK(b.value().size() == 1);
    CHECK(b.caret() == 1);
    b.clear();
    b.assign(std::wstring{L"\xD83D\xDE00"});
    b.moveHome(false);
    b.deleteForward();
    CHECK(b.value().size() == 1);
}

TEST_CASE("insert replaces current selection") {
    TextBuffer b;
    b.assign(L"abc");
    b.moveHome(false);
    b.moveRight(true);
    b.moveRight(true); // select "ab"
    b.insert(L"X");
    CHECK(b.value() == L"Xc");
    CHECK(b.caret() == 1);
    CHECK_FALSE(b.hasSelection());
}

TEST_CASE("extend flag controls anchor on movement") {
    TextBuffer b;
    b.assign(L"abc");
    b.moveHome(false);
    b.moveRight(false);
    CHECK(b.caret() == 1);
    CHECK_FALSE(b.hasSelection());
    b.moveRight(true);
    CHECK(b.caret() == 2);
    CHECK(b.hasSelection());
    CHECK(b.selection() == std::pair<std::size_t, std::size_t>{1, 2});
}
