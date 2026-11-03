#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf {

enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity{Severity::Error};
    std::string code;
    std::string message;
    ElementId element{kInvalidId};
    std::string port;
};

/// Todas as portas da cena, na ordem dos elementos.
[[nodiscard]] std::vector<PortInfo> all_ports(const Scene& scene);

/// Regras de integridade da v1: portas, conexões e geometria básica.
[[nodiscard]] std::vector<Diagnostic> validate_scene(const Scene& scene);

[[nodiscard]] std::string_view to_string(Severity severity) noexcept;

} // namespace pf
