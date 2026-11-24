#include "util/details_layout.h"

namespace tapir::details_layout {

Widths partition(int totalWidthDip, bool showPane) {
    if (!showPane || totalWidthDip <= kWidthDip) return {totalWidthDip, 0};
    return {totalWidthDip - kWidthDip, kWidthDip};
}

} // namespace tapir::details_layout
