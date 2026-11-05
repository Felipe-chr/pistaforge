#pragma once

#include <string>
#include <string_view>

#include "core/scene/element.hpp"
#include "core/scene/scene.hpp"

namespace pf {

enum class SnapKind { None, Port, Element, Grid };

[[nodiscard]] std::string_view to_string(SnapKind kind) noexcept;

/// Resultado de um encaixe: onde o ponto deve cair e por quê.
struct SnapResult {
    SnapKind kind{SnapKind::None};
    Vec2 point{};
    double distance{0.0};
    ElementId element{kInvalidId};
    std::string port;
    Vec2 direction{};
};

struct SnapOptions {
    double tolerance_mm{12.0};
    /// Passo da grade em mm; `<= 0` desliga o encaixe na grade.
    double grid_mm{10.0};
    bool ports{true};
    bool elements{true};
    bool grid{true};
    /// Elemento a ignorar (normalmente o que está sendo arrastado).
    ElementId ignore{kInvalidId};
};

/// Procura o alvo mais próximo, na ordem porta > elemento > grade.
///
/// Só considera elementos visíveis (camada visível e `hidden == false`).
[[nodiscard]] SnapResult snap_point(const Scene& scene, Vec2 point, const SnapOptions& options);

} // namespace pf
