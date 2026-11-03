#include <numbers>

#include <doctest/doctest.h>

#include "core/geometry/vec2.hpp"

using pf::Vec2;

TEST_CASE("Vec2: operações básicas") {
    const Vec2 a{3.0, 4.0};
    CHECK(a.length() == doctest::Approx(5.0));
    CHECK(a.length_squared() == doctest::Approx(25.0));
    CHECK(a.normalized().length() == doctest::Approx(1.0));

    const Vec2 b{1.0, 2.0};
    CHECK((a + b) == Vec2{4.0, 6.0});
    CHECK((a - b) == Vec2{2.0, 2.0});
    CHECK((a * 2.0) == Vec2{6.0, 8.0});
    CHECK((2.0 * a) == Vec2{6.0, 8.0});
    CHECK((a / 2.0) == Vec2{1.5, 2.0});
    CHECK((-a) == Vec2{-3.0, -4.0});
}

TEST_CASE("Vec2: produto escalar e vetorial") {
    const Vec2 x{1.0, 0.0};
    const Vec2 y{0.0, 1.0};
    CHECK(x.dot(y) == doctest::Approx(0.0));
    CHECK(x.cross(y) == doctest::Approx(1.0));
    CHECK(y.cross(x) == doctest::Approx(-1.0));
    CHECK(x.dot(x) == doctest::Approx(1.0));
}

TEST_CASE("Vec2: perpendicular e rotação") {
    CHECK(Vec2{1.0, 0.0}.perpendicular() == Vec2{0.0, 1.0});

    const Vec2 rotated = Vec2{1.0, 0.0}.rotated(std::numbers::pi / 2.0);
    CHECK(rotated.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(rotated.y == doctest::Approx(1.0));

    const Vec2 back = rotated.rotated(-std::numbers::pi / 2.0);
    CHECK(back.x == doctest::Approx(1.0));
    CHECK(back.y == doctest::Approx(0.0).epsilon(1e-12));
}

TEST_CASE("Vec2: normalização de vetor nulo") {
    CHECK(Vec2{}.normalized() == Vec2{});
}

TEST_CASE("Vec2: interpolação e distância") {
    CHECK(pf::lerp(Vec2{0.0, 0.0}, Vec2{10.0, 20.0}, 0.5) == Vec2{5.0, 10.0});
    CHECK(pf::distance(Vec2{0.0, 0.0}, Vec2{3.0, 4.0}) == doctest::Approx(5.0));
}
