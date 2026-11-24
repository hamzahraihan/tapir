#include <doctest.h>
#include <array>
#include <limits>
#include <span>
#include "config.h"
#include "util/monitor_policy.h"
#include "util/placement.h"

using tapir::Monitor;
using tapir::MonitorSelection;
using tapir::monitor_policy::contains;
using tapir::monitor_policy::Descriptor;
using tapir::monitor_policy::distanceSquared;
using tapir::monitor_policy::Point;
using tapir::monitor_policy::select;
using tapir::placement::Rect;

namespace {

const std::array<Descriptor, 3> kMonitors{{
    Descriptor{Rect{0, 0, 1920, 1080}, 96, true},
    Descriptor{Rect{1920, 0, 3840, 1080}, 144, false},
    Descriptor{Rect{0, 1080, 1920, 2160}, 96, false},
}};

constexpr std::size_t kNoMonitor = std::numeric_limits<std::size_t>::max();

} // namespace

TEST_CASE("contains uses half-open bounds") {
    const Rect rect{0, 0, 100, 100};
    CHECK(contains(rect, Point{0, 0}));
    CHECK(contains(rect, Point{99, 99}));
    CHECK_FALSE(contains(rect, Point{100, 50}));
    CHECK_FALSE(contains(rect, Point{50, 100}));
    CHECK_FALSE(contains(rect, Point{-1, 50}));
}

TEST_CASE("distanceSquared is zero inside and on the edge of a rect") {
    const Rect rect{0, 0, 100, 100};
    CHECK(distanceSquared(rect, Point{50, 50}) == 0);
    CHECK(distanceSquared(rect, Point{0, 50}) == 0);
}

TEST_CASE("distanceSquared measures distance to the nearest point") {
    const Rect rect{0, 0, 100, 100};
    CHECK(distanceSquared(rect, Point{110, 50}) == 100);
    CHECK(distanceSquared(rect, Point{-5, 50}) == 25);
    CHECK(distanceSquared(rect, Point{110, 110}) == 200);
}

TEST_CASE("distanceSquared saturates at INT_MAX") {
    const Rect rect{0, 0, 10, 10};
    CHECK(distanceSquared(rect, Point{std::numeric_limits<int>::max(),
                                      std::numeric_limits<int>::max()}) ==
          std::numeric_limits<int>::max());
    CHECK(distanceSquared(rect, Point{46350, 46350}) ==
          std::numeric_limits<int>::max());
}

TEST_CASE("cursor policy selects the monitor containing the cursor") {
    const std::size_t index = select(MonitorSelection{Monitor::Cursor, 0},
                                     Point{2000, 100}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 1);
}

TEST_CASE("cursor policy picks the nearest monitor when the cursor is outside all") {
    const std::size_t index = select(MonitorSelection{Monitor::Cursor, 0},
                                     Point{2000, 2000}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 2);
}

TEST_CASE("cursor policy resolves distance ties to the lowest index") {
    const std::array<Descriptor, 2> monitors{{
        Descriptor{Rect{0, 0, 100, 100}, 96, false},
        Descriptor{Rect{200, 0, 300, 100}, 96, false},
    }};
    const std::size_t index = select(MonitorSelection{Monitor::Cursor, 0},
                                     Point{150, 50}, Rect{0, 0, 1, 1}, monitors);
    CHECK(index == 0);
}

TEST_CASE("active policy selects the monitor containing the window center") {
    const std::size_t index = select(MonitorSelection{Monitor::Active, 0},
                                     Point{0, 0}, Rect{1900, 100, 2000, 200}, kMonitors);
    CHECK(index == 1);
}

TEST_CASE("active policy falls back to the top-left when the center is outside") {
    const std::array<Descriptor, 1> monitors{{
        Descriptor{Rect{0, 0, 1000, 1000}, 96, false},
    }};
    const std::size_t index = select(MonitorSelection{Monitor::Active, 0},
                                     Point{0, 0}, Rect{100, 100, 5000, 200}, monitors);
    CHECK(index == 0);
}

TEST_CASE("active policy falls back to nearest center when neither point is contained") {
    const std::array<Descriptor, 1> monitors{{
        Descriptor{Rect{0, 0, 1000, 1000}, 96, false},
    }};
    const std::size_t index = select(MonitorSelection{Monitor::Active, 0},
                                     Point{0, 0}, Rect{2000, 2000, 2100, 2100}, monitors);
    CHECK(index == 0);
}

TEST_CASE("active policy handles a window spanning two monitors") {
    const std::size_t index = select(MonitorSelection{Monitor::Active, 0},
                                     Point{0, 0}, Rect{1000, 100, 3000, 500}, kMonitors);
    CHECK(index == 1);
}

TEST_CASE("primary policy selects the primary descriptor") {
    const std::size_t index = select(MonitorSelection{Monitor::Primary, 0},
                                     Point{2000, 100}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 0);
}

TEST_CASE("primary policy uses the fallback index when no descriptor is primary") {
    const std::array<Descriptor, 3> monitors{{
        Descriptor{Rect{0, 0, 100, 100}, 96, false},
        Descriptor{Rect{100, 0, 200, 100}, 96, false},
        Descriptor{Rect{200, 0, 300, 100}, 96, false},
    }};
    const std::size_t index = select(MonitorSelection{Monitor::Primary, 0},
                                     Point{0, 0}, Rect{0, 0, 1, 1}, monitors, 2);
    CHECK(index == 2);
}

TEST_CASE("primary policy uses zero when no primary and the fallback is out of range") {
    const std::array<Descriptor, 2> monitors{{
        Descriptor{Rect{0, 0, 100, 100}, 96, false},
        Descriptor{Rect{100, 0, 200, 100}, 96, false},
    }};
    const std::size_t index = select(MonitorSelection{Monitor::Primary, 0},
                                     Point{0, 0}, Rect{0, 0, 1, 1}, monitors, 99);
    CHECK(index == 0);
}

TEST_CASE("index policy selects the zero-based index") {
    const std::size_t index = select(MonitorSelection{Monitor::Index, 2},
                                     Point{0, 0}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 2);
}

TEST_CASE("index policy falls back to cursor when out of range") {
    const std::size_t index = select(MonitorSelection{Monitor::Index, 99},
                                     Point{2000, 100}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 1);
}

TEST_CASE("index policy falls back to cursor for a negative index") {
    const std::size_t index = select(MonitorSelection{Monitor::Index, -1},
                                     Point{100, 100}, Rect{0, 0, 1, 1}, kMonitors);
    CHECK(index == 0);
}

TEST_CASE("an empty monitor list returns the sentinel") {
    const std::size_t index = select(MonitorSelection{Monitor::Cursor, 0},
                                     Point{0, 0}, Rect{0, 0, 1, 1},
                                     std::span<const Descriptor>{});
    CHECK(index == kNoMonitor);
}

TEST_CASE("mixed dpi descriptors do not affect selection") {
    const std::array<Descriptor, 3> monitors{{
        Descriptor{Rect{0, 0, 1920, 1080}, 96, false},
        Descriptor{Rect{1920, 0, 3840, 1080}, 144, true},
        Descriptor{Rect{0, 1080, 1920, 2160}, 192, false},
    }};
    const std::size_t byCursor = select(MonitorSelection{Monitor::Cursor, 0},
                                        Point{2000, 100}, Rect{0, 0, 1, 1}, monitors);
    CHECK(byCursor == 1);
    const std::size_t byPrimary = select(MonitorSelection{Monitor::Primary, 0},
                                         Point{0, 0}, Rect{0, 0, 1, 1}, monitors);
    CHECK(byPrimary == 1);
}
