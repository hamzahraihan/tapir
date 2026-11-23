#include <doctest.h>
#include "anim.h"

using tapir::anim::Timeline;
using tapir::anim::easeOutCubic;

TEST_CASE("easeOutCubic matches the settled cubic curve at boundaries") {
    CHECK(easeOutCubic(0.0f) == 0.0f);
    CHECK(easeOutCubic(1.0f) == 1.0f);
    CHECK(easeOutCubic(0.5f) == 0.875f);
    CHECK(easeOutCubic(0.25f) == 0.578125f);
}

TEST_CASE("easeOutCubic clamps out-of-range progress") {
    CHECK(easeOutCubic(-0.5f) == 0.0f);
    CHECK(easeOutCubic(1.5f) == 1.0f);
}

TEST_CASE("motion durations match the specification") {
    CHECK(tapir::anim::kShowMs == 120);
    CHECK(tapir::anim::kSelectionMs == 100);
    CHECK(tapir::anim::kCompactMs == 140);
}

TEST_CASE("timeline is idle until started") {
    Timeline t;
    CHECK_FALSE(t.active());
    CHECK(t.progress(1000) == 0.0f);
    CHECK_FALSE(t.finished(1000));
    CHECK_FALSE(t.update(1000));
}

TEST_CASE("timeline progresses with eased value and finishes at duration") {
    Timeline t;
    t.start(1000, 120);
    CHECK(t.active());
    CHECK(t.progress(1000) == 0.0f);
    CHECK(t.progress(1060) == 0.875f);
    CHECK_FALSE(t.finished(1060));
    CHECK(t.update(1060));
    CHECK(t.finished(1120));
    CHECK(t.progress(1120) == 1.0f);
    CHECK_FALSE(t.update(1120));
    CHECK_FALSE(t.active());
}

TEST_CASE("snap stops the timeline and leaves final state") {
    Timeline t;
    t.start(1000, 120);
    t.snap();
    CHECK_FALSE(t.active());
    CHECK_FALSE(t.update(1010));
    CHECK_FALSE(t.finished(1010));
}

TEST_CASE("reset returns to idle without progress") {
    Timeline t;
    t.start(1000, 120);
    t.snap();
    t.reset();
    CHECK_FALSE(t.active());
    CHECK(t.progress(2000) == 0.0f);
}

TEST_CASE("restart begins a fresh interval from the new start time") {
    Timeline t;
    t.start(1000, 100);
    CHECK(t.progress(1050) == 0.875f);
    t.start(2000, 100);
    CHECK(t.progress(2050) == 0.875f);
    CHECK_FALSE(t.finished(2050));
}

TEST_CASE("zero or negative duration finishes immediately without dividing by zero") {
    Timeline t;
    t.start(1000, 0);
    CHECK(t.finished(1000));
    CHECK(t.progress(1000) == 1.0f);
}
