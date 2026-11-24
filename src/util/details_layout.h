#pragma once

namespace tapir::details_layout {

inline constexpr int kWidthDip = 280;

struct Widths {
    int listDip;
    int paneDip;
};

Widths partition(int totalWidthDip, bool showPane);

} // namespace tapir::details_layout
