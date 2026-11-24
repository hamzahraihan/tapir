#include <doctest.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "input.h"
#include "popup_logic.h"

namespace {

using tapir::input::TextBuffer;
using tapir::popup::EnterAction;
using tapir::popup::enterAction;
using tapir::popup::filterMoreActions;
using tapir::popup::ghostSuffix;
using tapir::popup::hasVisibleQuery;
using tapir::popup::interpolateHeight;
using tapir::popup::listVisible;
using tapir::popup::motionAllowed;
using tapir::popup::MoreAction;
using tapir::popup::MoreItem;
using tapir::popup::moreActions;
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

TEST_CASE("moreActions in apps mode exposes exactly the four fixed actions in order") {
    const auto items = moreActions(true);
    REQUIRE_EQ(items.size(), 4U);
    CHECK(items[0].action == MoreAction::Open);
    CHECK_EQ(items[0].label, L"Open");
    CHECK(items[1].action == MoreAction::RunAsAdmin);
    CHECK_EQ(items[1].label, L"Run as administrator");
    CHECK(items[2].action == MoreAction::OpenFileLocation);
    CHECK_EQ(items[2].label, L"Open file location");
    CHECK(items[3].action == MoreAction::CopyPath);
    CHECK_EQ(items[3].label, L"Copy path");
}

TEST_CASE("moreActions in clipboard mode exposes exactly the four fixed actions in order") {
    const auto items = moreActions(false);
    REQUIRE_EQ(items.size(), 4U);
    CHECK(items[0].action == MoreAction::Paste);
    CHECK_EQ(items[0].label, L"Paste");
    CHECK(items[1].action == MoreAction::CopyOnly);
    CHECK_EQ(items[1].label, L"Copy only");
    CHECK(items[2].action == MoreAction::TogglePin);
    CHECK_EQ(items[2].label, L"Pin / Unpin");
    CHECK(items[3].action == MoreAction::DeleteItem);
    CHECK_EQ(items[3].label, L"Delete");
}

TEST_CASE("empty query keeps every action in both modes") {
    MoreItem appsOut[4]{};
    const auto appsCount = filterMoreActions(true, L"", appsOut);
    REQUIRE_EQ(appsCount, 4U);
    CHECK_EQ(appsOut[0].label, L"Open");
    CHECK_EQ(appsOut[3].label, L"Copy path");

    MoreItem clipboardOut[4]{};
    const auto clipboardCount = filterMoreActions(false, L"", clipboardOut);
    REQUIRE_EQ(clipboardCount, 4U);
    CHECK_EQ(clipboardOut[0].label, L"Paste");
    CHECK_EQ(clipboardOut[3].label, L"Delete");
}

TEST_CASE("filterMoreActions matches a query as a case-insensitive subsequence") {
    MoreItem out[4]{};

    const auto lowerCount = filterMoreActions(true, L"ra", out);
    REQUIRE_EQ(lowerCount, 1U);
    CHECK(out[0].action == MoreAction::RunAsAdmin);
    CHECK_EQ(out[0].label, L"Run as administrator");

    const auto upperCount = filterMoreActions(true, L"RA", out);
    REQUIRE_EQ(upperCount, 1U);
    CHECK(out[0].action == MoreAction::RunAsAdmin);
    CHECK_EQ(out[0].label, L"Run as administrator");
}

TEST_CASE("non-matching query yields zero actions in both modes") {
    MoreItem out[4]{};
    CHECK_EQ(filterMoreActions(true, L"zzz", out), 0U);
    CHECK_EQ(filterMoreActions(false, L"zzz", out), 0U);
}

TEST_CASE("filterMoreActions caps output at the span size") {
    MoreItem out[1]{};
    const auto count = filterMoreActions(true, L"o", out);
    REQUIRE_EQ(count, 1U);
    CHECK(out[0].action == MoreAction::Open);
    CHECK_EQ(out[0].label, L"Open");
}
