#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/geometry/vec2.hpp"

namespace pf {

/// Transformação rígida de um módulo: espelho local, rotação e translação.
struct Transform2 {
    Vec2 translation{};
    double rotation{0.0}; ///< radianos, sentido horário na tela
    bool mirrored{false};

    [[nodiscard]] Vec2 apply(Vec2 local) const noexcept {
        Vec2 point = local;
        if (mirrored) point.x = -point.x;
        return point.rotated(rotation) + translation;
    }

    [[nodiscard]] Vec2 apply_direction(Vec2 local) const noexcept {
        Vec2 direction = local;
        if (mirrored) direction.x = -direction.x;
        return direction.rotated(rotation);
    }
};

/// Instância de um módulo paramétrico da biblioteca.
struct ModuleInstance {
    std::string template_id;
    int template_version{1};
    std::map<std::string, double> params;
    Transform2 transform;

    [[nodiscard]] double param(std::string_view name, double fallback) const {
        const auto it = params.find(std::string{name});
        return it != params.end() ? it->second : fallback;
    }
};

/// Porta declarada por um template, em coordenadas locais.
struct ModulePortDef {
    std::string name;
    Vec2 position;
    Vec2 direction; ///< tangente unitária, sem sentido preferido
};

/// Metadados de um módulo da biblioteca.
struct ModuleTemplate {
    std::string id;
    int version{1};
    std::vector<std::string> port_names;
    /// Pareamento interno (ex.: cruzamento segue reto: w↔e, s↔n).
    std::vector<std::pair<std::string, std::string>> pairs;
    std::map<std::string, double> defaults;
};

/// Catálogo embutido mínimo da v1 (o catálogo completo chega no M3).
[[nodiscard]] const ModuleTemplate* find_module_template(std::string_view id);

/// Portas em coordenadas locais (antes da transformação da instância).
[[nodiscard]] std::vector<ModulePortDef> module_local_ports(const ModuleInstance& instance);

/// Centro do traçado do módulo como segmentos, em coordenadas locais.
[[nodiscard]] std::vector<std::pair<Vec2, Vec2>> module_local_segments(
    const ModuleInstance& instance);

} // namespace pf
