#include <cmath>

#include <doctest/doctest.h>

#include "core/geometry/line.hpp"

using pf::LineSegment;
using pf::Vec2;

TEST_CASE("Reta: comprimento, ponto e tangente") {
    const LineSegment segment{{0.0, 0.0}, {3.0, 4.0}};

    CHECK(segment.length() == doctest::Approx(5.0));
    CHECK(segment.point_at(0.0) == segment.a);
    CHECK(segment.point_at(1.0) == segment.b);
    CHECK(segment.point_at(0.5) == Vec2{1.5, 2.0});
    CHECK(segment.tangent() == Vec2{0.6, 0.8});
}

TEST_CASE("Reta: ponto mais próximo") {
    const LineSegment segment{{0.0, 0.0}, {10.0, 0.0}};

    double t = -1.0;
    const Vec2 middle = segment.closest_point({5.0, 3.0}, &t);
    CHECK(t == doctest::Approx(0.5));
    CHECK(middle == Vec2{5.0, 0.0});

    const Vec2 before = segment.closest_point({-4.0, 2.0}, &t);
    CHECK(t == doctest::Approx(0.0));
    CHECK(before == segment.a);

    const Vec2 after = segment.closest_point({14.0, 2.0}, &t);
    CHECK(t == doctest::Approx(1.0));
    CHECK(after == segment.b);
}

TEST_CASE("Reta: bbox e flatten") {
    const LineSegment segment{{-2.0, 3.0}, {4.0, -1.0}};
    const pf::BBox box = segment.bbox();

    REQUIRE(box.is_valid());
    CHECK(box.min == Vec2{-2.0, -1.0});
    CHECK(box.max == Vec2{4.0, 3.0});

    const std::vector<Vec2> points = segment.flatten(0.01);
    REQUIRE(points.size() == 2);
    CHECK(points[0] == segment.a);
    CHECK(points[1] == segment.b);
}

TEST_CASE("Reta: segmento degenerado") {
    const LineSegment point{{2.0, 2.0}, {2.0, 2.0}};

    CHECK(point.length() == doctest::Approx(0.0));
    CHECK(point.tangent() == Vec2{0.0, 0.0});

    double t = -1.0;
    CHECK(point.closest_point({5.0, 5.0}, &t) == Vec2{2.0, 2.0});
    CHECK(t == doctest::Approx(0.0));
}
