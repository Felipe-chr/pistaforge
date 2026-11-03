#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace pf {

/// Ponte JSON entre o frontend e o núcleo.
///
/// Requisição:  { "id": 7, "method": "app.ping", "params": { ... } }
/// Resposta:   { "id": 7, "ok": true,  "result": { ... } }
/// Erro:       { "id": 7, "ok": false, "error": { "code": "...", "message": "..." } }
class Dispatcher {
public:
    using Json = nlohmann::json;
    using Handler = std::function<Json(const Json& params)>;

    void add(std::string method, Handler handler);

    [[nodiscard]] bool has(std::string_view method) const;

    /// Processa uma requisição e devolve a resposta serializada.
    /// Nunca lança: erros de parsing, método desconhecido e exceções viram erro JSON.
    [[nodiscard]] std::string handle(std::string_view request) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace pf
