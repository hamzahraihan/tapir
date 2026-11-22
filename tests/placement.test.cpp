#include <doctest.h>
#include "config.h"
#include "util/placement.h"

using tapir::defaultSettings;
using tapir::placement::compute;
using tapir::placement::kCollapsedHeightDip;
using tapir::placement::Position;
using tapir::placement::Rect;

TEST_CASE("upper-center placement at 96 dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, defaultSettings().window.widthDip, kCollapsedHeightDip, 96, Position::UpperCenter);
    CHECK(r.left == 620);
    CHECK(r.top == 341);
    CHECK(r.right == 1300);
    CHECK(r.bottom == 397);
}

TEST_CASE("center placement at 96 dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, defaultSettings().window.widthDip, kCollapsedHeightDip, 96, Position::Center);
    CHECK(r.left == 620);
    CHECK(r.top == 512);
}

TEST_CASE("placement scales with dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, defaultSettings().window.widthDip, kCollapsedHeightDip, 144, Position::UpperCenter);
    CHECK(r.left == 450);
    CHECK(r.top == 332);
    CHECK(r.right == 1470);
    CHECK(r.bottom == 416);
}

TEST_CASE("placement clamps to a smaller monitor") {
    const Rect monitor{0, 0, 400, 300};
    const Rect r = compute(monitor, defaultSettings().window.widthDip, kCollapsedHeightDip, 96, Position::UpperCenter);
    CHECK(r.left == 0);
    CHECK(r.right == 400);
    CHECK(r.top == 81);
    CHECK(r.bottom == 137);
}
