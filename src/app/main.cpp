#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/shell/webui_shell.hpp"
#include "core/geometry/bezier.hpp"

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

    pf::Dispatcher dispatcher;
    pf::register_api(dispatcher);

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
