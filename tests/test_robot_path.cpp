#include <string>

#include <doctest/doctest.h>

#include "core/geometry/line.hpp"
#include "core/io/robot_path.hpp"
#include "core/scene/scene.hpp"

using pf::ElementId;
using pf::LineSegment;
using pf::PortRef;
using pf::Scene;

namespace {

Scene make_robot_scene() {
    Scene scene;
    scene.arena.name = "Arena Robo";
    scene.arena.width_mm = 1200.0;
    scene.arena.height_mm = 1200.0;
    scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 20.0);
    return scene;
}

} // namespace

TEST_CASE("Robô: lista de pontos com início, avanço e fim") {
    const Scene scene = make_robot_scene();
    const pf::io::RobotPath robot = pf::io::build_robot_path(scene);

    CHECK(robot.point_count == 11);
    CHECK(robot.total_length_mm == doctest::Approx(1000.0));
    CHECK(robot.clockwise);

    CHECK(robot.points_csv.starts_with(
        "ordem;comando;x_mm;y_mm;rumo_deg;delta_deg;distancia_mm;largura_mm;"
        "velocidade_mm_s;curvatura_1_mm;elemento\n"));
    CHECK(robot.points_csv.find("\n1;INICIO;0;0;0;0;0;20;250;0;") != std::string::npos);
    CHECK(robot.points_csv.find("\n11;FIM;1000;0;0;0;100;20;250;0;") != std::string::npos);
}

TEST_CASE("Robô: curva à direita vira comando VIRAR_DIR") {
    Scene scene;
    const ElementId first = scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 20.0).id;
    const ElementId second =
        scene.add_element(LineSegment{{1000.0, 0.0}, {1000.0, 1000.0}}, 20.0).id;
    scene.add_connection(PortRef{first, "end"}, PortRef{second, "start"});

    const pf::io::RobotPath robot = pf::io::build_robot_path(scene);

    CHECK(robot.points_csv.find(";VIRAR_DIR;") != std::string::npos);
    CHECK(robot.clockwise);
}

TEST_CASE("Robô: instruções trazem resumo, tabela e passo a passo") {
    const Scene scene = make_robot_scene();
    const pf::io::RobotPath robot = pf::io::build_robot_path(scene);

    CHECK(robot.instructions.find("PISTAFORGE - INSTRUCOES DE PINTURA") != std::string::npos);
    CHECK(robot.instructions.find("TABELA DE COMANDOS") != std::string::npos);
    CHECK(robot.instructions.find("SEQUENCIA PASSO A PASSO") != std::string::npos);
    CHECK(robot.instructions.find("Sentido: horario") != std::string::npos);
    CHECK(robot.instructions.find("Avancar 100 mm") != std::string::npos);
    CHECK(robot.instructions.find("Baixar o pincel") != std::string::npos);
    CHECK(robot.instructions.find("levantar o pincel") != std::string::npos);
}

TEST_CASE("Robô: largura do pincel pode substituir a largura do elemento") {
    const Scene scene = make_robot_scene();

    pf::io::RobotPathOptions options;
    options.brush_width_mm = 35.0;

    const pf::io::RobotPath robot = pf::io::build_robot_path(scene, options);
    CHECK(robot.points_csv.find(";35;") != std::string::npos);
}
