#pragma once

#include "core/dsl.h"

#include <cstddef>

namespace core::dsl {

inline bool polygonContains(const Element& element, double pointX, double pointY, float dpiScale, const Rect& bounds) {
    if (element.polygonPoints.size() < 3 || !bounds.contains(pointX, pointY)) {
        return false;
    }

    bool inside = false;
    const double localX = pointX - bounds.x;
    const double localY = pointY - bounds.y;
    std::size_t previous = element.polygonPoints.size() - 1;
    for (std::size_t current = 0; current < element.polygonPoints.size(); ++current) {
        const Vec2& a = element.polygonPoints[current];
        const Vec2& b = element.polygonPoints[previous];
        const double ax = static_cast<double>(a.x) * dpiScale;
        const double ay = static_cast<double>(a.y) * dpiScale;
        const double bx = static_cast<double>(b.x) * dpiScale;
        const double by = static_cast<double>(b.y) * dpiScale;
        const double denominator = by - ay;
        const bool crosses = ((ay > localY) != (by > localY)) &&
            (localX < (bx - ax) * (localY - ay) / denominator + ax);
        if (crosses) {
            inside = !inside;
        }
        previous = current;
    }
    return inside;
}

} // namespace core::dsl
