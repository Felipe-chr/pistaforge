#include <stdexcept>
#include <string_view>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"

using pf::Dispatcher;

namespace {

Dispatcher make_dispatcher() {
    Dispatcher dispatcher;
    pf::register_api(dispatcher);
    return dispatcher;
}

nlohmann::json call(const Dispatcher& dispatcher, std::string_view request) {
    return nlohmann::json::parse(dispatcher.handle(request));
}

} // namespace

TEST_CASE("Dispatcher: app.ping") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, R"({"id":7,"method":"app.ping","params":{"v":3}})");

    CHECK(response["id"] == 7);
    CHECK(response["ok"].get<bool>());
    CHECK(response["result"]["pong"].get<bool>());
    CHECK(response["result"]["echo"]["v"] == 3);
}

TEST_CASE("Dispatcher: app.info") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, R"({"id":1,"method":"app.info"})");

    CHECK(response["ok"].get<bool>());
    CHECK(response["result"]["name"] == "PistaForge");
    CHECK(response["result"]["cxx"] == "C++23");
    CHECK(response["result"]["arena"]["width"] == 4000.0);
    CHECK(response["result"]["arena"]["height"] == 3000.0);
}

TEST_CASE("Dispatcher: método desconhecido") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, R"({"id":2,"method":"doc.new"})");

    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(response["error"]["code"] == "unknown_method");
    CHECK(response["id"] == 2);
}

TEST_CASE("Dispatcher: JSON inválido") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, "{nao e json");

    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(response["error"]["code"] == "parse_error");
}

TEST_CASE("Dispatcher: requisição não-objeto") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, "[1,2,3]");

    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(response["error"]["code"] == "invalid_request");
}

TEST_CASE("Dispatcher: método ausente") {
    const Dispatcher dispatcher = make_dispatcher();
    const auto response = call(dispatcher, R"({"id":9})");

    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(response["error"]["code"] == "invalid_request");
    CHECK(response["id"] == 9);
}

TEST_CASE("Dispatcher: exceção do handler vira erro interno") {
    Dispatcher dispatcher;
    dispatcher.add("boom", [](const Dispatcher::Json&) -> Dispatcher::Json {
        throw std::runtime_error("falha proposital");
    });

    const auto response = call(dispatcher, R"({"id":4,"method":"boom"})");
    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(response["error"]["code"] == "internal_error");
    CHECK(response["id"] == 4);
}
