#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/bridge/state.hpp"
#include "app/shell/webui_shell.hpp"
#include "core/geometry/bezier.hpp"
#include "core/io/scene_json.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/validation.hpp"

#ifndef PF_VERSION
#define PF_VERSION "0.0.0"
#endif

namespace {

using Json = nlohmann::json;

int run_selftest(pf::Dispatcher& dispatcher) {
    int failures = 0;

    const auto check = [&failures](bool condition, std::string_view label) {
        std::cout << (condition ? "[ok]   " : "[fail] ") << label << '\n';
        if (!condition) ++failures;
    };

    const auto call = [&dispatcher](std::string_view request) {
        return Json::parse(dispatcher.handle(request));
    };

    const Json ping = call(R"({"id":1,"method":"app.ping","params":{"value":42}})");
    check(ping["ok"].get<bool>(), "app.ping responde ok");
    check(ping["id"] == 1, "app.ping ecoa o id");
    check(ping["result"]["pong"].get<bool>(), "app.ping devolve pong");
    check(ping["result"]["echo"]["value"] == 42, "app.ping ecoa os parâmetros");

    const Json info = call(R"({"id":2,"method":"app.info"})");
    check(info["ok"].get<bool>(), "app.info responde ok");
    check(info["result"]["name"] == "PistaForge", "app.info informa o nome");
    check(info["result"]["arena"]["width"] == 4000.0, "app.info informa a arena padrão");

    const Json unknown = call(R"({"id":3,"method":"nao.existe"})");
    check(!unknown["ok"].get<bool>(), "método desconhecido vira erro");
    check(unknown["error"]["code"] == "unknown_method", "código de erro correto");

    const Json malformed = call("{isto não é json");
    check(!malformed["ok"].get<bool>(), "JSON inválido vira erro");
    check(malformed["error"]["code"] == "parse_error", "erro de parsing identificado");

    const pf::CubicBezier straight{{0.0, 0.0}, {30.0, 0.0}, {70.0, 0.0}, {100.0, 0.0}};
    check(std::abs(straight.length() - 100.0) < 1e-6, "comprimento de Bézier reta");
    check(straight.flatten(0.01).size() >= 2, "flatten gera polilinha");
    check(std::abs(straight.bbox().width() - 100.0) < 1e-9, "bbox da Bézier reta");

    const Json fresh = call(
        R"({"id":10,"method":"doc.new","params":{"arena":{"name":"Selftest","width":3000,"height":2000}}})");
    check(fresh["ok"].get<bool>(), "doc.new responde ok");
    check(fresh["result"]["arena"]["width"] == 3000.0, "doc.new aplica a arena");
    check(fresh["result"]["arena"]["name"] == "Selftest", "doc.new aplica o nome da arena");

    const Json line = call(
        R"({"id":11,"method":"doc.addLine","params":{"a":[100,100],"b":[900,100]}})");
    check(line["ok"].get<bool>(), "doc.addLine responde ok");
    const auto line_id = line["result"]["id"].get<std::uint64_t>();

    const Json arc = call(
        R"({"id":12,"method":"doc.addArc","params":{"center":[900,400],"radius":300,"start_deg":-90,"sweep_deg":90}})");
    check(arc["ok"].get<bool>(), "doc.addArc responde ok");
    const auto arc_id = arc["result"]["id"].get<std::uint64_t>();

    const Json connect_request{
        {"id", 13},
        {"method", "doc.connect"},
        {"params",
         {
             {"a", Json::array({line_id, "end"})},
             {"b", Json::array({arc_id, "start"})},
         }},
    };
    const std::string connect_text = connect_request.dump();
    const Json connection = call(connect_text);
    check(connection["ok"].get<bool>(), "doc.connect responde ok");

    const Json validation = call(R"({"id":14,"method":"scene.validate"})");
    check(validation["ok"].get<bool>(), "scene.validate responde ok");
    bool has_error = false;
    for (const Json& diagnostic : validation["result"]) {
        if (diagnostic["severity"] == "error") has_error = true;
    }
    check(!has_error, "documento conectado sem erros de validação");

    const Json ports = call(R"({"id":15,"method":"scene.ports"})");
    check(ports["ok"].get<bool>(), "scene.ports responde ok");
    check(ports["result"].size() == 4, "scene.ports devolve as quatro portas");

    pf::Scene scene;
    scene.arena.name = "Round-trip";
    const pf::ElementId sample_line =
        scene.add_element(pf::LineSegment{{100.0, 100.0}, {900.0, 100.0}}).id;
    const pf::ElementId sample_arc = scene.add_element(
        pf::ArcSegment{{900.0, 400.0}, 300.0, -std::numbers::pi / 2.0, std::numbers::pi / 2.0}).id;
    scene.add_connection(pf::PortRef{sample_line, "end"}, pf::PortRef{sample_arc, "start"});

    const std::string document_text = pf::io::to_string(scene);
    const pf::Scene restored = pf::io::from_string(document_text);
    check(restored.elements.size() == scene.elements.size(), "round-trip preserva elementos");
    check(restored.connections.size() == scene.connections.size(),
          "round-trip preserva conexões");
    check(restored.next_id == scene.next_id, "round-trip preserva next_id");

    const std::vector<pf::Diagnostic> diagnostics = pf::validate_scene(restored);
    bool round_trip_error = false;
    for (const pf::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == pf::Severity::Error) round_trip_error = true;
    }
    check(!round_trip_error, "round-trip sem erros de validação");

    std::cout << (failures == 0 ? "selftest: OK\n" : "selftest: FALHOU\n");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    bool selftest = false;
    std::size_t port = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--selftest") {
            selftest = true;
        } else if (argument == "--port" && i + 1 < argc) {
            port = static_cast<std::size_t>(std::stoul(argv[++i]));
        }
    }

    pf::AppState state;
    pf::Dispatcher dispatcher;
    pf::register_api(dispatcher, state);

    if (selftest) return run_selftest(dispatcher);

    const std::filesystem::path executable_dir =
        std::filesystem::absolute(argv[0]).parent_path();

    std::cout << "PistaForge " << PF_VERSION << '\n';
    std::cout << "web root: " << executable_dir / "web" << '\n';

    pf::WebUiShell shell(executable_dir);
    if (port != 0 && !shell.set_port(port)) {
        std::cerr << "[webui] porta indisponível: " << port << '\n';
        return 1;
    }

    shell.bind(dispatcher);

    if (!shell.show()) return 1;

    shell.wait();
    return 0;
}
