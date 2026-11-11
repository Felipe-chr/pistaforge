#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include <doctest/doctest.h>

#include "core/geometry/arc.hpp"
#include "core/geometry/line.hpp"
#include "core/scene/module.hpp"
#include "core/scene/scene.hpp"
#include "core/sim/path_table.hpp"

using pf::ElementId;
using pf::LineSegment;
using pf::Marker;
using pf::MarkerType;
using pf::ModuleInstance;
using pf::PathTable;
using pf::PathTableOptions;
using pf::PortRef;
using pf::Scene;
using pf::Vec2;

namespace {

PathTableOptions options_with(double step_mm) {
    PathTableOptions options;
    options.step_mm = step_mm;
    options.tolerance_mm = 0.25;
    return options;
}

} // namespace

TEST_CASE("Tabela: reta gera matriz regular com passo definido") {
    Scene scene;
    const ElementId line = scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 19.0).id;

    const PathTable table = pf::build_path_table(scene, options_with(100.0));

    REQUIRE(table.samples.size() == 11);
    CHECK(table.total_length_mm == doctest::Approx(1000.0));
    CHECK(table.step_mm == doctest::Approx(100.0));
    CHECK_FALSE(table.ordered);
    CHECK_FALSE(table.closed);
    CHECK(table.start_element == line);

    CHECK(table.samples.front().position.x == doctest::Approx(0.0));
    CHECK(table.samples.front().position.y == doctest::Approx(0.0));
    CHECK(table.samples.back().position.x == doctest::Approx(1000.0));
    CHECK(table.samples[5].position.x == doctest::Approx(500.0));
    CHECK(table.samples[5].heading_deg == doctest::Approx(0.0));
    CHECK(table.samples[5].curvature == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(table.samples[5].width_mm == doctest::Approx(19.0));
    CHECK(table.samples[5].element == line);
}

TEST_CASE("Tabela: percurso segue conexões e calcula o rumo na curva") {
    Scene scene;
    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 19.0).id;
    const ElementId second =
        scene.add_element(LineSegment{{1000.0, 0.0}, {1000.0, 1000.0}}, 25.0).id;
    scene.add_connection(PortRef{first, "end"}, PortRef{second, "start"});

    const PathTable table = pf::build_path_table(scene, options_with(250.0));

    CHECK(table.ordered);
    CHECK_FALSE(table.closed);
    CHECK(table.total_length_mm == doctest::Approx(2000.0));
    REQUIRE(table.samples.size() == 9);

    CHECK(table.samples[4].position.x == doctest::Approx(1000.0));
    CHECK(table.samples[4].position.y == doctest::Approx(0.0));
    CHECK(table.samples[5].position.x == doctest::Approx(1000.0));
    CHECK(table.samples[5].position.y == doctest::Approx(250.0));
    CHECK(table.samples[5].heading_deg == doctest::Approx(90.0));
    CHECK(table.samples[5].width_mm == doctest::Approx(25.0));
    CHECK(table.samples[5].element == second);
}

TEST_CASE("Tabela: marcador de largada escolhe o sentido do percurso") {
    Scene scene;
    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 19.0).id;
    const ElementId second =
        scene.add_element(LineSegment{{1000.0, 0.0}, {1000.0, 1000.0}}, 19.0).id;
    scene.add_connection(PortRef{first, "end"}, PortRef{second, "start"});
    scene.markers.push_back(Marker{
        .id = scene.allocate_id(),
        .type = MarkerType::Start,
        .position = {1000.0, 1000.0},
        .angle = 0.0,
        .text = {},
    });

    const PathTable table = pf::build_path_table(scene, options_with(250.0));

    REQUIRE(!table.samples.empty());
    CHECK(table.samples.front().position.x == doctest::Approx(1000.0));
    CHECK(table.samples.front().position.y == doctest::Approx(1000.0));
    CHECK(table.samples.back().position.x == doctest::Approx(0.0));
    CHECK(table.samples.back().position.y == doctest::Approx(0.0));
    CHECK(table.ordered);
}

TEST_CASE("Tabela: cruzamento segue reto pela porta oposta") {
    Scene scene;
    ModuleInstance cross;
    cross.template_id = "cross.90";
    cross.template_version = 1;
    cross.params = {{"arm", 80.0}, {"width", 19.0}};
    scene.add_element(std::move(cross), 19.0);

    const PathTable table = pf::build_path_table(scene, options_with(100.0));

    CHECK(table.total_length_mm == doctest::Approx(160.0));
    REQUIRE(table.samples.size() == 3);
    CHECK(table.samples.front().position.x == doctest::Approx(-80.0));
    CHECK(table.samples.front().position.y == doctest::Approx(0.0));
    CHECK(table.samples[1].position.x == doctest::Approx(20.0));
    CHECK(table.samples.back().position.x == doctest::Approx(80.0));
    CHECK(table.samples.back().heading_deg == doctest::Approx(0.0));
}

TEST_CASE("Tabela: curva 90 de módulo gera arco com comprimento correto") {
    Scene scene;
    ModuleInstance curve;
    curve.template_id = "curve.90";
    curve.template_version = 1;
    curve.params = {{"radius", 100.0}, {"width", 19.0}};
    scene.add_element(std::move(curve), 19.0);

    const PathTable table = pf::build_path_table(scene, options_with(50.0));

    CHECK(table.total_length_mm == doctest::Approx(100.0 * std::numbers::pi / 2.0).epsilon(1e-3));
    REQUIRE(!table.samples.empty());
    CHECK(table.samples.front().position.x == doctest::Approx(100.0));
    CHECK(table.samples.front().position.y == doctest::Approx(0.0));
    CHECK(table.samples.back().position.x == doctest::Approx(0.0));
    CHECK(table.samples.back().position.y == doctest::Approx(100.0));
}

TEST_CASE("Tabela: arco completo é detectado como pista fechada") {
    Scene scene;
    scene.add_element(pf::ArcSegment{{500.0, 500.0}, 200.0, 0.0, 2.0 * std::numbers::pi}, 19.0);

    const PathTable table = pf::build_path_table(scene, options_with(100.0));

    CHECK(table.closed);
    CHECK(table.total_length_mm ==
          doctest::Approx(2.0 * std::numbers::pi * 200.0).epsilon(1e-3));
    CHECK(table.samples.front().position.x == doctest::Approx(700.0));
    CHECK(table.samples.back().position.x == doctest::Approx(700.0));
}

TEST_CASE("Tabela: CSV tem cabeçalho e uma linha por ponto") {
    Scene scene;
    scene.add_element(LineSegment{{0.0, 0.0}, {500.0, 0.0}}, 19.0);

    const PathTable table = pf::build_path_table(scene, options_with(100.0));
    const std::string csv = pf::path_table_csv(table);

    CHECK(csv.starts_with("indice;s_mm;x_mm;y_mm;rumo_deg;curvatura_1_mm;largura_mm;elemento\n"));
    CHECK(std::count(csv.begin(), csv.end(), '\n') == 7);
}
