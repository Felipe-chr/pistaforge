#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/geometry/arc.hpp"
#include "core/geometry/bezier.hpp"
#include "core/geometry/line.hpp"
#include "core/scene/ids.hpp"
#include "core/scene/module.hpp"

namespace pf {

enum class LayerId { Track, Markers, Annotations, Guides };

[[nodiscard]] std::string_view to_string(LayerId layer) noexcept;
[[nodiscard]] std::optional<LayerId> layer_from_string(std::string_view text) noexcept;

struct PortRef {
    ElementId element{};
    std::string port;

    [[nodiscard]] bool operator==(const PortRef&) const = default;
};

/// Porta derivada da forma, não persistida.
struct PortInfo {
    PortRef ref;
    Vec2 position;
    Vec2 direction; ///< tangente unitária, sem sentido preferido
    double curvature{0.0};
    double width_mm{19.0};
};

using Shape = std::variant<LineSegment, ArcSegment, CubicBezier, ModuleInstance>;

struct Element {
    ElementId id{};
    Shape shape;
    double width_mm{19.0};
    LayerId layer{LayerId::Track};
    bool hidden{false};
    bool locked{false};
    std::string label;
    /// Dificuldade manual de 1 a 5 (0 = automática). Persistida só quando > 0.
    int difficulty{0};
};

/// Portas `start`/`end` para primitivas e portas do template para módulos.
[[nodiscard]] std::vector<PortInfo> ports_of(const Element& element);
[[nodiscard]] BBox shape_bbox(const Shape& shape);
[[nodiscard]] double shape_length(const Shape& shape);
[[nodiscard]] Vec2 shape_closest_point(const Shape& shape, Vec2 point, double* t_out = nullptr);
/// Tangente unitária no ponto mais próximo de `point` (para orientar marcas).
[[nodiscard]] Vec2 shape_tangent(const Shape& shape, Vec2 point);
[[nodiscard]] std::vector<Vec2> shape_flatten(const Shape& shape, double tolerance_mm);

/// Translada a forma inteira por `delta` (mm), preservando orientação.
void translate(LineSegment& line, Vec2 delta) noexcept;
void translate(ArcSegment& arc, Vec2 delta) noexcept;
void translate(CubicBezier& bezier, Vec2 delta) noexcept;
void translate(ModuleInstance& module, Vec2 delta) noexcept;
void translate(Shape& shape, Vec2 delta) noexcept;
void translate(Element& element, Vec2 delta) noexcept;

} // namespace pf
