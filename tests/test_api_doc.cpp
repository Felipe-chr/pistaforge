#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/bridge/state.hpp"

using nlohmann::json;
using pf::Dispatcher;

namespace {

struct Api {
    pf::AppState state;
    Dispatcher dispatcher;

    Api() { pf::register_api(dispatcher, state); }

    json call(std::string_view request) const {
        return json::parse(dispatcher.handle(request));
    }

    json call_json(const json& request) const { return call(request.dump()); }
};

json port_request(std::uint64_t element, const std::string& port) {
    return json::array({element, port});
}

} // namespace

TEST_CASE("API: doc.new cria documento vazio") {
    Api api;
    const json response = api.call(
        R"({"id":1,"method":"doc.new","params":{"arena":{"name":"Teste","width":3000,"height":2000,"margin":80}}})");

    REQUIRE(response["ok"].get<bool>());
    CHECK(response["result"]["arena"]["name"] == "Teste");
    CHECK(response["result"]["arena"]["width"] == 3000.0);
    CHECK(response["result"]["arena"]["height"] == 2000.0);
    CHECK(response["result"]["arena"]["margin"] == 80.0);
    CHECK(response["result"]["elements"] == 0);
    CHECK(response["result"]["rev"] == 1);
    CHECK(response["result"]["path"].is_null());
}

TEST_CASE("API: elementos e estado do documento") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");

    const json line = api.call(
        R"({"id":2,"method":"doc.addLine","params":{"a":[0,0],"b":[100,0],"width":19}})");
    REQUIRE(line["ok"].get<bool>());
    CHECK(line["result"]["id"] == 1);
    CHECK(line["result"]["rev"] == 2);

    const json cubic = api.call(
        R"({"id":3,"method":"doc.addCubic","params":{"p0":[100,0],"p1":[150,0],"p2":[200,50],"p3":[200,100]}})");
    REQUIRE(cubic["ok"].get<bool>());
    CHECK(cubic["result"]["id"] == 2);

    const json arc = api.call(
        R"({"id":4,"method":"doc.addArc","params":{"center":[300,100],"radius":100,"start_deg":90,"sweep_deg":90}})");
    REQUIRE(arc["ok"].get<bool>());
    CHECK(arc["result"]["id"] == 3);

    const json module = api.call(
        R"({"id":5,"method":"doc.addModule","params":{"template":"cross.90","params":{"arm":80},"transform":{"tx":500,"ty":500,"rot_deg":30,"mirrored":true}}})");
    REQUIRE(module["ok"].get<bool>());
    CHECK(module["result"]["id"] == 4);

    const json state = api.call(R"({"id":6,"method":"doc.state"})");
    REQUIRE(state["ok"].get<bool>());
    CHECK(state["result"]["elements"].size() == 4);
    CHECK(state["result"]["elements"][0]["type"] == "line");
    CHECK(state["result"]["elements"][1]["type"] == "cubic");
    CHECK(state["result"]["elements"][2]["type"] == "arc");
    CHECK(state["result"]["elements"][3]["type"] == "module");
    CHECK(state["result"]["elements"][3]["transform"]["mirrored"] == true);
    CHECK(state["result"]["elements"][3]["transform"]["rot"] ==
          doctest::Approx(30.0));
    CHECK(state["result"]["revision"] == 5);
}

