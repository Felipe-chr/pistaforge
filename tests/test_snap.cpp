#include <cmath>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "core/scene/element.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/snap.hpp"

using pf::ElementId;
using pf::LineSegment;
using pf::Scene;
using pf::SnapKind;
using pf::SnapOptions;
using pf::SnapResult;
using pf::Vec2;

namespace {

SnapOptions options_without_grid() {
    SnapOptions options;
    options.grid = false;
    return options;
}

} // namespace

TEST_CASE("Encaixe: converte tipos para texto") {
    CHECK(pf::to_string(SnapKind::None) == "none");
    CHECK(pf::to_string(SnapKind::Port) == "port");
    CHECK(pf::to_string(SnapKind::Element) == "element");
    CHECK(pf::to_string(SnapKind::Grid) == "grid");
}

TEST_CASE("Encaixe: prioriza portas sobre a geometria") {
    Scene scene;
    const ElementId line = scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}}).id;
    const ElementId vertical = scene.add_element(LineSegment{{100.0, 10.0}, {100.0, 100.0}}).id;

    // (100, 3) está a 3 mm da linha horizontal e a 7 mm da porta da vertical.
    const SnapResult result = pf::snap_point(scene, {100.0, 3.0}, options_without_grid());

    CHECK(result.kind == SnapKind::Port);
    CHECK(result.point == Vec2{100.0, 10.0});
    CHECK(result.element == vertical);
    CHECK(result.port == "start");
    CHECK(result.distance == doctest::Approx(7.0));
    CHECK(result.element != line);
}

TEST_CASE("Encaixe: cai na geometria quando não há porta perto") {
    Scene scene;
    const ElementId line = scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}}).id;

    const SnapResult result = pf::snap_point(scene, {50.0, 3.0}, options_without_grid());

    CHECK(result.kind == SnapKind::Element);
    CHECK(result.point == Vec2{50.0, 0.0});
    CHECK(result.element == line);
    CHECK(result.port.empty());
    CHECK(result.distance == doctest::Approx(3.0));
    CHECK(result.direction == Vec2{1.0, 0.0});
}

TEST_CASE("Encaixe: direção acompanha a tangente da geometria") {
    Scene scene;
    const ElementId vertical = scene.add_element(LineSegment{{100.0, 0.0}, {100.0, 200.0}}).id;

    const SnapResult result = pf::snap_point(scene, {103.0, 50.0}, options_without_grid());

    CHECK(result.kind == SnapKind::Element);
    CHECK(result.element == vertical);
    CHECK(result.point == Vec2{100.0, 50.0});
    CHECK(result.direction.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(result.direction.y == doctest::Approx(1.0));
}

TEST_CASE("Encaixe: usa a grade quando nada está perto") {
    Scene scene;

    SnapOptions options;
    options.grid_mm = 10.0;

    const SnapResult result = pf::snap_point(scene, {13.0, 27.0}, options);

    CHECK(result.kind == SnapKind::Grid);
    CHECK(result.point == Vec2{10.0, 30.0});
    CHECK(result.distance == doctest::Approx(std::hypot(3.0, 3.0)));
    CHECK(result.element == pf::kInvalidId);
}

TEST_CASE("Encaixe: respeita a tolerância") {
    Scene scene;
    scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}});

    const SnapResult result = pf::snap_point(scene, {50.0, 40.0}, options_without_grid());
    CHECK(result.kind == SnapKind::None);
    CHECK(result.element == pf::kInvalidId);
}

TEST_CASE("Encaixe: ignora o elemento informado") {
    Scene scene;
    const ElementId line = scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}}).id;

    SnapOptions options = options_without_grid();
    options.ignore = line;

    const SnapResult result = pf::snap_point(scene, {50.0, 3.0}, options);
    CHECK(result.kind == SnapKind::None);
}

TEST_CASE("Encaixe: ignora elementos ocultos e camadas invisíveis") {
    Scene scene;
    const ElementId hidden = scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}}).id;
    scene.find_element(hidden)->hidden = true;

    SnapOptions options = options_without_grid();
    CHECK(pf::snap_point(scene, {50.0, 3.0}, options).kind == SnapKind::None);

    scene.find_element(hidden)->hidden = false;
    scene.layers.track = false;
    CHECK(pf::snap_point(scene, {50.0, 3.0}, options).kind == SnapKind::None);

    scene.layers.track = true;
    CHECK(pf::snap_point(scene, {50.0, 3.0}, options).kind == SnapKind::Element);
}

TEST_CASE("Encaixe: opções desligadas são respeitadas") {
    Scene scene;
    scene.add_element(LineSegment{{0.0, 0.0}, {200.0, 0.0}});

    SnapOptions options;
    options.ports = false;
    options.elements = false;
    options.grid = false;

    const SnapResult result = pf::snap_point(scene, {50.0, 0.0}, options);
    CHECK(result.kind == SnapKind::None);
}
