#pragma once

#include "config.h"
#include "util/placement.h"

#include <cstddef>
#include <span>

namespace tapir::monitor_policy {

struct Point {
    int x;
    int y;
};

struct Descriptor {
    placement::Rect work;
    unsigned dpi;
    bool primary;
};

bool contains(const placement::Rect& rect, Point point);

int distanceSquared(const placement::Rect& rect, Point point);

std::size_t select(const MonitorSelection& policy, Point cursor,
                   const placement::Rect& activeWindow,
                   std::span<const Descriptor> monitors,
                   std::size_t fallbackIndex = 0);

} // namespace tapir::monitor_policy
