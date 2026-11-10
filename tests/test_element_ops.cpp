#include <cmath>
#include <numbers>

#include <doctest/doctest.h>

#include "core/scene/element_ops.hpp"

using pf::ArcSegment;
using pf::CubicBezier;
using pf::LineSegment;
using pf::ModuleInstance;
using pf::Shape;
using pf::ShapeType;
using pf::Vec2;

namespace {

constexpr double kTolerance = 1e-6;

CubicBezier bowed_cubic() {
    return CubicBezier{
        .p0 = {0.0, 0.0},
        .p1 = {33.0, -40.0},
        .p2 = {66.0, -40.0},
        .p3 = {100.0, 0.0},
    };
}

ArcSegment quarter_arc(double radius) {
    return ArcSegment{
        .center = {0.0, 0.0},
        .radius = radius,
        .start_angle = 0.0,
        .sweep_angle = std::numbers::pi / 2.0,
    };
}

void check_endpoints(const Shape& shape, Vec2 start, Vec2 end) {
    const auto endpoints = pf::shape_endpoints(shape);
    REQUIRE(endpoints.has_value());
    CHECK(pf::distance(endpoints->first, start) < kTolerance);
    CHECK(pf::distance(endpoints->second, end) < kTolerance);
}

} // namespace

TEST_CASE("Formas: pontas e tipo inferido") {
    const Shape line = LineSegment{{0.0, 0.0}, {100.0, 0.0}};
    check_endpoints(line, {0.0, 0.0}, {100.0, 0.0});
    CHECK(pf::infer_shape_type(line) == ShapeType::Line);
    CHECK_FALSE(pf::shape_radius(line).has_value());

    const Shape cubic = bowed_cubic();
    check_endpoints(cubic, {0.0, 0.0}, {100.0, 0.0});
    CHECK(pf::infer_shape_type(cubic) == ShapeType::Cubic);

    const Shape arc = quarter_arc(100.0);
    check_endpoints(arc, {100.0, 0.0}, {0.0, 100.0});
    CHECK(pf::infer_shape_type(arc) == ShapeType::Arc90);
    REQUIRE(pf::shape_radius(arc).has_value());
    CHECK(*pf::shape_radius(arc) == doctest::Approx(100.0));
    REQUIRE(pf::shape_angle_deg(arc).has_value());
    CHECK(*pf::shape_angle_deg(arc) == doctest::Approx(90.0));
}

TEST_CASE("Formas: conversão preserva as pontas") {
    const Shape line = LineSegment{{0.0, 0.0}, {100.0, 0.0}};

    const auto arc = pf::convert_shape(line, ShapeType::Arc90);
    REQUIRE(arc.has_value());
    check_endpoints(*arc, {0.0, 0.0}, {100.0, 0.0});
    REQUIRE(pf::shape_radius(*arc).has_value());
    CHECK(*pf::shape_radius(*arc) == doctest::Approx(100.0 / std::sqrt(2.0)));

    const auto arc180 = pf::convert_shape(line, ShapeType::Arc180);
    REQUIRE(arc180.has_value());
    check_endpoints(*arc180, {0.0, 0.0}, {100.0, 0.0});
    REQUIRE(pf::shape_radius(*arc180).has_value());
    CHECK(*pf::shape_radius(*arc180) == doctest::Approx(50.0));

    const auto smooth = pf::smooth_shape(line);
    REQUIRE(smooth.has_value());
    check_endpoints(*smooth, {0.0, 0.0}, {100.0, 0.0});
    CHECK(std::holds_alternative<CubicBezier>(*smooth));

    const auto back = pf::convert_shape(*smooth, ShapeType::Line);
    REQUIRE(back.has_value());
    check_endpoints(*back, {0.0, 0.0}, {100.0, 0.0});
}

TEST_CASE("Formas: inverter mantém as pontas") {
    Shape cubic = bowed_cubic();
    const double before = std::get<CubicBezier>(cubic).eval(0.5).y;
    REQUIRE(pf::flip_shape(cubic));
    check_endpoints(cubic, {0.0, 0.0}, {100.0, 0.0});
    CHECK(std::get<CubicBezier>(cubic).eval(0.5).y == doctest::Approx(-before));

    Shape arc = quarter_arc(100.0);
    REQUIRE(pf::flip_shape(arc));
    check_endpoints(arc, {100.0, 0.0}, {0.0, 100.0});
    CHECK(std::get<ArcSegment>(arc).sweep_angle < 0.0);

    Shape line = LineSegment{{0.0, 0.0}, {100.0, 0.0}};
    CHECK_FALSE(pf::flip_shape(line));
}

