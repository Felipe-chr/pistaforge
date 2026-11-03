#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "core/geometry/arc.hpp"

using pf::ArcSegment;
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

ArcSegment quarter_arc(double radius) {
    return ArcSegment{
        .center = {0.0, 0.0},
        .radius = radius,
        .start_angle = 0.0,
        .sweep_angle = std::numbers::pi / 2.0,
    };
}

} // namespace

TEST_CASE("Arco: comprimento e pontos") {
    const ArcSegment arc = quarter_arc(100.0);

    CHECK(arc.length() == doctest::Approx(std::numbers::pi * 50.0));
    CHECK(arc.point_at(0.0) == Vec2{100.0, 0.0});
    CHECK(arc.point_at(0.5).x == doctest::Approx(100.0 / std::sqrt(2.0)));
    CHECK(arc.point_at(0.5).y == doctest::Approx(100.0 / std::sqrt(2.0)));
    CHECK(arc.point_at(1.0).x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(arc.point_at(1.0).y == doctest::Approx(100.0));
}

TEST_CASE("Arco: tangentes nos extremos") {
    const ArcSegment clockwise = quarter_arc(100.0);
    CHECK(clockwise.tangent_at(0.0) == Vec2{0.0, 1.0});
    CHECK(clockwise.tangent_at(1.0).x == doctest::Approx(-1.0).epsilon(1e-12));
    CHECK(clockwise.tangent_at(1.0).y == doctest::Approx(0.0).epsilon(1e-12));

    const ArcSegment counter_clockwise{
        .center = {0.0, 0.0},
        .radius = 100.0,
        .start_angle = std::numbers::pi / 2.0,
        .sweep_angle = -std::numbers::pi / 2.0,
    };
    CHECK(counter_clockwise.point_at(0.0).x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(counter_clockwise.point_at(0.0).y == doctest::Approx(100.0));
    CHECK(counter_clockwise.point_at(1.0).x == doctest::Approx(100.0));
    CHECK(counter_clockwise.point_at(1.0).y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(counter_clockwise.tangent_at(0.0).x == doctest::Approx(1.0));
    CHECK(counter_clockwise.tangent_at(0.0).y == doctest::Approx(0.0).epsilon(1e-12));
}

TEST_CASE("Arco: bbox de um quarto de círculo") {
    const pf::BBox box = quarter_arc(100.0).bbox();

    REQUIRE(box.is_valid());
    CHECK(box.min.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(box.min.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(box.max.x == doctest::Approx(100.0));
    CHECK(box.max.y == doctest::Approx(100.0));
}

TEST_CASE("Arco: ponto mais próximo") {
    const ArcSegment arc = quarter_arc(100.0);

    double t = -1.0;
    const Vec2 closest = arc.closest_point({100.0, 100.0}, &t);
    CHECK(t == doctest::Approx(0.5).epsilon(1e-6));
    CHECK(closest.x == doctest::Approx(100.0 / std::sqrt(2.0)));
    CHECK(closest.y == doctest::Approx(100.0 / std::sqrt(2.0)));

    const Vec2 outside = arc.closest_point({300.0, 0.0}, &t);
    CHECK(t == doctest::Approx(0.0));
    CHECK(outside == Vec2{100.0, 0.0});
}

TEST_CASE("Arco: flatten respeita a tolerância") {
    const ArcSegment arc = quarter_arc(200.0);
    constexpr double tolerance = 0.01;
    const std::vector<Vec2> points = arc.flatten(tolerance);

    REQUIRE(points.size() > 2);
    CHECK(points.front() == arc.point_at(0.0));
    CHECK(points.back() == arc.point_at(1.0));

    for (int i = 0; i <= 400; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        CHECK(distance_to_polyline(arc.point_at(t), points) <= tolerance * 1.5);
    }
}

TEST_CASE("Arco: validade") {
    CHECK(quarter_arc(10.0).is_valid());
    CHECK_FALSE((ArcSegment{{0.0, 0.0}, 0.0, 0.0, 1.0}).is_valid());
    CHECK_FALSE((ArcSegment{{0.0, 0.0}, 10.0, 0.0, 0.0}).is_valid());

    const ArcSegment invalid{{0.0, 0.0}, 0.0, 0.0, 0.0};
    double t = -1.0;
    CHECK(invalid.closest_point({1.0, 1.0}, &t) == invalid.point_at(0.0));
    CHECK(t == doctest::Approx(0.0));
}
