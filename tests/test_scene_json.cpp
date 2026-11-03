#include <chrono>
#include <cmath>
#include <numbers>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/io/scene_json.hpp"
#include "core/scene/scene.hpp"

using nlohmann::json;
using pf::ArcSegment;
using pf::CubicBezier;
using pf::LineSegment;
using pf::ModuleInstance;
using pf::Scene;
using pf::Vec2;

namespace {

Scene make_sample_scene() {
    Scene scene;
    scene.arena.name = "Regional 2026";
    scene.arena.width_mm = 4000.0;
    scene.arena.height_mm = 3000.0;
    scene.arena.margin_mm = 100.0;
    scene.layers.guides = true;

    scene.add_element(LineSegment{{250.0, 1500.0}, {900.0, 1500.0}}, 19.0);
    scene.add_element(ArcSegment{{1200.0, 1800.0}, 300.0, -std::numbers::pi / 2.0,
                                 std::numbers::pi / 2.0},
                      19.0);
    scene.add_element(CubicBezier{{1200.0, 1800.0}, {1400.0, 1800.0}, {1500.0, 1900.0},
                                  {1500.0, 2100.0}},
                      19.0);

    ModuleInstance cross;
    cross.template_id = "cross.90";
    cross.template_version = 1;
    cross.params = {{"arm", 80.0}, {"width", 19.0}};
    cross.transform.translation = {2200.0, 1800.0};
    cross.transform.rotation = std::numbers::pi / 4.0;
    cross.transform.mirrored = true;
    scene.add_element(std::move(cross), 19.0);

    scene.add_connection({scene.elements[0].id, "end"}, {scene.elements[1].id, "start"});
    scene.add_connection({scene.elements[1].id, "end"}, {scene.elements[2].id, "start"});
    scene.add_connection({scene.elements[2].id, "end"}, {scene.elements[3].id, "w"});

    scene.markers.push_back(pf::Marker{
        .id = scene.allocate_id(),
        .type = pf::MarkerType::StartFinish,
        .position = {250.0, 1500.0},
        .angle = std::numbers::pi / 4.0,
        .text = "largada",
    });

    scene.guides.push_back(pf::Guide{
        .id = scene.allocate_id(),
        .a = {0.0, 500.0},
        .b = {4000.0, 500.0},
    });

    return scene;
}

} // namespace

TEST_CASE("JSON: round-trip completo") {
    const Scene original = make_sample_scene();
    const json document = pf::io::to_json(original);
    const Scene restored = pf::io::from_json(document);

    CHECK(restored.schema_version == original.schema_version);
    CHECK(restored.arena.name == original.arena.name);
    CHECK(restored.arena.width_mm == doctest::Approx(original.arena.width_mm));
    CHECK(restored.arena.height_mm == doctest::Approx(original.arena.height_mm));
    CHECK(restored.arena.margin_mm == doctest::Approx(original.arena.margin_mm));
    CHECK(restored.layers.guides == original.layers.guides);

    REQUIRE(restored.elements.size() == original.elements.size());
    REQUIRE(restored.connections.size() == original.connections.size());
    REQUIRE(restored.markers.size() == 1);
    REQUIRE(restored.guides.size() == 1);
    CHECK(restored.next_id == original.next_id);

    const auto& line = std::get<LineSegment>(restored.elements[0].shape);
    CHECK(line.a == Vec2{250.0, 1500.0});
    CHECK(line.b == Vec2{900.0, 1500.0});

    const auto& arc = std::get<ArcSegment>(restored.elements[1].shape);
    CHECK(arc.center == Vec2{1200.0, 1800.0});
    CHECK(arc.radius == doctest::Approx(300.0));
    CHECK(arc.start_angle == doctest::Approx(-std::numbers::pi / 2.0));
    CHECK(arc.sweep_angle == doctest::Approx(std::numbers::pi / 2.0));

    const auto& cubic = std::get<CubicBezier>(restored.elements[2].shape);
    CHECK(cubic.p0 == Vec2{1200.0, 1800.0});
    CHECK(cubic.p3 == Vec2{1500.0, 2100.0});

    const auto& module = std::get<ModuleInstance>(restored.elements[3].shape);
    CHECK(module.template_id == "cross.90");
    CHECK(module.template_version == 1);
    CHECK(module.params.at("arm") == doctest::Approx(80.0));
    CHECK(module.transform.translation == Vec2{2200.0, 1800.0});
    CHECK(module.transform.rotation == doctest::Approx(std::numbers::pi / 4.0));
    CHECK(module.transform.mirrored);

    CHECK(restored.connections[0].a == pf::PortRef{original.elements[0].id, "end"});
    CHECK(restored.connections[0].b == pf::PortRef{original.elements[1].id, "start"});

    CHECK(restored.markers[0].type == pf::MarkerType::StartFinish);
    CHECK(restored.markers[0].position == Vec2{250.0, 1500.0});
    CHECK(restored.markers[0].angle == doctest::Approx(std::numbers::pi / 4.0));
    CHECK(restored.markers[0].text == "largada");

    CHECK(restored.guides[0].a == Vec2{0.0, 500.0});
    CHECK(restored.guides[0].b == Vec2{4000.0, 500.0});
}