TEST_CASE("Formas: espelhar em torno de um eixo vertical") {
    Shape line = LineSegment{{10.0, 0.0}, {50.0, 20.0}};
    CHECK(pf::mirror_shape(line, 0.0));
    const auto& mirrored = std::get<LineSegment>(line);
    CHECK(mirrored.a.x == doctest::Approx(-10.0));
    CHECK(mirrored.b.x == doctest::Approx(-50.0));
    CHECK(mirrored.a.y == doctest::Approx(0.0));
}

TEST_CASE("Formas: dividir reta e Bézier") {
    const Shape line = LineSegment{{0.0, 0.0}, {100.0, 0.0}};
    const auto parts = pf::split_shape(line, 0.5);
    REQUIRE(parts.has_value());
    const auto& first = std::get<LineSegment>(parts->first);
    const auto& second = std::get<LineSegment>(parts->second);
    CHECK(first.b.x == doctest::Approx(50.0));
    CHECK(second.a.x == doctest::Approx(50.0));
    CHECK(second.b.x == doctest::Approx(100.0));

    const Shape cubic = bowed_cubic();
    const Vec2 middle = std::get<CubicBezier>(cubic).eval(0.5);
    const auto split_cubic = pf::split_shape(cubic, 0.5);
    REQUIRE(split_cubic.has_value());
    check_endpoints(split_cubic->first, {0.0, 0.0}, middle);
    check_endpoints(split_cubic->second, middle, {100.0, 0.0});
}

TEST_CASE("Formas: ajustar comprimento, raio e ângulo") {
    Shape line = LineSegment{{0.0, 0.0}, {100.0, 0.0}};
    REQUIRE(pf::adjust_length(line, 250.0));
    const auto& resized = std::get<LineSegment>(line);
    CHECK(resized.a == Vec2{0.0, 0.0});
    CHECK(resized.b.x == doctest::Approx(250.0));

    Shape arc = quarter_arc(100.0);
    REQUIRE(pf::adjust_radius(arc, 1.5));
    check_endpoints(arc, {100.0, 0.0}, {0.0, 100.0});
    REQUIRE(pf::shape_radius(arc).has_value());
    CHECK(*pf::shape_radius(arc) == doctest::Approx(150.0));

    REQUIRE(pf::shape_angle_deg(arc).has_value());
    const double angle_before = *pf::shape_angle_deg(arc);
    REQUIRE(pf::adjust_angle(arc, 30.0));
    check_endpoints(arc, {100.0, 0.0}, {0.0, 100.0});
    REQUIRE(pf::shape_angle_deg(arc).has_value());
    CHECK(*pf::shape_angle_deg(arc) == doctest::Approx(angle_before + 30.0));
}

TEST_CASE("Formas: fechar curva gera laço de 360 graus") {
    const Shape arc = quarter_arc(80.0);
    const auto closed = pf::closed_shape(arc);
    REQUIRE(closed.has_value());
    const auto& loop = std::get<ArcSegment>(*closed);
    CHECK(loop.sweep_angle == doctest::Approx(2.0 * std::numbers::pi));
    CHECK(loop.radius == doctest::Approx(80.0));
}

TEST_CASE("Formas: inverter módulo curva.90 mantém as portas") {
    Shape shape = ModuleInstance{.template_id = "curve.90"};
    const auto before = pf::shape_endpoints(shape);
    REQUIRE(before.has_value());

    REQUIRE(pf::flip_shape(shape));
    const auto after = pf::shape_endpoints(shape);
    REQUIRE(after.has_value());
    CHECK(pf::distance(before->first, after->first) < 1e-3);
    CHECK(pf::distance(before->second, after->second) < 1e-3);

    CHECK(pf::endpoint_port_names(shape).has_value());
    CHECK(pf::endpoint_port_names(shape)->first == "in");
    CHECK(pf::endpoint_port_names(shape)->second == "out");
}
