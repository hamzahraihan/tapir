#include "util/placement.h"

#include <algorithm>

namespace tapir::placement {
namespace {

int toPixels(int dip, unsigned dpi) {
    return (dip * static_cast<int>(dpi) + 48) / 96;
}

} // namespace

Rect compute(const Rect& monitor, int widthDip, int heightDip, unsigned dpi, Position position) {
    const int monitorWidth = monitor.right - monitor.left;
    const int monitorHeight = monitor.bottom - monitor.top;

    const int width = std::min(toPixels(widthDip, dpi), monitorWidth);
    const int height = std::min(toPixels(heightDip, dpi), monitorHeight);

    const int x = monitor.left + (monitorWidth - width) / 2;
    const int freeY = monitorHeight - height;
    const int y = monitor.top + (position == Position::UpperCenter ? freeY / 3 : freeY / 2);

    return Rect{x, y, x + width, y + height};
}

} // namespace tapir::placement
