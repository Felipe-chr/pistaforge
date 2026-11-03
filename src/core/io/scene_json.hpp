#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/scene/scene.hpp"

namespace pf::io {

/// Documento inválido (JSON malformado ou campos inconsistentes).
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

[[nodiscard]] nlohmann::json to_json(const Scene& scene);
[[nodiscard]] Scene from_json(const nlohmann::json& document);
[[nodiscard]] std::string to_string(const Scene& scene);
[[nodiscard]] Scene from_string(std::string_view text);

} // namespace pf::io
