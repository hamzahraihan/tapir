#include <doctest.h>

#include <cstddef>
#include <string_view>

#include "popup_logic.h"

namespace {

using tapir::popup::hasVisibleQuery;
using tapir::popup::listVisible;
using tapir::popup::needsEmptyState;

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
