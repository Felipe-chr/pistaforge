#include <cmath>
#include <cstdint>
#include <numbers>
#include <ostream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/scene/element.hpp"
#include "core/scene/module.hpp"
#include "core/scene/scene.hpp"

using pf::CubicBezier;
using pf::Element;
using pf::ElementId;
using pf::LayerId;
using pf::LineSegment;
using pf::ModuleInstance;
using pf::PortInfo;
using pf::Scene;
using pf::Vec2;

TEST_CASE("Cena: identificadores monotônicos e não reutilizados") {
    Scene scene;

    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {10.0, 0.0}}).id;
    const ElementId second = scene.add_element(LineSegment{{10.0, 0.0}, {20.0, 0.0}}).id;

    CHECK(second == first + 1);
    CHECK(scene.find_element(first) != nullptr);

    REQUIRE(scene.remove_element(second));
    CHECK(scene.find_element(second) == nullptr);

    const ElementId third = scene.add_element(LineSegment{{20.0, 0.0}, {30.0, 0.0}}).id;
    CHECK(third > second);
    CHECK(third != second);
}

TEST_CASE("Cena: remover elemento remove conexões associadas") {
    Scene scene;
    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {10.0, 0.0}}).id;
    const ElementId second = scene.add_element(LineSegment{{10.0, 0.0}, {20.0, 0.0}}).id;

    const pf::ConnectionId connection =
        scene.add_connection(pf::PortRef{first, "end"}, pf::PortRef{second, "start"}).id;
    REQUIRE(scene.find_connection(connection) != nullptr);

    REQUIRE(scene.remove_element(first));
    CHECK(scene.find_connection(connection) == nullptr);
    CHECK(scene.connections.empty());
}

TEST_CASE("Cena: conexões podem ser removidas") {
    Scene scene;
    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {10.0, 0.0}}).id;
    const ElementId second = scene.add_element(LineSegment{{10.0, 0.0}, {20.0, 0.0}}).id;
    const pf::ConnectionId connection =
        scene.add_connection(pf::PortRef{first, "end"}, pf::PortRef{second, "start"}).id;

    CHECK(scene.remove_connection(connection));
    CHECK_FALSE(scene.remove_connection(connection));
    CHECK(scene.connections.empty());
}

