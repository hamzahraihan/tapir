#pragma once

namespace tapir::placement {

struct Rect {
    int left;
    int top;
    int right;
    int bottom;
};

enum class Position { UpperCenter, Center };

inline constexpr int kDefaultWidthDip = 680;
inline constexpr int kCollapsedHeightDip = 56;

Rect compute(const Rect& monitor, int widthDip, int heightDip, unsigned dpi, Position position);

} // namespace tapir::placement
