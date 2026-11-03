#include "core/geometry/arc.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pf {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double normalize_positive(double angle) noexcept {
    double value = std::fmod(angle, kTwoPi);
    if (value < 0.0) value += kTwoPi;
    return value;
}

bool angle_in_sweep(double angle, double start, double sweep) noexcept {
    if (std::abs(sweep) >= kTwoPi - 1e-12) return true;
    if (sweep >= 0.0) return normalize_positive(angle - start) <= sweep + 1e-12;
    return normalize_positive(start - angle) <= -sweep + 1e-12;
}

} // namespace

double ArcSegment::length() const noexcept {
    return std::abs(sweep_angle) * std::abs(radius);
}

Vec2 ArcSegment::point_at(double t) const noexcept {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double angle = start_angle + sweep_angle * clamped;
    return center + Vec2{std::cos(angle), std::sin(angle)} * radius;
}

Vec2 ArcSegment::tangent_at(double t) const noexcept {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double angle = start_angle + sweep_angle * clamped;
    const double sign = sweep_angle >= 0.0 ? 1.0 : -1.0;
    return Vec2{-std::sin(angle), std::cos(angle)} * sign;
}

BBox ArcSegment::bbox() const noexcept {
    BBox box;
    box.expand(point_at(0.0));
    box.expand(point_at(1.0));

    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const double angle = static_cast<double>(quadrant) * std::numbers::pi / 2.0;
        if (angle_in_sweep(angle, start_angle, sweep_angle)) {
            box.expand(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
        }
    }
    return box;
}

std::vector<Vec2> ArcSegment::flatten(double tolerance_mm) const {
    std::vector<Vec2> points;
    if (!is_valid()) {
        points.push_back(point_at(0.0));
        points.push_back(point_at(1.0));
        return points;
    }

    const double abs_radius = std::abs(radius);
    const double tolerance = std::max(tolerance_mm, 1e-6);
    double step = std::numbers::pi / 2.0;
    if (tolerance < abs_radius) {
        step = 2.0 * std::acos(std::clamp(1.0 - tolerance / abs_radius, -1.0, 1.0));
    }
    step = std::clamp(step, 1e-4, std::numbers::pi / 2.0);

    const int segments =
        std::clamp(static_cast<int>(std::ceil(std::abs(sweep_angle) / step)), 1, 4096);
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        points.push_back(point_at(static_cast<double>(i) / static_cast<double>(segments)));
    }
    return points;
}

Vec2 ArcSegment::closest_point(Vec2 point, double* t_out) const noexcept {
    if (!is_valid()) {
        if (t_out != nullptr) *t_out = 0.0;
        return point_at(0.0);
    }

    const Vec2 offset = point - center;
    double best_t = 0.0;
    double best_distance_squared = distance_squared(point, point_at(0.0));

    const double candidate_end = distance_squared(point, point_at(1.0));
    if (candidate_end < best_distance_squared) {
        best_distance_squared = candidate_end;
        best_t = 1.0;
    }

    if (offset.length_squared() > 1e-18) {
        const double angle = std::atan2(offset.y, offset.x);
        double t = 0.0;
        bool inside = false;
        if (sweep_angle >= 0.0) {
            const double delta = normalize_positive(angle - start_angle);
            if (delta <= sweep_angle + 1e-12) {
                t = sweep_angle > 1e-12 ? delta / sweep_angle : 0.0;
                inside = true;
            }
        } else {
            const double delta = normalize_positive(start_angle - angle);
            if (delta <= -sweep_angle + 1e-12) {
                t = -sweep_angle > 1e-12 ? delta / -sweep_angle : 0.0;
                inside = true;
            }
        }

        if (inside) {
            t = std::clamp(t, 0.0, 1.0);
            const double candidate = distance_squared(point, point_at(t));
            if (candidate < best_distance_squared) {
                best_distance_squared = candidate;
                best_t = t;
            }
        }
    }

    if (t_out != nullptr) *t_out = best_t;
    return point_at(best_t);
}

} // namespace pf