TEST_CASE("Cena: portas das primitivas") {
    Element line;
    line.id = 1;
    line.shape = LineSegment{{0.0, 0.0}, {100.0, 0.0}};
    const std::vector<PortInfo> line_ports = pf::ports_of(line);
    REQUIRE(line_ports.size() == 2);
    CHECK(line_ports[0].ref.port == "start");
    CHECK(line_ports[0].position == Vec2{0.0, 0.0});
    CHECK(line_ports[1].ref.port == "end");
    CHECK(line_ports[1].position == Vec2{100.0, 0.0});
    CHECK(line_ports[0].direction == Vec2{1.0, 0.0});

    Element arc;
    arc.id = 2;
    arc.shape = pf::ArcSegment{{0.0, 0.0}, 100.0, 0.0, std::numbers::pi / 2.0};
    const std::vector<PortInfo> arc_ports = pf::ports_of(arc);
    REQUIRE(arc_ports.size() == 2);
    CHECK(arc_ports[0].position == Vec2{100.0, 0.0});
    CHECK(arc_ports[1].position.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(arc_ports[1].position.y == doctest::Approx(100.0));
    CHECK(arc_ports[0].curvature == doctest::Approx(0.01));

    Element cubic;
    cubic.id = 3;
    cubic.shape = CubicBezier{{0.0, 0.0}, {30.0, 0.0}, {70.0, 0.0}, {100.0, 0.0}};
    const std::vector<PortInfo> cubic_ports = pf::ports_of(cubic);
    REQUIRE(cubic_ports.size() == 2);
    CHECK(cubic_ports[0].position == Vec2{0.0, 0.0});
    CHECK(cubic_ports[1].position == Vec2{100.0, 0.0});
    CHECK(cubic_ports[0].direction == Vec2{1.0, 0.0});
}

TEST_CASE("Cena: portas dos módulos e transformação") {
    Element cross;
    cross.id = 4;
    ModuleInstance module;
    module.template_id = "cross.90";
    module.params = {{"arm", 80.0}};
    cross.shape = module;

    const std::vector<PortInfo> ports = pf::ports_of(cross);
    REQUIRE(ports.size() == 4);
    CHECK(ports[0].ref.port == "w");
    CHECK(ports[0].position == Vec2{-80.0, 0.0});
    CHECK(ports[1].ref.port == "e");
    CHECK(ports[2].ref.port == "s");
    CHECK(ports[3].ref.port == "n");

    module.transform.translation = {10.0, 20.0};
    module.transform.rotation = std::numbers::pi / 2.0;
    cross.shape = module;
    const std::vector<PortInfo> transformed = pf::ports_of(cross);
    REQUIRE(transformed.size() == 4);
    CHECK(transformed[0].position.x == doctest::Approx(10.0));
    CHECK(transformed[0].position.y == doctest::Approx(-60.0));
    CHECK(transformed[0].direction.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(transformed[0].direction.y == doctest::Approx(1.0));
}

TEST_CASE("Cena: catálogo mínimo de módulos") {
    REQUIRE(pf::find_module_template("cross.90") != nullptr);
    REQUIRE(pf::find_module_template("curve.90") != nullptr);
    CHECK(pf::find_module_template("desconhecido") == nullptr);

    const pf::ModuleTemplate* cross = pf::find_module_template("cross.90");
    REQUIRE(cross != nullptr);
    CHECK(cross->pairs.size() == 2);
    CHECK(cross->pairs[0].first == "w");
    CHECK(cross->pairs[0].second == "e");
    CHECK(cross->pairs[1].first == "s");
    CHECK(cross->pairs[1].second == "n");
}

TEST_CASE("Cena: helpers de forma") {
    const LineSegment line{{0.0, 0.0}, {100.0, 0.0}};
    CHECK(pf::shape_length(line) == doctest::Approx(100.0));
    CHECK(pf::shape_bbox(line).width() == doctest::Approx(100.0));

    double t = -1.0;
    CHECK(pf::shape_closest_point(line, {50.0, 10.0}, &t) == Vec2{50.0, 0.0});
    CHECK(t == doctest::Approx(0.5));

    const std::vector<Vec2> flattened = pf::shape_flatten(line, 0.01);
    CHECK(flattened.size() == 2);

    ModuleInstance module;
    module.template_id = "curve.90";
    module.params = {{"radius", 100.0}};
    CHECK(pf::shape_length(module) > 150.0);
    CHECK(pf::shape_length(module) < 160.0);
}

TEST_CASE("Cena: limites incluem a arena e os elementos") {
    Scene scene;
    pf::BBox bounds = scene.bounds();
    CHECK(bounds.min == Vec2{0.0, 0.0});
    CHECK(bounds.max == Vec2{4000.0, 3000.0});

    scene.add_element(LineSegment{{5000.0, 100.0}, {5100.0, 100.0}}, 19.0);
    bounds = scene.bounds();
    CHECK(bounds.max.x == doctest::Approx(5109.5));
}

TEST_CASE("Cena: conversão de camadas e marcadores") {
    CHECK(pf::to_string(LayerId::Track) == "track");
    CHECK(pf::to_string(LayerId::Markers) == "markers");
    CHECK(pf::to_string(LayerId::Annotations) == "annotations");
    CHECK(pf::to_string(LayerId::Guides) == "guides");

    CHECK(pf::layer_from_string("track") == LayerId::Track);
    CHECK(pf::layer_from_string("markers") == LayerId::Markers);
    CHECK_FALSE(pf::layer_from_string("inexistente").has_value());

    CHECK(pf::to_string(pf::MarkerType::StartFinish) == "start_finish");
    CHECK(pf::to_string(pf::MarkerType::DirectionArrow) == "direction_arrow");
    CHECK(pf::to_string(pf::MarkerType::Text) == "text");
    CHECK(pf::marker_type_from_string("start_finish") == pf::MarkerType::StartFinish);
    CHECK_FALSE(pf::marker_type_from_string("inexistente").has_value());
}

TEST_CASE("Cena: revisão avança com touch") {
    Scene scene;
    const std::uint64_t before = scene.revision;
    scene.touch();
    CHECK(scene.revision == before + 1);
}

TEST_CASE("Cena: translação desloca todas as formas") {
    const Vec2 delta{10.0, -5.0};

    LineSegment line{{0.0, 0.0}, {100.0, 0.0}};
    pf::translate(line, delta);
    CHECK(line.a == Vec2{10.0, -5.0});
    CHECK(line.b == Vec2{110.0, -5.0});

    pf::ArcSegment arc{{0.0, 0.0}, 50.0, 0.0, std::numbers::pi / 2.0};
    pf::translate(arc, delta);
    CHECK(arc.center == Vec2{10.0, -5.0});
    CHECK(arc.radius == 50.0);
    CHECK(arc.start_angle == 0.0);
    CHECK(arc.sweep_angle == std::numbers::pi / 2.0);

    CubicBezier bezier{{0.0, 0.0}, {1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}};
    pf::translate(bezier, delta);
    CHECK(bezier.p0 == Vec2{10.0, -5.0});
    CHECK(bezier.p1 == Vec2{11.0, -3.0});
    CHECK(bezier.p2 == Vec2{13.0, -1.0});
    CHECK(bezier.p3 == Vec2{15.0, 1.0});

    ModuleInstance module;
    module.template_id = "cross.90";
    module.transform.translation = {100.0, 200.0};
    module.transform.rotation = std::numbers::pi / 2.0;
    pf::translate(module, delta);
    CHECK(module.transform.translation == Vec2{110.0, 195.0});
    CHECK(module.transform.rotation == std::numbers::pi / 2.0);

    Element element;
    element.shape = LineSegment{{0.0, 0.0}, {10.0, 0.0}};
    pf::translate(element, delta);
    const auto* moved = std::get_if<LineSegment>(&element.shape);
    REQUIRE(moved != nullptr);
    CHECK(moved->a == Vec2{10.0, -5.0});
    CHECK(moved->b == Vec2{20.0, -5.0});
}
