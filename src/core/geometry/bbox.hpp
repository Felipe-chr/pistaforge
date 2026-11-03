#pragma once

#include <limits>

#include "core/geometry/vec2.hpp"

namespace pf {

/// Caixa alinhada aos eixos. Começa inválida e cresce com `expand`.
struct BBox {
    Vec2 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return min.x <= max.x && min.y <= max.y;
    }

    constexpr void expand(Vec2 point) noexcept {
        if (point.x < min.x) min.x = point.x;
        if (point.y < min.y) min.y = point.y;
        if (point.x > max.x) max.x = point.x;
        if (point.y > max.y) max.y = point.y;
    }

    constexpr void expand(const BBox& other) noexcept {
        if (!other.is_valid()) return;
        expand(other.min);
        expand(other.max);
    }

    [[nodiscard]] constexpr double width() const noexcept {
        return is_valid() ? max.x - min.x : 0.0;
    }

    [[nodiscard]] constexpr double height() const noexcept {
        return is_valid() ? max.y - min.y : 0.0;
    }

    [[nodiscard]] constexpr Vec2 center() const noexcept {
        return {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5};
    }

    [[nodiscard]] constexpr bool contains(Vec2 point) const noexcept {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

    [[nodiscard]] constexpr bool intersects(const BBox& other) const noexcept {
        return !(other.min.x > max.x || other.max.x < min.x || other.min.y > max.y ||
                 other.max.y < min.y);
    }
};

} // namespace pf
