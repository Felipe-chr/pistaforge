#include "core/geometry/bezier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pf {
namespace {

constexpr std::array<double, 12> kGaussNodes{
    0.1252334085114689, 0.3678314989981802, 0.5873179542866175, 0.7699026741943047,
    0.9041172563704749, 0.9815606342467192,
};

constexpr std::array<double, 12> kGaussWeights{
    0.2491470458134028, 0.2334925365383548, 0.2031674267230659, 0.1600783285433462,
    0.1069393259953184, 0.0471753363865118,
};

/// Resolve a t² + b t + c = 0 de forma numericamente estável.
int solve_quadratic(double a, double b, double c, double roots[2]) noexcept {
    constexpr double kZero = 1e-12;
    if (std::abs(a) < kZero) {
        if (std::abs(b) < kZero) return 0;
        roots[0] = -c / b;
        return 1;
    }

    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return 0;
    if (discriminant == 0.0) {
        roots[0] = -b / (2.0 * a);
        return 1;
    }

    const double sqrt_disc = std::sqrt(discriminant);
    const double q = -0.5 * (b + std::copysign(sqrt_disc, b));
    roots[0] = q / a;
    roots[1] = c / q;
    return 2;
}

double gauss_length(const CubicBezier& curve, double t0, double t1) noexcept {
    const double half = 0.5 * (t1 - t0);
    const double middle = 0.5 * (t1 + t0);
    double sum = 0.0;
    for (std::size_t i = 0; i < kGaussNodes.size(); ++i) {
        const double offset = half * kGaussNodes[i];
        sum += kGaussWeights[i] * (curve.derivative(middle + offset).length() +
                                   curve.derivative(middle - offset).length());
    }
    return half * sum;
}

double adaptive_length(const CubicBezier& curve, double t0, double t1, double whole,
                       double tolerance, int depth) noexcept {
    const double middle = 0.5 * (t0 + t1);
    const double left = gauss_length(curve, t0, middle);
    const double right = gauss_length(curve, middle, t1);
    if (depth >= 20) return left + right;
    if (std::abs(left + right - whole) <= tolerance) return left + right;
    return adaptive_length(curve, t0, middle, left, tolerance * 0.5, depth + 1) +
           adaptive_length(curve, middle, t1, right, tolerance * 0.5, depth + 1);
}

double point_line_distance(Vec2 point, Vec2 a, Vec2 b) noexcept {
    const Vec2 ab = b - a;
    const double length_squared = ab.length_squared();
    if (length_squared <= 1e-18) return (point - a).length();
    return std::abs(ab.cross(point - a)) / std::sqrt(length_squared);
}

void flatten_recursive(const CubicBezier& curve, double tolerance, int depth,
                       std::vector<Vec2>& output) {
    const double flatness = std::max(point_line_distance(curve.p1, curve.p0, curve.p3),
                                     point_line_distance(curve.p2, curve.p0, curve.p3));
    if (depth >= 24 || flatness <= tolerance) {
        output.push_back(curve.p3);
        return;
    }

    const auto [left, right] = curve.split(0.5);
    flatten_recursive(left, tolerance, depth + 1, output);
    flatten_recursive(right, tolerance, depth + 1, output);
}

} // namespace

Vec2 CubicBezier::eval(double t) const noexcept {
    const double u = 1.0 - t;
    const double u2 = u * u;
    const double t2 = t * t;
    return p0 * (u2 * u) + p1 * (3.0 * u2 * t) + p2 * (3.0 * u * t2) + p3 * (t2 * t);
}

Vec2 CubicBezier::derivative(double t) const noexcept {
    const double u = 1.0 - t;
    return (p1 - p0) * (3.0 * u * u) + (p2 - p1) * (6.0 * u * t) + (p3 - p2) * (3.0 * t * t);
}

