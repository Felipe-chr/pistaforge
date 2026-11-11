#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/geometry/line.hpp"
#include "core/scene/scene.hpp"
#include "core/sim/sim_doc.hpp"

using nlohmann::json;
using pf::ElementId;
using pf::LineSegment;
using pf::PortRef;
using pf::Scene;
using pf::sim::build_sim_document;
using pf::sim::kSimFormat;
using pf::sim::kSimSchema;
using pf::sim::SimDocOptions;

namespace {

SimDocOptions options_with(double step_mm) {
    SimDocOptions options;
    options.step_mm = step_mm;
    options.tolerance_mm = 0.25;
    return options;
}

json parse(const Scene& scene, const SimDocOptions& options) {
    return json::parse(build_sim_document(scene, options).json);
}

} // namespace

TEST_CASE("Simulador: documento vazio tem cabeçalho, arena e métricas zeradas") {
    Scene scene;
    scene.arena.name = "Arena de teste";
    scene.arena.width_mm = 3000.0;
    scene.arena.height_mm = 2000.0;
    scene.arena.margin_mm = 50.0;

    const json doc = parse(scene, options_with(50.0));

    CHECK(doc.at("format").get<std::string>() == kSimFormat);
    CHECK(doc.at("schema").get<int>() == kSimSchema);
    CHECK(doc.at("generator").at("name").get<std::string>() == "PistaForge");
    CHECK_FALSE(doc.at("generator").contains("version"));
    CHECK(doc.at("units").at("length").get<std::string>() == "mm");
    CHECK(doc.at("coordinate_system").at("y").get<std::string>() == "down");
    CHECK(doc.at("arena").at("name").get<std::string>() == "Arena de teste");
    CHECK(doc.at("arena").at("width_mm").get<double>() == doctest::Approx(3000.0));
    CHECK(doc.at("arena").at("margin_mm").get<double>() == doctest::Approx(50.0));
    CHECK(doc.at("path").at("point_count").get<int>() == 0);
    CHECK(doc.at("path").at("points").empty());
    CHECK(doc.at("path").at("segments").empty());
    CHECK_FALSE(doc.at("path").at("closed").get<bool>());
    CHECK(doc.at("metrics").at("element_count").get<int>() == 0);
    CHECK(doc.at("metrics").at("total_length_mm").get<double>() == doctest::Approx(0.0));
    CHECK(doc.at("critical_points").empty());
}

TEST_CASE("Simulador: reta gera pontos com passo, rumo e elemento") {
    Scene scene;
    const ElementId line =
        scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 19.0).id;

    const json doc = parse(scene, options_with(100.0));

    const json& path = doc.at("path");
    REQUIRE(path.at("point_count").get<int>() == 11);
    REQUIRE(path.at("points").size() == 11);

    const json& first = path.at("points").at(0);
    CHECK(first.at("i").get<int>() == 0);
    CHECK(first.at("s_mm").get<double>() == doctest::Approx(0.0));
    CHECK(first.at("x_mm").get<double>() == doctest::Approx(0.0));
    CHECK(first.at("y_mm").get<double>() == doctest::Approx(0.0));
    CHECK(first.at("heading_deg").get<double>() == doctest::Approx(0.0));
    CHECK(first.at("curvature_1_mm").get<double>() == doctest::Approx(0.0));
    CHECK(first.at("width_mm").get<double>() == doctest::Approx(19.0));
    CHECK(first.at("element").get<ElementId>() == line);

    const json& last = path.at("points").at(10);
    CHECK(last.at("i").get<int>() == 10);
    CHECK(last.at("s_mm").get<double>() == doctest::Approx(1000.0));
    CHECK(last.at("x_mm").get<double>() == doctest::Approx(1000.0));

    REQUIRE(path.at("segments").size() == 1);
    CHECK(path.at("segments").at(0).at("element").get<ElementId>() == line);
    CHECK(path.at("segments").at(0).at("length_mm").get<double>() == doctest::Approx(1000.0));

    CHECK(doc.at("metrics").at("total_length_mm").get<double>() == doctest::Approx(1000.0));
    CHECK(doc.at("metrics").at("element_count").get<int>() == 1);
    CHECK(doc.at("metrics").at("straight_count").get<int>() == 1);
}

TEST_CASE("Simulador: percurso ordenado pelas conexões") {
    Scene scene;
    const ElementId first =
        scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 19.0).id;
    const ElementId second =
        scene.add_element(LineSegment{{1000.0, 0.0}, {1000.0, 1000.0}}, 19.0).id;
    scene.add_connection(PortRef{first, "end"}, PortRef{second, "start"});

    const json doc = parse(scene, options_with(250.0));

    const json& path = doc.at("path");
    CHECK(path.at("ordered").get<bool>());
    CHECK_FALSE(path.at("closed").get<bool>());
    CHECK(path.at("total_length_mm").get<double>() == doctest::Approx(2000.0));
    REQUIRE(path.at("points").size() == 9);
    CHECK(path.at("points").at(5).at("heading_deg").get<double>() == doctest::Approx(90.0));
    CHECK(path.at("points").at(5).at("element").get<ElementId>() == second);
}

TEST_CASE("Simulador: versão do gerador é gravada quando informada") {
    Scene scene;
    SimDocOptions options = options_with(50.0);
    options.generator_version = "9.9.9";

    const json doc = parse(scene, options);
    CHECK(doc.at("generator").at("version").get<std::string>() == "9.9.9");
}

TEST_CASE("Simulador: tabela retornada corresponde ao JSON") {
    Scene scene;
    scene.add_element(LineSegment{{0.0, 0.0}, {500.0, 0.0}}, 19.0);

    const auto document = build_sim_document(scene, options_with(50.0));
    const json doc = json::parse(document.json);

    CHECK(document.table.samples.size() == doc.at("path").at("point_count").get<std::size_t>());
    CHECK(document.table.total_length_mm ==
          doctest::Approx(doc.at("path").at("total_length_mm").get<double>()));
    CHECK(document.table.step_mm == doctest::Approx(doc.at("path").at("step_mm").get<double>()));
}
