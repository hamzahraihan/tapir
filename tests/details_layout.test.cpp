#include <doctest.h>
#include "util/details_layout.h"

using tapir::details_layout::kWidthDip;
using tapir::details_layout::partition;
using tapir::details_layout::Widths;

TEST_CASE("partition gives the whole width to the list when the pane is off") {
    const Widths widths = partition(680, false);
    CHECK(widths.listDip == 680);
    CHECK(widths.paneDip == 0);
}

TEST_CASE("partition reserves the pane width when enabled") {
    const Widths widths = partition(680, true);
    CHECK(widths.listDip == 680 - kWidthDip);
    CHECK(widths.paneDip == kWidthDip);
    CHECK(kWidthDip == 280);
}

TEST_CASE("partition keeps the whole width when the total is too narrow") {
    const Widths narrow = partition(200, true);
    CHECK(narrow.listDip == 200);
    CHECK(narrow.paneDip == 0);

    const Widths exact = partition(kWidthDip, true);
    CHECK(exact.listDip == kWidthDip);
    CHECK(exact.paneDip == 0);
}
