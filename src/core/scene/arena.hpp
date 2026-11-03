#pragma once

#include <string>

#include "core/geometry/bbox.hpp"
#include "core/geometry/vec2.hpp"

namespace pf {

/// Retângulo que define o mundo e a área impressa. Origem no canto superior
/// esquerdo, X para a direita e Y para baixo, em milímetros.
struct Arena {
    std::string name{"Arena"};
    double width_mm{4000.0};
    double height_mm{3000.0};
    double margin_mm{100.0};
    bool show_dimensions{true};

    [[nodiscard]] bool is_valid() const noexcept {
        return width_mm > 0.0 && height_mm > 0.0 && margin_mm >= 0.0 &&
               margin_mm * 2.0 < width_mm && margin_mm * 2.0 < height_mm;
    }

    [[nodiscard]] BBox bounds() const noexcept {
        BBox box;
        box.expand(Vec2{0.0, 0.0});
        box.expand(Vec2{width_mm, height_mm});
        return box;
    }
};

} // namespace pf
