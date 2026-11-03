#include "app/bridge/dispatcher.hpp"

#include <exception>
#include <utility>

#include "app/bridge/error.hpp"

namespace pf {
namespace {

using Json = nlohmann::json;

Json make_error(const Json& id, std::string_view code, std::string_view message) {
    return Json{
        {"id", id},
        {"ok", false},
        {"error", {{"code", std::string{code}}, {"message", std::string{message}}}},
    };
}

} // namespace

void Dispatcher::add(std::string method, Handler handler) {
    handlers_.insert_or_assign(std::move(method), std::move(handler));
}

bool Dispatcher::has(std::string_view method) const {
    return handlers_.find(std::string{method}) != handlers_.end();
}

std::string Dispatcher::handle(std::string_view request) const {
    Json id = nullptr;

    try {
        Json parsed = Json::parse(request.begin(), request.end());
        if (!parsed.is_object()) {
            return make_error(id, "invalid_request", "A requisição deve ser um objeto JSON.")
                .dump();
        }

        if (parsed.contains("id")) id = parsed["id"];

        if (!parsed.contains("method") || !parsed["method"].is_string()) {
            return make_error(id, "invalid_request", "Campo 'method' ausente ou inválido.")
                .dump();
        }

        const std::string method = parsed["method"].get<std::string>();
        const auto it = handlers_.find(method);
        if (it == handlers_.end()) {
            return make_error(id, "unknown_method", "Método desconhecido: " + method).dump();
        }

        Json params = Json::object();
        if (parsed.contains("params")) params = parsed["params"];

        Json result = it->second(params);
        return Json{{"id", id}, {"ok", true}, {"result", std::move(result)}}.dump();
    } catch (const Json::parse_error& error) {
        return make_error(id, "parse_error", error.what()).dump();
    } catch (const Json::type_error& error) {
        return make_error(id, "type_error", error.what()).dump();
    } catch (const ApiError& error) {
        return make_error(id, error.code(), error.what()).dump();
    } catch (const std::exception& error) {
        return make_error(id, "internal_error", error.what()).dump();
    } catch (...) {
        return make_error(id, "internal_error", "Erro interno desconhecido.").dump();
    }
}

} // namespace pf
