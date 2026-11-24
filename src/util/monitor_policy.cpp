#include "util/monitor_policy.h"

#include <cstdint>
#include <limits>

namespace tapir::monitor_policy {
namespace {

constexpr std::int64_t kMaxDistance = std::numeric_limits<int>::max();

int saturatingSquare(std::int64_t value) {
    if (value > 46340) return static_cast<int>(kMaxDistance);
    return static_cast<int>(value * value);
}

std::size_t nearest(std::span<const Descriptor> monitors, Point point) {
    std::size_t best = 0;
    int bestDistance = distanceSquared(monitors[0].work, point);
    for (std::size_t i = 1; i < monitors.size(); ++i) {
        const int distance = distanceSquared(monitors[i].work, point);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

Point centerOf(const placement::Rect& rect) {
    return Point{
        static_cast<int>((static_cast<std::int64_t>(rect.left) + rect.right) / 2),
        static_cast<int>((static_cast<std::int64_t>(rect.top) + rect.bottom) / 2)};
}

} // namespace

bool contains(const placement::Rect& rect, Point point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

int distanceSquared(const placement::Rect& rect, Point point) {
    std::int64_t dx = 0;
    if (point.x < rect.left) {
        dx = static_cast<std::int64_t>(rect.left) - point.x;
    } else if (point.x > rect.right) {
        dx = static_cast<std::int64_t>(point.x) - rect.right;
    }

    std::int64_t dy = 0;
    if (point.y < rect.top) {
        dy = static_cast<std::int64_t>(rect.top) - point.y;
    } else if (point.y > rect.bottom) {
        dy = static_cast<std::int64_t>(point.y) - rect.bottom;
    }

    const std::int64_t sx = saturatingSquare(dx);
    const std::int64_t sy = saturatingSquare(dy);
    if (sx >= kMaxDistance || sy >= kMaxDistance || sx + sy > kMaxDistance) {
        return static_cast<int>(kMaxDistance);
    }
    return static_cast<int>(sx + sy);
}

std::size_t select(const MonitorSelection& policy, Point cursor,
                   const placement::Rect& activeWindow,
                   std::span<const Descriptor> monitors,
                   std::size_t fallbackIndex) {
    if (monitors.empty()) return std::numeric_limits<std::size_t>::max();

    switch (policy.kind) {
    case Monitor::Cursor:
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (contains(monitors[i].work, cursor)) return i;
        }
        return nearest(monitors, cursor);

    case Monitor::Active: {
        const Point center = centerOf(activeWindow);
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (contains(monitors[i].work, center)) return i;
        }
        const Point topLeft{activeWindow.left, activeWindow.top};
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (contains(monitors[i].work, topLeft)) return i;
        }
        return nearest(monitors, center);
    }

    case Monitor::Primary:
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (monitors[i].primary) return i;
        }
        return fallbackIndex < monitors.size() ? fallbackIndex : 0;

    case Monitor::Index:
        if (policy.index >= 0 &&
            static_cast<std::size_t>(policy.index) < monitors.size()) {
            return static_cast<std::size_t>(policy.index);
        }
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (contains(monitors[i].work, cursor)) return i;
        }
        return nearest(monitors, cursor);
    }

    return 0;
}

} // namespace tapir::monitor_policy
