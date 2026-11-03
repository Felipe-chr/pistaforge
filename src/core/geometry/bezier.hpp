#pragma once

#include <utility>
#include <vector>

#include "core/geometry/bbox.hpp"
#include "core/geometry/vec2.hpp"

namespace pf {

/// Bézier cúbica com pontos de controle absolutos (mm).
struct CubicBezier {
    Vec2 p0{};
    Vec2 p1{};
    Vec2 p2{};
    Vec2 p3{};

    [[nodiscard]] Vec2 eval(double t) const noexcept;
    [[nodiscard]] Vec2 derivative(double t) const noexcept;
    [[nodiscard]] Vec2 second_derivative(double t) const noexcept;

    /// Curvatura com sinal: positiva curva para a esquerda do sentido de percurso.
    /// Retorna 0 em pontos de velocidade nula (cúspides) para não propagar infinito.
    [[nodiscard]] double curvature(double t) const noexcept;

    [[nodiscard]] std::pair<CubicBezier, CubicBezier> split(double t) const noexcept;

    /// BBox exata (inclui extremos internos).
    [[nodiscard]] BBox bbox() const noexcept;

    /// Comprimento de arco por Gauss–Legendre adaptativo.
    [[nodiscard]] double length(double tolerance = 1e-6) const;

    /// Polilinha adaptativa; o primeiro ponto é `p0` e o último é `p3`.
    [[nodiscard]] std::vector<Vec2> flatten(double tolerance_mm) const;

    /// Ponto mais próximo da curva. `t_out` recebe o parâmetro encontrado.
    [[nodiscard]] Vec2 closest_point(Vec2 point, double* t_out = nullptr) const;

    [[nodiscard]] bool is_degenerate(double epsilon = 1e-12) const noexcept;
};

} // namespace pf