TEST_CASE("API: conexões, portas e pick") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");
    const json first = api.call(
        R"({"id":2,"method":"doc.addLine","params":{"a":[0,0],"b":[100,0]}})");
    const json second = api.call(
        R"({"id":3,"method":"doc.addLine","params":{"a":[100,0],"b":[200,0]}})");
    const auto first_id = first["result"]["id"].get<std::uint64_t>();
    const auto second_id = second["result"]["id"].get<std::uint64_t>();

    const json connection = api.call_json(json{
        {"id", 4},
        {"method", "doc.connect"},
        {"params",
         {
             {"a", port_request(first_id, "end")},
             {"b", port_request(second_id, "start")},
         }},
    });
    REQUIRE(connection["ok"].get<bool>());
    CHECK(connection["result"]["id"] == 3);

    const json ports = api.call(R"({"id":5,"method":"scene.ports"})");
    REQUIRE(ports["ok"].get<bool>());
    CHECK(ports["result"].size() == 4);
    CHECK(ports["result"][0]["port"] == "start");
    CHECK(ports["result"][1]["port"] == "end");

    const json pick_port = api.call(
        R"({"id":6,"method":"scene.pick","params":{"point":[100,0],"tolerance":5}})");
    REQUIRE(pick_port["ok"].get<bool>());
    CHECK(pick_port["result"]["element"] == first_id);
    CHECK(pick_port["result"]["port"] == "end");
    CHECK(pick_port["result"]["distance"] == doctest::Approx(0.0));

    const json pick_curve = api.call(
        R"({"id":7,"method":"scene.pick","params":{"point":[50,3],"tolerance":10}})");
    REQUIRE(pick_curve["ok"].get<bool>());
    CHECK(pick_curve["result"]["element"] == first_id);
    CHECK(pick_curve["result"]["port"].is_null());
    CHECK(pick_curve["result"]["distance"] == doctest::Approx(3.0));

    const json pick_empty = api.call(
        R"({"id":8,"method":"scene.pick","params":{"point":[500,500],"tolerance":5}})");
    REQUIRE(pick_empty["ok"].get<bool>());
    CHECK(pick_empty["result"]["element"].is_null());
    CHECK(pick_empty["result"]["distance"].is_null());

    const json validation = api.call(R"({"id":9,"method":"scene.validate"})");
    REQUIRE(validation["ok"].get<bool>());
    for (const json& diagnostic : validation["result"]) {
        CHECK(diagnostic["severity"] != "error");
    }

    const json bounds = api.call(R"({"id":10,"method":"scene.bounds"})");
    REQUIRE(bounds["ok"].get<bool>());
    CHECK(bounds["result"]["min"][0] == doctest::Approx(-9.5));
    CHECK(bounds["result"]["min"][1] == doctest::Approx(-9.5));
    CHECK(bounds["result"]["max"][0] == 4000.0);
    CHECK(bounds["result"]["max"][1] == 3000.0);
}

TEST_CASE("API: erros de conexão") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");
    const auto first = api.call(
        R"({"id":2,"method":"doc.addLine","params":{"a":[0,0],"b":[100,0]}})")["result"]["id"].get<std::uint64_t>();
    const auto second = api.call(
        R"({"id":3,"method":"doc.addLine","params":{"a":[100,0],"b":[200,0]}})")["result"]["id"].get<std::uint64_t>();
    const auto third = api.call(
        R"({"id":4,"method":"doc.addLine","params":{"a":[200,0],"b":[300,0]}})")["result"]["id"].get<std::uint64_t>();

    const json missing_element = api.call_json(json{
        {"id", 5},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(9999, "start")}, {"b", port_request(second, "start")}}},
    });
    CHECK_FALSE(missing_element["ok"].get<bool>());
    CHECK(missing_element["error"]["code"] == "not_found");

    const json missing_port = api.call_json(json{
        {"id", 6},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "bogus")}, {"b", port_request(second, "start")}}},
    });
    CHECK_FALSE(missing_port["ok"].get<bool>());
    CHECK(missing_port["error"]["code"] == "invalid_port");

    const json self_connection = api.call_json(json{
        {"id", 7},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "start")}, {"b", port_request(first, "start")}}},
    });
    CHECK_FALSE(self_connection["ok"].get<bool>());
    CHECK(self_connection["error"]["code"] == "invalid_params");

    const json valid = api.call_json(json{
        {"id", 8},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "end")}, {"b", port_request(second, "start")}}},
    });
    REQUIRE(valid["ok"].get<bool>());

    const json duplicate = api.call_json(json{
        {"id", 9},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "end")}, {"b", port_request(third, "start")}}},
    });
    CHECK_FALSE(duplicate["ok"].get<bool>());
    CHECK(duplicate["error"]["code"] == "port_in_use");

    const json disconnected = api.call_json(json{
        {"id", 10},
        {"method", "doc.disconnect"},
        {"params", {{"id", valid["result"]["id"]}}},
    });
    REQUIRE(disconnected["ok"].get<bool>());
    CHECK(disconnected["result"]["removed"] == valid["result"]["id"]);

    const json reconnect = api.call_json(json{
        {"id", 11},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "end")}, {"b", port_request(third, "start")}}},
    });
    REQUIRE(reconnect["ok"].get<bool>());
}

