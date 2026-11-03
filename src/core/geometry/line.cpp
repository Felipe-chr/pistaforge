#include "core/geometry/line.hpp"

#include <algorithm>

namespace pf {

Vec2 LineSegment::closest_point(Vec2 point, double* t_out) const noexcept {
    const Vec2 ab = b - a;
    const double length_squared = ab.length_squared();
    double t = 0.0;
    if (length_squared > 1e-18) {
        t = std::clamp((point - a).dot(ab) / length_squared, 0.0, 1.0);
    }
    if (t_out != nullptr) *t_out = t;
    return a + ab * t;
}

} // namespace pf
