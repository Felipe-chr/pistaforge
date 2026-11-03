#pragma once

#include <cmath>
#include <vector>

#include "core/geometry/bbox.hpp"
#include "core/geometry/vec2.hpp"

namespace pf {

/// Arco de círculo em coordenadas absolutas.
///
/// `start_angle` e `sweep_angle` estão em radianos. O sentido positivo é
/// horário na tela (Y para baixo), igual a `Vec2::rotated`.
struct ArcSegment {
    Vec2 center{};
    double radius{0.0};
    double start_angle{0.0};
    double sweep_angle{0.0};

    [[nodiscard]] double length() const noexcept;

    /// Ponto no parâmetro normalizado `t` em [0, 1].
    [[nodiscard]] Vec2 point_at(double t) const noexcept;

    /// Tangente unitária no sentido de percurso (sinal de `sweep_angle`).
    [[nodiscard]] Vec2 tangent_at(double t) const noexcept;

    [[nodiscard]] BBox bbox() const noexcept;

    /// Polilinha adaptativa com erro de corda limitado por `tolerance_mm`.
    [[nodiscard]] std::vector<Vec2> flatten(double tolerance_mm) const;

    /// Ponto mais próximo do arco; `t_out` recebe o parâmetro em [0, 1].
    [[nodiscard]] Vec2 closest_point(Vec2 point, double* t_out = nullptr) const noexcept;

    [[nodiscard]] bool is_valid() const noexcept {
        return radius > 0.0 && std::abs(sweep_angle) > 1e-12;
    }
};

} // namespace pf