TEST_CASE("API: validação detecta afastamento e desalinhamento") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");
    const auto first = api.call(
        R"({"id":2,"method":"doc.addLine","params":{"a":[0,0],"b":[100,0]}})")["result"]["id"].get<std::uint64_t>();
    const auto gap = api.call(
        R"({"id":3,"method":"doc.addLine","params":{"a":[110,0],"b":[200,0]}})")["result"]["id"].get<std::uint64_t>();

    const json connection = api.call_json(json{
        {"id", 4},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "end")}, {"b", port_request(gap, "start")}}},
    });
    REQUIRE(connection["ok"].get<bool>());

    const json validation = api.call(R"({"id":5,"method":"scene.validate"})");
    bool found_gap = false;
    for (const json& diagnostic : validation["result"]) {
        if (diagnostic["code"] == "connection.gap") found_gap = true;
    }
    CHECK(found_gap);
}

TEST_CASE("API: remover elemento remove conexões") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");
    const auto first = api.call(
        R"({"id":2,"method":"doc.addLine","params":{"a":[0,0],"b":[100,0]}})")["result"]["id"].get<std::uint64_t>();
    const auto second = api.call(
        R"({"id":3,"method":"doc.addLine","params":{"a":[100,0],"b":[200,0]}})")["result"]["id"].get<std::uint64_t>();

    api.call_json(json{
        {"id", 4},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(first, "end")}, {"b", port_request(second, "start")}}},
    });

    const json removed = api.call_json(json{
        {"id", 5},
        {"method", "doc.removeElement"},
        {"params", {{"id", first}}},
    });
    REQUIRE(removed["ok"].get<bool>());

    const json state = api.call(R"({"id":6,"method":"doc.state"})");
    CHECK(state["result"]["elements"].size() == 1);
    CHECK(state["result"]["connections"].empty());

    const json missing = api.call_json(json{
        {"id", 7},
        {"method", "doc.removeElement"},
        {"params", {{"id", first}}},
    });
    CHECK_FALSE(missing["ok"].get<bool>());
    CHECK(missing["error"]["code"] == "not_found");
}

TEST_CASE("API: arena e marcadores") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");

    const json arena = api.call(
        R"({"id":2,"method":"doc.setArena","params":{"width":5000,"height":2500,"margin":120}})");
    REQUIRE(arena["ok"].get<bool>());
    CHECK(arena["result"]["arena"]["width"] == 5000.0);
    CHECK(arena["result"]["arena"]["height"] == 2500.0);
    CHECK(arena["result"]["arena"]["margin"] == 120.0);

    const json invalid = api.call(
        R"({"id":3,"method":"doc.setArena","params":{"width":-10}})");
    CHECK_FALSE(invalid["ok"].get<bool>());
    CHECK(invalid["error"]["code"] == "invalid_params");

    const json marker = api.call(
        R"({"id":4,"method":"doc.addMarker","params":{"type":"start_finish","x":100,"y":200,"angle_deg":45,"text":"largada"}})");
    REQUIRE(marker["ok"].get<bool>());
    CHECK(marker["result"]["id"] == 1);

    const json state = api.call(R"({"id":5,"method":"doc.state"})");
    REQUIRE(state["result"]["markers"].size() == 1);
    CHECK(state["result"]["markers"][0]["type"] == "start_finish");
    CHECK(state["result"]["markers"][0]["x"] == 100.0);
    CHECK(state["result"]["markers"][0]["angle"] == doctest::Approx(45.0));
    CHECK(state["result"]["markers"][0]["text"] == "largada");
}