TEST_CASE("JSON: ângulos são gravados em graus") {
    const Scene scene = make_sample_scene();
    const json document = pf::io::to_json(scene);

    CHECK(document["elements"][1]["start"].get<double>() == doctest::Approx(-90.0));
    CHECK(document["elements"][1]["sweep"].get<double>() == doctest::Approx(90.0));
    CHECK(document["elements"][3]["transform"]["rot"].get<double>() ==
          doctest::Approx(45.0));
    CHECK(document["markers"][0]["angle"].get<double>() == doctest::Approx(45.0));
}

TEST_CASE("JSON: next_id é elevado acima do maior id") {
    json document{
        {"schema", 1},
        {"arena", {{"width", 1000.0}, {"height", 1000.0}, {"margin", 50.0}}},
        {"next_id", 1},
        {"elements",
         json::array({
             {{"id", 10},
              {"type", "line"},
              {"a", json::array({0.0, 0.0})},
              {"b", json::array({10.0, 0.0})}},
         })},
    };

    const Scene scene = pf::io::from_json(document);
    CHECK(scene.next_id == 11);
}

TEST_CASE("JSON: erros de documento") {
    CHECK_THROWS_AS(pf::io::from_json(json::array()), pf::io::ParseError);

    json unknown_type{
        {"arena", {{"width", 1000.0}, {"height", 1000.0}, {"margin", 50.0}}},
        {"elements", json::array({{{"id", 1}, {"type", "inexistente"}}})},
    };
    CHECK_THROWS_AS(pf::io::from_json(unknown_type), pf::io::ParseError);

    json bad_point{
        {"arena", {{"width", 1000.0}, {"height", 1000.0}, {"margin", 50.0}}},
        {"elements",
         json::array({{{"id", 1}, {"type", "line"}, {"a", json::array({0.0})}, {"b", json::array({1.0, 2.0})}}})},
    };
    CHECK_THROWS_AS(pf::io::from_json(bad_point), pf::io::ParseError);

    CHECK_THROWS_AS(pf::io::from_string("{nao e json"), pf::io::ParseError);
    CHECK_THROWS_AS(pf::io::from_string(R"({"arena":5})"), pf::io::ParseError);
}

TEST_CASE("JSON: compatibilidade com o Apêndice B") {
    const std::string text = R"json({
  "schema": 1,
  "arena": { "name": "Regional 2026", "width": 4000, "height": 3000, "margin": 100 },
  "next_id": 8,
  "layers": { "track": true, "markers": true, "annotations": true, "guides": false },
  "elements": [
    { "id": 1, "type": "line", "layer": "track", "width": 19,
      "a": [250, 1500], "b": [900, 1500] },
    { "id": 2, "type": "cubic", "layer": "track", "width": 19,
      "p0": [900, 1500], "p1": [1100, 1500], "p2": [1200, 1600], "p3": [1200, 1800] },
    { "id": 3, "type": "module", "layer": "track", "width": 19,
      "template": "cross.90", "version": 1,
      "params": { "arm": 80, "width": 19 },
      "transform": { "tx": 2200, "ty": 1800, "rot": 0, "mirrored": false } }
  ],
  "connections": [
    { "id": 1, "a": [1, "end"], "b": [2, "start"] },
    { "id": 2, "a": [2, "end"], "b": [3, "w"] }
  ],
  "markers": [
    { "id": 4, "type": "start_finish", "x": 300, "y": 1500, "angle": 0 }
  ],
  "guides": [],
  "robot": { "wheelbase": 120, "sensor_span": 80, "max_speed": 1.2 },
  "print": { "paper": "A3", "orientation": "landscape", "margin": 10 }
})json";

    const Scene scene = pf::io::from_string(text);
    CHECK(scene.arena.name == "Regional 2026");
    CHECK(scene.elements.size() == 3);
    CHECK(scene.connections.size() == 2);
    CHECK(scene.markers.size() == 1);
    CHECK(scene.next_id == 8);

    const auto& module = std::get<ModuleInstance>(scene.elements[2].shape);
    CHECK(module.template_id == "cross.90");
    CHECK(module.params.at("arm") == doctest::Approx(80.0));
    CHECK(module.transform.translation == Vec2{2200.0, 1800.0});
    CHECK(module.transform.rotation == doctest::Approx(0.0));
    CHECK(scene.markers[0].angle == doctest::Approx(0.0));
}

TEST_CASE("JSON: 5k elementos em menos de um segundo") {
    Scene scene;
    scene.elements.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        const double x = static_cast<double>(i % 100) * 40.0;
        const double y = static_cast<double>(i / 100) * 60.0;
        scene.add_element(LineSegment{{x, y}, {x + 35.0, y}}, 19.0);
    }

    const auto start = std::chrono::steady_clock::now();
    const std::string text = pf::io::to_string(scene);
    const Scene restored = pf::io::from_string(text);
    const auto end = std::chrono::steady_clock::now();

    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    CHECK(restored.elements.size() == 5000);
    CHECK(text.size() > 100000);
    CHECK(elapsed_ms < 1000.0);
}