Vec2 CubicBezier::second_derivative(double t) const noexcept {
    const double u = 1.0 - t;
    return (p2 - p1 * 2.0 + p0) * (6.0 * u) + (p3 - p2 * 2.0 + p1) * (6.0 * t);
}

double CubicBezier::curvature(double t) const noexcept {
    const Vec2 first = derivative(t);
    const Vec2 second = second_derivative(t);
    const double speed_squared = first.dot(first);
    if (speed_squared < 1e-18) return 0.0;
    return first.cross(second) / (speed_squared * std::sqrt(speed_squared));
}

std::pair<CubicBezier, CubicBezier> CubicBezier::split(double t) const noexcept {
    const Vec2 p01 = lerp(p0, p1, t);
    const Vec2 p12 = lerp(p1, p2, t);
    const Vec2 p23 = lerp(p2, p3, t);
    const Vec2 p012 = lerp(p01, p12, t);
    const Vec2 p123 = lerp(p12, p23, t);
    const Vec2 p0123 = lerp(p012, p123, t);

    return {CubicBezier{p0, p01, p012, p0123}, CubicBezier{p0123, p123, p23, p3}};
}

BBox CubicBezier::bbox() const noexcept {
    BBox box;
    box.expand(p0);
    box.expand(p3);

    const auto add_extrema = [&box, this](double c0, double c1, double c2, double c3) {
        const double a = -c0 + 3.0 * c1 - 3.0 * c2 + c3;
        const double b = 2.0 * (c0 - 2.0 * c1 + c2);
        const double c = c1 - c0;

        double roots[2]{};
        const int count = solve_quadratic(a, b, c, roots);
        for (int i = 0; i < count; ++i) {
            const double t = roots[i];
            if (t > 0.0 && t < 1.0) box.expand(eval(t));
        }
    };

    add_extrema(p0.x, p1.x, p2.x, p3.x);
    add_extrema(p0.y, p1.y, p2.y, p3.y);
    return box;
}

double CubicBezier::length(double tolerance) const {
    const double safe_tolerance = std::max(tolerance, 1e-12);
    return adaptive_length(*this, 0.0, 1.0, gauss_length(*this, 0.0, 1.0), safe_tolerance, 0);
}

std::vector<Vec2> CubicBezier::flatten(double tolerance_mm) const {
    const double tolerance = std::max(tolerance_mm, 1e-6);
    std::vector<Vec2> output;
    output.reserve(16);
    output.push_back(p0);
    flatten_recursive(*this, tolerance, 0, output);
    return output;
}

Vec2 CubicBezier::closest_point(Vec2 point, double* t_out) const {
    constexpr int kSamples = 64;
    double best_t = 0.0;
    double best_distance_squared = std::numeric_limits<double>::max();

    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples);
        const double d2 = distance_squared(eval(t), point);
        if (d2 < best_distance_squared) {
            best_distance_squared = d2;
            best_t = t;
        }
    }

    double t = best_t;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const Vec2 difference = eval(t) - point;
        const Vec2 first = derivative(t);
        const Vec2 second = second_derivative(t);
        const double f = difference.dot(first);
        const double f_prime = first.dot(first) + difference.dot(second);
        if (std::abs(f_prime) < 1e-14) break;

        const double next = std::clamp(t - f / f_prime, 0.0, 1.0);
        if (std::abs(next - t) < 1e-13) {
            t = next;
            break;
        }
        t = next;
    }

    const double refined_distance_squared = distance_squared(eval(t), point);
    if (refined_distance_squared < best_distance_squared) {
        best_distance_squared = refined_distance_squared;
        best_t = t;
    }

    if (t_out != nullptr) *t_out = best_t;
    return eval(best_t);
}

bool CubicBezier::is_degenerate(double epsilon) const noexcept {
    const double d1 = distance(p0, p1);
    const double d2 = distance(p1, p2);
    const double d3 = distance(p2, p3);
    const double d4 = distance(p0, p3);
    return std::max({d1, d2, d3, d4}) <= epsilon;
}

} // namespace pf
