#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/bridge/state.hpp"
#include "core/io/scene_svg.hpp"
#include "core/scene/scene.hpp"

using nlohmann::json;
using pf::ArcSegment;
using pf::CubicBezier;
using pf::LineSegment;
using pf::Marker;
using pf::MarkerType;
using pf::ModuleInstance;
using pf::Scene;
using pf::Vec2;

namespace {

Scene make_svg_scene() {
    Scene scene;
    scene.arena.name = "Arena & Teste <1>";
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

    scene.markers.push_back(Marker{
        .id = scene.allocate_id(),
        .type = MarkerType::Start,
        .position = {250.0, 1500.0},
        .angle = std::numbers::pi / 4.0,
        .text = "largada",
    });
    scene.markers.push_back(Marker{
        .id = scene.allocate_id(),
        .type = MarkerType::Finish,
        .position = {900.0, 1500.0},
        .angle = 0.0,
        .text = "chegada",
    });

    scene.guides.push_back(pf::Guide{
        .id = scene.allocate_id(),
        .a = {100.0, 100.0},
        .b = {900.0, 900.0},
    });

    return scene;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

struct Api {
    pf::AppState state;
    pf::Dispatcher dispatcher;

    Api() { pf::register_api(dispatcher, state); }

    json call(const json& request) const { return json::parse(dispatcher.handle(request.dump())); }
};

} // namespace

TEST_CASE("SVG: cabeçalho 1:1 em mm e viewBox da arena") {
    const Scene scene = make_svg_scene();
    const std::string svg = pf::io::to_svg(scene);

    CHECK(svg.starts_with("<svg xmlns=\"http://www.w3.org/2000/svg\""));
    CHECK(svg.find("width=\"4000mm\"") != std::string::npos);
    CHECK(svg.find("height=\"3000mm\"") != std::string::npos);
    CHECK(svg.find("viewBox=\"0 0 4000 3000\"") != std::string::npos);
    CHECK(svg.ends_with("</svg>\n"));
}

TEST_CASE("SVG: linhas, arcos, cúbicas e módulos viram caminhos") {
    const Scene scene = make_svg_scene();
    const std::string svg = pf::io::to_svg(scene);

    CHECK(svg.find("M 250 1500 L 900 1500") != std::string::npos);
    CHECK(svg.find("M 1200 1500 A 300 300 0 0 1 1500 1800") != std::string::npos);
    CHECK(svg.find("C 1400 1800 1500 1900 1500 2100") != std::string::npos);
    // Cruzamento: dois segmentos com `M` próprio (sem ligação espúria).
    CHECK(svg.find("M 2256.569 1856.569 L 2143.431 1743.431 M 2256.569 1743.431 L "
                   "2143.431 1856.569") != std::string::npos);
}

TEST_CASE("SVG: círculo completo é dividido em dois arcos") {
    Scene scene;
    scene.add_element(ArcSegment{{500.0, 500.0}, 100.0, 0.0, 2.0 * std::numbers::pi}, 19.0);

    const std::string svg = pf::io::to_svg(scene);
    const std::size_t first = svg.find("A 100 100");
    REQUIRE(first != std::string::npos);
    CHECK(svg.find("A 100 100", first + 1) != std::string::npos);
}

TEST_CASE("SVG: marcadores de largada e chegada") {
    const Scene scene = make_svg_scene();
    const std::string svg = pf::io::to_svg(scene);

    CHECK(svg.find("LARGADA") != std::string::npos);
    CHECK(svg.find("CHEGADA") != std::string::npos);
    CHECK(svg.find("rotate(45)") != std::string::npos);
    CHECK(svg.find("#15803d") != std::string::npos);
    CHECK(svg.find("#b91c1c") != std::string::npos);
}

TEST_CASE("SVG: texto é escapado e a calibração aparece") {
    const Scene scene = make_svg_scene();
    const std::string svg = pf::io::to_svg(scene);

    CHECK(svg.find("<title>Arena &amp; Teste &lt;1&gt;</title>") != std::string::npos);
    CHECK(svg.find("100 mm - escala 1:1 (mm)") != std::string::npos);
    CHECK(svg.find("PistaForge - Arena &amp; Teste &lt;1&gt;") != std::string::npos);
    CHECK(svg.find("4000 x 3000 mm") != std::string::npos);
}

TEST_CASE("SVG: opções desligam grade, calibração e guias") {
    Scene scene = make_svg_scene();
    scene.layers.guides = false;

    pf::io::SvgOptions options;
    options.grid = false;
    options.calibration = false;
    options.guides = false;

    const std::string svg = pf::io::to_svg(scene, options);
    CHECK(svg.find("100 mm - escala 1:1 (mm)") == std::string::npos);
    CHECK(svg.find("stroke-dasharray=\"6 4\"") == std::string::npos);
    CHECK(svg.find("x1=\"100\" y1=\"0\"") == std::string::npos);
}

TEST_CASE("SVG: opção de guias força a exportação da camada oculta") {
    Scene scene = make_svg_scene();
    scene.layers.guides = false;

    pf::io::SvgOptions options;
    options.guides = true;

    const std::string svg = pf::io::to_svg(scene, options);
    CHECK(svg.find("stroke-dasharray=\"6 4\"") != std::string::npos);
}

TEST_CASE("SVG: mosaico calcula páginas e origens") {
    Scene scene;
    scene.arena.width_mm = 350.0;
    scene.arena.height_mm = 250.0;

    pf::io::SvgOptions options;
    options.page_width_mm = 190.0;
    options.page_height_mm = 277.0;
    options.overlap_mm = 10.0;

    const std::vector<pf::io::SvgPage> pages = pf::io::to_svg_pages(scene, options);
    REQUIRE(pages.size() == 2);

    CHECK(pages[0].column == 0);
    CHECK(pages[0].row == 0);
    CHECK(pages[0].columns == 2);
    CHECK(pages[0].rows == 1);
    CHECK(pages[0].origin_x_mm == doctest::Approx(0.0));
    CHECK(pages[1].origin_x_mm == doctest::Approx(160.0));
    CHECK(pages[1].width_mm == doctest::Approx(190.0));

    CHECK(pages[1].content.find("viewBox=\"160 -13.5 190 277\"") != std::string::npos);
    CHECK(pages[1].content.find("pagina 2/2 (coluna), 1/1 (linha)") != std::string::npos);
    CHECK(pages[0].content.find("<rect") != std::string::npos);
}

TEST_CASE("SVG: arena menor que a página fica centralizada") {
    Scene scene;
    scene.arena.width_mm = 100.0;
    scene.arena.height_mm = 80.0;

    pf::io::SvgOptions options;
    options.page_width_mm = 190.0;
    options.page_height_mm = 277.0;

    const std::vector<pf::io::SvgPage> pages = pf::io::to_svg_pages(scene, options);
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].origin_x_mm == doctest::Approx(-45.0));
    CHECK(pages[0].origin_y_mm == doctest::Approx(-98.5));
}

