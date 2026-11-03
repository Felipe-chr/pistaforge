#pragma once

#include <vector>

#include "core/geometry/bbox.hpp"
#include "core/geometry/vec2.hpp"

namespace pf {

/// Segmento de reta em coordenadas absolutas.
struct LineSegment {
    Vec2 a{};
    Vec2 b{};

    [[nodiscard]] double length() const noexcept { return distance(a, b); }

    [[nodiscard]] Vec2 point_at(double t) const noexcept { return lerp(a, b, t); }

    /// Tangente unitária no sentido a → b.
    [[nodiscard]] Vec2 tangent() const noexcept { return (b - a).normalized(); }

    [[nodiscard]] BBox bbox() const noexcept {
        BBox box;
        box.expand(a);
        box.expand(b);
        return box;
    }

    [[nodiscard]] std::vector<Vec2> flatten(double /*tolerance_mm*/) const {
        return {a, b};
    }

    /// Ponto mais próximo do segmento; `t_out` recebe o parâmetro em [0, 1].
    [[nodiscard]] Vec2 closest_point(Vec2 point, double* t_out = nullptr) const noexcept;
};

} // namespace pf
