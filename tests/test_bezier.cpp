#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "core/geometry/bezier.hpp"

using pf::CubicBezier;
using pf::Vec2;

namespace {

double distance_point_segment(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double length_squared = ab.length_squared();
    if (length_squared <= 1e-18) return pf::distance(point, a);
    const double t = std::clamp((point - a).dot(ab) / length_squared, 0.0, 1.0);
    return pf::distance(point, a + ab * t);
}

double distance_to_polyline(Vec2 point, const std::vector<Vec2>& polyline) {
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < polyline.size(); ++i) {
        best = std::min(best, distance_point_segment(point, polyline[i - 1], polyline[i]));
    }
    return best;
}

/// Aproximação padrão de um quarto de círculo de raio R.
CubicBezier quarter_circle(double radius) {
    constexpr double kappa = 0.5522847498307936;
    const double k = kappa * radius;
    return CubicBezier{
        {radius, 0.0},
        {radius, k},
        {k, radius},
        {0.0, radius},
    };
}

} // namespace

TEST_CASE("Bézier: extremos e derivadas") {
    const CubicBezier curve{{0.0, 0.0}, {0.0, 10.0}, {10.0, 10.0}, {10.0, 0.0}};

    CHECK(curve.eval(0.0) == curve.p0);
    CHECK(curve.eval(1.0) == curve.p3);
    CHECK(pf::distance(curve.derivative(0.0), (curve.p1 - curve.p0) * 3.0) < 1e-12);
    CHECK(pf::distance(curve.derivative(1.0), (curve.p3 - curve.p2) * 3.0) < 1e-12);
    CHECK(curve.eval(0.5).x == doctest::Approx(5.0));
    CHECK(curve.eval(0.5).y == doctest::Approx(7.5));
}

TEST_CASE("Bézier: split preserva a geometria") {
    const CubicBezier curve{{0.0, 0.0}, {20.0, 80.0}, {90.0, -20.0}, {100.0, 60.0}};

    for (const double t : {0.1, 0.37, 0.5, 0.85}) {
        const auto [left, right] = curve.split(t);

        CHECK(pf::distance(left.eval(0.0), curve.eval(0.0)) < 1e-12);
        CHECK(pf::distance(left.eval(1.0), curve.eval(t)) < 1e-12);
        CHECK(pf::distance(right.eval(0.0), curve.eval(t)) < 1e-12);
        CHECK(pf::distance(right.eval(1.0), curve.eval(1.0)) < 1e-12);

        for (int i = 0; i <= 10; ++i) {
            const double s = static_cast<double>(i) / 10.0;
            CHECK(pf::distance(left.eval(s), curve.eval(s * t)) < 1e-9);
            CHECK(pf::distance(right.eval(s), curve.eval(t + s * (1.0 - t))) < 1e-9);
        }
    }
}

TEST_CASE("Bézier: comprimento de uma reta") {
    const CubicBezier curve{{0.0, 0.0}, {30.0, 0.0}, {70.0, 0.0}, {100.0, 0.0}};
    CHECK(curve.length() == doctest::Approx(100.0).epsilon(1e-12));
}

TEST_CASE("Bézier: comprimento de um quarto de círculo") {
    constexpr double radius = 100.0;
    const CubicBezier curve = quarter_circle(radius);
    const double expected = std::numbers::pi * radius / 2.0;
    CHECK(curve.length() == doctest::Approx(expected).epsilon(0.002));
}

TEST_CASE("Bézier: curvatura") {
    const CubicBezier straight{{0.0, 0.0}, {30.0, 0.0}, {70.0, 0.0}, {100.0, 0.0}};
    CHECK(straight.curvature(0.3) == doctest::Approx(0.0).epsilon(1e-12));

    constexpr double radius = 100.0;
    const CubicBezier circle = quarter_circle(radius);
    CHECK(circle.curvature(0.0) == doctest::Approx(1.0 / radius).epsilon(0.03));
    CHECK(circle.curvature(1.0) == doctest::Approx(1.0 / radius).epsilon(0.03));
}

TEST_CASE("Bézier: bbox exata inclui extremos internos") {
    const CubicBezier curve{{0.0, 0.0}, {0.0, 10.0}, {10.0, 10.0}, {10.0, 0.0}};
    const pf::BBox box = curve.bbox();

    REQUIRE(box.is_valid());
    CHECK(box.min.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(box.min.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(box.max.x == doctest::Approx(10.0).epsilon(1e-12));
    CHECK(box.max.y == doctest::Approx(7.5).epsilon(1e-9));
}

TEST_CASE("Bézier: flatten respeita a tolerância") {
    const CubicBezier straight{{0.0, 0.0}, {30.0, 0.0}, {70.0, 0.0}, {100.0, 0.0}};
    const std::vector<Vec2> straight_points = straight.flatten(0.01);
    CHECK(straight_points.size() == 2);
    CHECK(straight_points.front() == straight.p0);
    CHECK(straight_points.back() == straight.p3);

    const CubicBezier curve = quarter_circle(200.0);
    constexpr double tolerance = 0.01;
    const std::vector<Vec2> points = curve.flatten(tolerance);

    REQUIRE(points.size() > 2);
    CHECK(points.front() == curve.p0);
    CHECK(points.back() == curve.p3);

    for (int i = 0; i <= 400; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        CHECK(distance_to_polyline(curve.eval(t), points) <= tolerance * 1.5);
    }
}

TEST_CASE("Bézier: ponto mais próximo") {
    const CubicBezier curve{{0.0, 0.0}, {3.0, 0.0}, {7.0, 0.0}, {10.0, 0.0}};
    double t = -1.0;
    const Vec2 closest = curve.closest_point({5.0, 2.0}, &t);

    CHECK(t == doctest::Approx(0.5).epsilon(1e-6));
    CHECK(closest.x == doctest::Approx(5.0).epsilon(1e-6));
    CHECK(closest.y == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(pf::distance(closest, Vec2{5.0, 2.0}) == doctest::Approx(2.0));
}

TEST_CASE("Bézier: curva degenerada") {
    const CubicBezier point{{1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};
    CHECK(point.is_degenerate());
    CHECK(point.length() == doctest::Approx(0.0).epsilon(1e-12));
}