TEST_CASE("API: erros de criação de elementos") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");

    const json unknown_module = api.call(
        R"({"id":2,"method":"doc.addModule","params":{"template":"nao.existe"}})");
    CHECK_FALSE(unknown_module["ok"].get<bool>());
    CHECK(unknown_module["error"]["code"] == "unknown_template");

    const json bad_arc = api.call(
        R"({"id":3,"method":"doc.addArc","params":{"center":[0,0],"radius":100,"start_deg":0,"sweep_deg":0}})");
    CHECK_FALSE(bad_arc["ok"].get<bool>());
    CHECK(bad_arc["error"]["code"] == "invalid_params");

    const json bad_width = api.call(
        R"({"id":4,"method":"doc.addLine","params":{"a":[0,0],"b":[10,0],"width":0}})");
    CHECK_FALSE(bad_width["ok"].get<bool>());
    CHECK(bad_width["error"]["code"] == "invalid_params");

    const json bad_point = api.call(
        R"({"id":5,"method":"doc.addLine","params":{"a":[0],"b":[10,0]}})");
    CHECK_FALSE(bad_point["ok"].get<bool>());
    CHECK(bad_point["error"]["code"] == "invalid_params");
}

TEST_CASE("API: salvar e carregar documento") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pistaforge_api_test.pftrack";
    std::filesystem::remove(path);

    Api api;
    api.call(R"({"id":1,"method":"doc.new","params":{"arena":{"name":"Disco","width":3200,"height":2400}}})");
    api.call(R"({"id":2,"method":"doc.addLine","params":{"a":[10,10],"b":[500,10]}})");
    api.call(R"({"id":3,"method":"doc.addLine","params":{"a":[500,10],"b":[500,500]}})");
    api.call_json(json{
        {"id", 4},
        {"method", "doc.connect"},
        {"params",
         {{"a", port_request(1, "end")}, {"b", port_request(2, "start")}}},
    });

    const json saved = api.call_json(json{
        {"id", 5},
        {"method", "doc.save"},
        {"params", {{"path", path.string()}}},
    });
    REQUIRE(saved["ok"].get<bool>());
    CHECK(std::filesystem::exists(path));
    CHECK(saved["result"]["path"] == std::filesystem::absolute(path).string());

    Api other;
    const json loaded = other.call_json(json{
        {"id", 6},
        {"method", "doc.load"},
        {"params", {{"path", path.string()}}},
    });
    REQUIRE(loaded["ok"].get<bool>());
    CHECK(loaded["result"]["arena"]["name"] == "Disco");
    CHECK(loaded["result"]["elements"] == 2);
    CHECK(loaded["result"]["connections"] == 1);

    const json state = other.call(R"({"id":7,"method":"doc.state"})");
    CHECK(state["result"]["path"] == std::filesystem::absolute(path).string());
    CHECK(state["result"]["elements"].size() == 2);

    const json pick = other.call(
        R"({"id":8,"method":"scene.pick","params":{"point":[500,250],"tolerance":5}})");
    REQUIRE(pick["ok"].get<bool>());
    CHECK(pick["result"]["element"] == 2);

    std::filesystem::remove(path);
}

TEST_CASE("API: erros de arquivo") {
    Api api;
    api.call(R"({"id":1,"method":"doc.new"})");

    const json save_without_path = api.call(
        R"({"id":2,"method":"doc.save"})");
    CHECK_FALSE(save_without_path["ok"].get<bool>());
    CHECK(save_without_path["error"]["code"] == "invalid_params");

    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "pistaforge_nao_existe.pftrack";
    std::filesystem::remove(missing);

    const json load_missing = api.call_json(json{
        {"id", 3},
        {"method", "doc.load"},
        {"params", {{"path", missing.string()}}},
    });
    CHECK_FALSE(load_missing["ok"].get<bool>());
    CHECK(load_missing["error"]["code"] == "io_error");
}