TEST_CASE("API: doc.exportSvg grava arquivo único") {
    Api api;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "pistaforge-svg-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);

    api.call(json{{"id", 1}, {"method", "doc.new"}});
    api.call(json{{"id", 2},
                  {"method", "doc.addLine"},
                  {"params", {{"a", {0, 0}}, {"b", {100, 0}}}}});
    api.call(json{{"id", 3},
                  {"method", "doc.addMarker"},
                  {"params", {{"type", "start"}, {"x", 0}, {"y", 0}, {"angle", 90}}}});

    const std::filesystem::path file = directory / "pista.svg";
    const json response = api.call(json{{"id", 4},
                                        {"method", "doc.exportSvg"},
                                        {"params", {{"path", file.string()}}}});

    REQUIRE(response["ok"].get<bool>());
    CHECK(response["result"]["pages"] == 1);
    CHECK(response["result"]["tiled"] == false);
    CHECK(response["result"]["width"] == 4000.0);
    CHECK(response["result"]["files"].size() == 1);
    REQUIRE(std::filesystem::exists(file));

    const std::string svg = read_file(file);
    CHECK(svg.find("viewBox=\"0 0 4000 3000\"") != std::string::npos);
    CHECK(svg.find("LARGADA") != std::string::npos);

    std::filesystem::remove_all(directory, error);
}

TEST_CASE("API: doc.exportSvg em mosaico cria um arquivo por página") {
    Api api;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "pistaforge-svg-tiled-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);

    api.call(json{{"id", 1},
                  {"method", "doc.new"},
                  {"params", {{"arena", {{"width", 350}, {"height", 250}}}}}});

    const std::filesystem::path file = directory / "mosaico.svg";
    const json response = api.call(json{{"id", 2},
                                        {"method", "doc.exportSvg"},
                                        {"params",
                                         {{"path", file.string()},
                                          {"tiled", true},
                                          {"page_width", 190},
                                          {"page_height", 277},
                                          {"overlap", 10}}}});

    REQUIRE(response["ok"].get<bool>());
    CHECK(response["result"]["tiled"] == true);
    CHECK(response["result"]["pages"] == 2);
    CHECK(response["result"]["files"].size() == 2);

    const std::filesystem::path first = directory / "mosaico-c1-r1.svg";
    const std::filesystem::path second = directory / "mosaico-c2-r1.svg";
    CHECK(std::filesystem::exists(first));
    CHECK(std::filesystem::exists(second));
    CHECK(read_file(first).find("viewBox=\"0 -13.5 190 277\"") != std::string::npos);
    CHECK(read_file(second).find("viewBox=\"160 -13.5 190 277\"") != std::string::npos);

    std::filesystem::remove_all(directory, error);
}

TEST_CASE("API: doc.exportSvg valida parâmetros") {
    Api api;
    api.call(json{{"id", 1}, {"method", "doc.new"}});

    const json missing = api.call(
        json{{"id", 2}, {"method", "doc.exportSvg"}, {"params", json::object()}});
    REQUIRE_FALSE(missing["ok"].get<bool>());
    CHECK(missing["error"]["code"] == "invalid_params");

    const json bad_page = api.call(
        json{{"id", 3},
             {"method", "doc.exportSvg"},
             {"params", {{"path", "x.svg"}, {"page_width", 0}}}});
    REQUIRE_FALSE(bad_page["ok"].get<bool>());
    CHECK(bad_page["error"]["code"] == "invalid_params");

    const json negative_overlap = api.call(
        json{{"id", 4},
             {"method", "doc.exportSvg"},
             {"params", {{"path", "x.svg"}, {"overlap", -1}}}});
    REQUIRE_FALSE(negative_overlap["ok"].get<bool>());
    CHECK(negative_overlap["error"]["code"] == "invalid_params");
}
