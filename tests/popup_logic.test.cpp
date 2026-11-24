#include <doctest.h>

#include <cstddef>
#include <string_view>

#include "input.h"
#include "popup_logic.h"

namespace {

using tapir::input::TextBuffer;
using tapir::popup::EnterAction;
using tapir::popup::enterAction;
using tapir::popup::ghostSuffix;
using tapir::popup::hasVisibleQuery;
using tapir::popup::interpolateHeight;
using tapir::popup::listVisible;
using tapir::popup::motionAllowed;
using tapir::popup::needsEmptyState;
using tapir::popup::tryAcceptGhost;

}  // namespace

TEST_CASE("listVisible: compact + apps + empty query hides the list") {
    CHECK_FALSE(hasVisibleQuery(L""));
    CHECK_FALSE(listVisible(true, true, L""));
}

TEST_CASE("listVisible: whitespace query counts as non-empty") {
    CHECK(hasVisibleQuery(L" "));
    CHECK(listVisible(true, true, L" "));
}

TEST_CASE("listVisible: clipboard mode shows the list with an empty query") {
    CHECK(listVisible(true, false, L""));
}

TEST_CASE("listVisible: non-compact mode always shows the list") {
    CHECK(listVisible(false, true, L""));
    CHECK(listVisible(false, false, L""));
}

TEST_CASE("empty state is required whenever a visible list has no rows") {
    CHECK(needsEmptyState(true, 0));
    CHECK_FALSE(needsEmptyState(true, 1));
    CHECK_FALSE(needsEmptyState(false, 0));
}

TEST_CASE("ghost suffix completes a literal case-insensitive prefix") {
    CHECK_EQ(ghostSuffix(L"Visual Studio Code", L"vis"), L"ual Studio Code");
    CHECK_EQ(ghostSuffix(L"Visual Studio Code", L"VIS"), L"ual Studio Code");
}

TEST_CASE("ghost suffix completes a fuzzy match with gaps") {
    CHECK_EQ(ghostSuffix(L"Visual Studio Code", L"vsc"), L"ode");
}

TEST_CASE("ghost suffix is empty for empty query and non-matching candidate") {
    CHECK(ghostSuffix(L"Visual Studio Code", L"").empty());
    CHECK(ghostSuffix(L"Visual Studio Code", L"zzz").empty());
}

TEST_CASE("ghost suffix does not depend on mode") {
    const std::wstring_view title = L"Visual Studio Code";
    const std::wstring_view query = L"vsc";
    const auto first = ghostSuffix(title, query);
    const auto second = ghostSuffix(title, query);
    CHECK_EQ(first, second);
    CHECK_EQ(first, L"ode");
}

TEST_CASE("Right at end accepts the ghost and parks the caret at the end") {
    TextBuffer buffer;
    buffer.assign(L"vsc");
    REQUIRE(tryAcceptGhost(buffer, L"Visual Studio Code"));
    CHECK_EQ(buffer.value(), L"Visual Studio Code");
    CHECK_EQ(buffer.caret(), buffer.value().size());
    CHECK_FALSE(buffer.hasSelection());
}

TEST_CASE("Right inside text or with a selection does not accept the ghost") {
    TextBuffer inside;
    inside.assign(L"vsc");
    inside.moveLeft(false);
    CHECK_FALSE(tryAcceptGhost(inside, L"Visual Studio Code"));
    CHECK_EQ(inside.value(), L"vsc");

    TextBuffer selected;
    selected.assign(L"vsc");
    selected.moveHome(false);
    selected.moveRight(true);
    CHECK_FALSE(tryAcceptGhost(selected, L"Visual Studio Code"));
    CHECK_EQ(selected.value(), L"vsc");

    TextBuffer emptyCompletion;
    emptyCompletion.assign(L"vsc");
    CHECK_FALSE(tryAcceptGhost(emptyCompletion, L""));
    CHECK_EQ(emptyCompletion.value(), L"vsc");
}

TEST_CASE("interpolateHeight hits exact endpoints and rounds intermediate heights") {
    CHECK_EQ(interpolateHeight(100, 200, 0.0f), 100);
    CHECK_EQ(interpolateHeight(100, 200, 1.0f), 200);
    CHECK_EQ(interpolateHeight(100, 200, 0.25f), 125);
    CHECK_EQ(interpolateHeight(100, 200, 0.5f), 150);
    CHECK_EQ(interpolateHeight(0, 3, 0.5f), 2);
}

TEST_CASE("motion is gated by both config and client-area setting") {
    CHECK(motionAllowed(true, true));
    CHECK_FALSE(motionAllowed(true, false));
    CHECK_FALSE(motionAllowed(false, true));
    CHECK_FALSE(motionAllowed(false, false));
}

TEST_CASE("Enter launches in apps mode and Shift+Enter copies only in clipboard mode") {
    CHECK(enterAction(true, false) == EnterAction::Launch);
    CHECK(enterAction(true, true) == EnterAction::None);
    CHECK(enterAction(false, false) == EnterAction::Paste);
    CHECK(enterAction(false, true) == EnterAction::CopyOnly);
}
