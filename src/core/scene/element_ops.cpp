#include "core/scene/element_ops.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pf {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kPi = std::numbers::pi;

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

Vec2 shape_point_at(const Shape& shape, double t) {
    return std::visit(
        Overloaded{
            [t](const LineSegment& line) { return line.point_at(t); },
            [t](const ArcSegment& arc) { return arc.point_at(t); },
            [t](const CubicBezier& bezier) { return bezier.eval(t); },
            [t](const ModuleInstance& module) {
                if (module.template_id == "curve.90") {
                    const double radius = module.param("radius", 100.0);
                    const ArcSegment local{
                        .center = {},
                        .radius = radius,
                        .start_angle = 0.0,
                        .sweep_angle = kPi / 2.0,
                    };
                    return module.transform.apply(local.point_at(t));
                }
                return module.transform.translation;
            },
        },
        shape);
}

/// Lado (sinal em relação à normal da corda) do primeiro quarto da curva.
double leading_side(const Shape& shape, Vec2 start, Vec2 end) {
    const Vec2 chord = end - start;
    const double length = chord.length();
    if (length <= kGeometryEpsilon) return 1.0;
    const Vec2 normal = chord.perpendicular() / length;
    const double side = (shape_point_at(shape, 0.25) - start).dot(normal);
    return side < 0.0 ? -1.0 : 1.0;
}

/// Arco entre `a` e `b` com o raio pedido, preservando o sentido e se o arco
/// original era maior que 180°.
std::optional<ArcSegment> arc_from_chord_radius(Vec2 a, Vec2 b, double radius,
                                                double reference_sweep) {
    const Vec2 chord = b - a;
    const double length = chord.length();
    if (length <= kGeometryEpsilon) return std::nullopt;

    const double minimum_radius = length * 0.5;
    const double safe_radius = std::max(radius, minimum_radius + 1e-6);
    const double ratio = std::clamp(length / (2.0 * safe_radius), 0.0, 1.0);
    const double minor_sweep = 2.0 * std::asin(ratio);
    const double sign = reference_sweep >= 0.0 ? 1.0 : -1.0;
    const bool major = std::abs(reference_sweep) > kPi;
    const double sweep = sign * (major ? kTwoPi - minor_sweep : minor_sweep);

    const Vec2 normal = chord.perpendicular() / length;
    const Vec2 center =
        (a + b) * 0.5 + normal * (safe_radius * std::cos(sweep * 0.5) * sign);
    const Vec2 offset = a - center;
    if (offset.length_squared() <= kGeometryEpsilon) return std::nullopt;

    return ArcSegment{
        .center = center,
        .radius = safe_radius,
        .start_angle = std::atan2(offset.y, offset.x),
        .sweep_angle = sweep,
    };
}

/// Arco entre `a` e `b` com a varredura pedida, preservando as pontas.
std::optional<ArcSegment> arc_from_chord_sweep(Vec2 a, Vec2 b, double sweep) {
    const Vec2 chord = b - a;
    const double length = chord.length();
    if (length <= kGeometryEpsilon) return std::nullopt;

    const double limit = kTwoPi - 1e-3;
    const double safe_sweep = std::clamp(sweep, -limit, limit);
    if (std::abs(safe_sweep) < 1e-3) return std::nullopt;

    const double radius = length / (2.0 * std::abs(std::sin(safe_sweep * 0.5)));
    if (!(radius > 0.0) || !std::isfinite(radius)) return std::nullopt;

    const double sign = safe_sweep >= 0.0 ? 1.0 : -1.0;
    const Vec2 normal = chord.perpendicular() / length;
    const Vec2 center =
        (a + b) * 0.5 + normal * (radius * std::cos(safe_sweep * 0.5) * sign);
    const Vec2 offset = a - center;
    if (offset.length_squared() <= kGeometryEpsilon) return std::nullopt;

    return ArcSegment{
        .center = center,
        .radius = radius,
        .start_angle = std::atan2(offset.y, offset.x),
        .sweep_angle = safe_sweep,
    };
}

} // namespace

std::string_view to_string(ShapeType type) noexcept {
    switch (type) {
        case ShapeType::Line: return "line";
        case ShapeType::Cubic: return "cubic";
        case ShapeType::Arc90: return "arc90";
        case ShapeType::Arc180: return "arc180";
        case ShapeType::SCurve: return "s";
    }
    return "line";
}

std::optional<ShapeType> shape_type_from_string(std::string_view text) noexcept {
    if (text == "line" || text == "reta") return ShapeType::Line;
    if (text == "cubic" || text == "bezier" || text == "bézier") return ShapeType::Cubic;
    if (text == "arc90" || text == "arco90") return ShapeType::Arc90;
    if (text == "arc180" || text == "arco180") return ShapeType::Arc180;
    if (text == "s" || text == "scurve" || text == "curva-s") return ShapeType::SCurve;
    return std::nullopt;
}

std::optional<std::pair<Vec2, Vec2>> shape_endpoints(const Shape& shape) {
    if (const auto* line = std::get_if<LineSegment>(&shape)) {
        return std::pair{line->a, line->b};
    }
    if (const auto* arc = std::get_if<ArcSegment>(&shape)) {
        if (!arc->is_valid()) return std::nullopt;
        return std::pair{arc->point_at(0.0), arc->point_at(1.0)};
    }
    if (const auto* bezier = std::get_if<CubicBezier>(&shape)) {
        return std::pair{bezier->p0, bezier->p3};
    }
    if (const auto* module = std::get_if<ModuleInstance>(&shape)) {
        if (module->template_id != "curve.90") return std::nullopt;
        const std::vector<ModulePortDef> ports = module_local_ports(*module);
        if (ports.size() < 2) return std::nullopt;
        return std::pair{module->transform.apply(ports[0].position),
                         module->transform.apply(ports[1].position)};
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> endpoint_port_names(const Shape& shape) {
    if (const auto* module = std::get_if<ModuleInstance>(&shape)) {
        if (module->template_id == "curve.90") {
            return std::pair{std::string{"in"}, std::string{"out"}};
        }
        return std::nullopt;
    }
    if (std::holds_alternative<LineSegment>(shape) || std::holds_alternative<ArcSegment>(shape) ||
        std::holds_alternative<CubicBezier>(shape)) {
        return std::pair{std::string{"start"}, std::string{"end"}};
    }
    return std::nullopt;
}

std::optional<ShapeType> infer_shape_type(const Shape& shape) {
    if (std::holds_alternative<LineSegment>(shape)) return ShapeType::Line;

    if (const auto* arc = std::get_if<ArcSegment>(&shape)) {
        const double sweep = std::abs(arc->sweep_angle) * 180.0 / kPi;
        if (std::abs(sweep - 90.0) < 1.0) return ShapeType::Arc90;
        if (std::abs(sweep - 180.0) < 1.0) return ShapeType::Arc180;
        return std::nullopt;
    }

    if (const auto* bezier = std::get_if<CubicBezier>(&shape)) {
        const Vec2 chord = bezier->p3 - bezier->p0;
        if (chord.length_squared() <= kGeometryEpsilon) return ShapeType::Cubic;
        const Vec2 normal = chord.perpendicular().normalized();
        const double first = (bezier->p1 - bezier->p0).dot(normal);
        const double second = (bezier->p2 - bezier->p0).dot(normal);
        return first * second < 0.0 ? ShapeType::SCurve : ShapeType::Cubic;
    }

    return std::nullopt;
}

std::optional<double> shape_radius(const Shape& shape) {
    return std::visit(
        Overloaded{
            [](const LineSegment&) -> std::optional<double> { return std::nullopt; },
            [](const ArcSegment& arc) -> std::optional<double> {
                if (!arc.is_valid()) return std::nullopt;
                return std::abs(arc.radius);
            },
            [](const CubicBezier& bezier) -> std::optional<double> {
                double maximum_curvature = 0.0;
                for (int i = 0; i <= 48; ++i) {
                    const double t = static_cast<double>(i) / 48.0;
                    maximum_curvature =
                        std::max(maximum_curvature, std::abs(bezier.curvature(t)));
                }
                if (maximum_curvature < 1e-9) return std::nullopt;
                return 1.0 / maximum_curvature;
            },
            [](const ModuleInstance& module) -> std::optional<double> {
                if (module.template_id == "curve.90") {
                    return module.param("radius", 100.0);
                }
                return std::nullopt;
            },
        },
        shape);
}

std::optional<double> shape_angle_deg(const Shape& shape) {
    return std::visit(
        Overloaded{
            [](const LineSegment&) -> std::optional<double> { return 0.0; },
            [](const ArcSegment& arc) -> std::optional<double> {
                if (!arc.is_valid()) return std::nullopt;
                return arc.sweep_angle * 180.0 / kPi;
            },
            [](const CubicBezier& bezier) -> std::optional<double> {
                const Vec2 start = bezier.derivative(0.0).normalized();
                const Vec2 end = bezier.derivative(1.0).normalized();
                if (start.length_squared() <= kGeometryEpsilon ||
                    end.length_squared() <= kGeometryEpsilon) {
                    return std::nullopt;
                }
                return std::atan2(start.cross(end), start.dot(end)) * 180.0 / kPi;
            },
            [](const ModuleInstance& module) -> std::optional<double> {
                if (module.template_id == "curve.90") return 90.0;
                return std::nullopt;
            },
        },
        shape);
}

bool flip_shape(Shape& shape) {
    if (auto* bezier = std::get_if<CubicBezier>(&shape)) {
        const Vec2 chord = bezier->p3 - bezier->p0;
        const double length = chord.length();
        if (length <= kGeometryEpsilon) return false;
        const Vec2 normal = chord.perpendicular() / length;
        const auto reflect = [&](Vec2 point) {
            return point - normal * (2.0 * (point - bezier->p0).dot(normal));
        };
        bezier->p1 = reflect(bezier->p1);
        bezier->p2 = reflect(bezier->p2);
        return true;
    }

    if (auto* arc = std::get_if<ArcSegment>(&shape)) {
        if (!arc->is_valid()) return false;
        const Vec2 start = arc->point_at(0.0);
        const Vec2 end = arc->point_at(1.0);
        const Vec2 chord = end - start;
        const double length = chord.length();
        if (length <= kGeometryEpsilon) return false;
        const Vec2 normal = chord.perpendicular() / length;
        const Vec2 center =
            arc->center - normal * (2.0 * (arc->center - start).dot(normal));
        const Vec2 offset = start - center;
        if (offset.length_squared() <= kGeometryEpsilon) return false;
        arc->center = center;
        arc->start_angle = std::atan2(offset.y, offset.x);
        arc->sweep_angle = -arc->sweep_angle;
        return true;
    }

    if (auto* module = std::get_if<ModuleInstance>(&shape)) {
        if (module->template_id != "curve.90") return false;
        const std::vector<ModulePortDef> ports = module_local_ports(*module);
        if (ports.size() < 2) return false;
        const Vec2 start = module->transform.apply(ports[0].position);
        const Vec2 end = module->transform.apply(ports[1].position);
        const Vec2 chord = end - start;
        const double length = chord.length();
        if (length <= kGeometryEpsilon) return false;
        const Vec2 normal = chord.perpendicular() / length;
        const Vec2 center = module->transform.translation -
                            normal * (2.0 * (module->transform.translation - start).dot(normal));
        const Vec2 first = start - center;
        const Vec2 second = end - center;
        if (first.length_squared() <= kGeometryEpsilon) return false;

        const bool mirrored = first.cross(second) < 0.0;
        double rotation = std::atan2(first.y, first.x);
        if (mirrored) rotation += kPi;

        module->transform.translation = center;
        module->transform.rotation = rotation;
        module->transform.mirrored = mirrored;
        return true;
    }

    return false;
}

bool mirror_shape(Shape& shape, double axis_x) {
    return std::visit(
        Overloaded{
            [axis_x](LineSegment& line) {
                line.a.x = 2.0 * axis_x - line.a.x;
                line.b.x = 2.0 * axis_x - line.b.x;
                return true;
            },
            [axis_x](CubicBezier& bezier) {
                for (Vec2* point : {&bezier.p0, &bezier.p1, &bezier.p2, &bezier.p3}) {
                    point->x = 2.0 * axis_x - point->x;
                }
                return true;
            },
            [axis_x](ArcSegment& arc) {
                arc.center.x = 2.0 * axis_x - arc.center.x;
                arc.start_angle = kPi - arc.start_angle;
                arc.sweep_angle = -arc.sweep_angle;
                return true;
            },
            [axis_x](ModuleInstance& module) {
                module.transform.translation.x = 2.0 * axis_x - module.transform.translation.x;
                module.transform.rotation = -module.transform.rotation;
                module.transform.mirrored = !module.transform.mirrored;
                return true;
            },
        },
        shape);
}

std::optional<Shape> convert_shape(const Shape& shape, ShapeType type) {
    const auto endpoints = shape_endpoints(shape);
    if (!endpoints) return std::nullopt;

    const Vec2 start = endpoints->first;
    const Vec2 end = endpoints->second;
    const Vec2 chord = end - start;
    const double length = chord.length();
    if (length <= 1e-6) return std::nullopt;

    const Vec2 direction = chord / length;
    const Vec2 normal = direction.perpendicular();
    const double side = leading_side(shape, start, end);

    switch (type) {
        case ShapeType::Line:
            return LineSegment{start, end};

        case ShapeType::Cubic: {
            const double bow = std::min(length * 0.25, 160.0) * side;
            return CubicBezier{
                .p0 = start,
                .p1 = start + direction * (length * 0.33) + normal * bow,
                .p2 = start + direction * (length * 0.66) + normal * bow,
                .p3 = end,
            };
        }

        case ShapeType::SCurve: {
            const double bow = std::min(length * 0.25, 160.0) * side;
            return CubicBezier{
                .p0 = start,
                .p1 = start + direction * (length * 0.33) + normal * bow,
                .p2 = start + direction * (length * 0.66) - normal * bow,
                .p3 = end,
            };
        }

        case ShapeType::Arc90:
        case ShapeType::Arc180: {
            const double sweep =
                -side * (type == ShapeType::Arc90 ? kPi / 2.0 : kPi);
            const auto arc = arc_from_chord_sweep(start, end, sweep);
            if (!arc) return std::nullopt;
            return *arc;
        }
    }

    return std::nullopt;
}

std::optional<Shape> smooth_shape(const Shape& shape) {
    if (std::holds_alternative<CubicBezier>(shape)) return shape;
    if (std::holds_alternative<LineSegment>(shape)) {
        return convert_shape(shape, ShapeType::Cubic);
    }

    const auto endpoints = shape_endpoints(shape);
    if (!endpoints) return std::nullopt;

    const Vec2 start = endpoints->first;
    const Vec2 end = endpoints->second;
    const Vec2 chord = end - start;
    const double length = chord.length();
    if (length <= 1e-6) return std::nullopt;

    const Vec2 direction = chord / length;
    Vec2 start_tangent = shape_tangent(shape, start).normalized();
    Vec2 end_tangent = shape_tangent(shape, end).normalized();
    if (start_tangent.length_squared() <= kGeometryEpsilon ||
        end_tangent.length_squared() <= kGeometryEpsilon) {
        return convert_shape(shape, ShapeType::Cubic);
    }
    if (start_tangent.dot(direction) < 0.0) start_tangent = -start_tangent;
    if (end_tangent.dot(direction) < 0.0) end_tangent = -end_tangent;

    const double handle = length / 3.0;
    return CubicBezier{
        .p0 = start,
        .p1 = start + start_tangent * handle,
        .p2 = end - end_tangent * handle,
        .p3 = end,
    };
}

std::optional<Shape> closed_shape(const Shape& shape) {
    if (const auto* arc = std::get_if<ArcSegment>(&shape)) {
        if (!arc->is_valid()) return std::nullopt;
        return ArcSegment{
            .center = arc->center,
            .radius = arc->radius,
            .start_angle = arc->start_angle,
            .sweep_angle = kTwoPi,
        };
    }

    const BBox box = shape_bbox(shape);
    if (!box.is_valid()) return std::nullopt;

    const double radius = std::max({box.width(), box.height(), 10.0}) * 0.5;
    const Vec2 center = box.center();
    const auto endpoints = shape_endpoints(shape);
    const Vec2 start = endpoints ? endpoints->first : center + Vec2{radius, 0.0};
    const Vec2 offset = start - center;
    const double start_angle =
        offset.length_squared() > kGeometryEpsilon ? std::atan2(offset.y, offset.x) : 0.0;

    return ArcSegment{
        .center = center,
        .radius = radius,
        .start_angle = start_angle,
        .sweep_angle = kTwoPi,
    };
}

std::optional<SplitShapes> split_shape(const Shape& shape, double t) {
    const double clamped = std::clamp(t, 0.05, 0.95);

    return std::visit(
        Overloaded{
            [clamped](const LineSegment& line) -> std::optional<SplitShapes> {
                if (line.length() <= kGeometryEpsilon) return std::nullopt;
                const Vec2 middle = line.point_at(clamped);
                return SplitShapes{LineSegment{line.a, middle}, LineSegment{middle, line.b}};
            },
            [clamped](const CubicBezier& bezier) -> std::optional<SplitShapes> {
                if (bezier.is_degenerate()) return std::nullopt;
                const auto [left, right] = bezier.split(clamped);
                return SplitShapes{left, right};
            },
            [clamped](const ArcSegment& arc) -> std::optional<SplitShapes> {
                if (!arc.is_valid()) return std::nullopt;
                const double first_sweep = arc.sweep_angle * clamped;
                const double second_sweep = arc.sweep_angle - first_sweep;
                if (std::abs(first_sweep) < 1e-6 || std::abs(second_sweep) < 1e-6) {
                    return std::nullopt;
                }
                return SplitShapes{
                    ArcSegment{arc.center, arc.radius, arc.start_angle, first_sweep},
                    ArcSegment{arc.center, arc.radius, arc.start_angle + first_sweep,
                               second_sweep},
                };
            },
            [](const ModuleInstance&) -> std::optional<SplitShapes> { return std::nullopt; },
        },
        shape);
}

bool adjust_radius(Shape& shape, double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) return false;

    if (auto* arc = std::get_if<ArcSegment>(&shape)) {
        if (!arc->is_valid()) return false;
        const Vec2 start = arc->point_at(0.0);
        const Vec2 end = arc->point_at(1.0);
        if (distance(start, end) <= 1e-6) {
            // Laço fechado: apenas escala o raio em torno do centro.
            arc->radius *= factor;
            return arc->radius > 0.0;
        }
        const auto updated =
            arc_from_chord_radius(start, end, arc->radius * factor, arc->sweep_angle);
        if (!updated) return false;
        *arc = *updated;
        return true;
    }

    if (auto* bezier = std::get_if<CubicBezier>(&shape)) {
        const Vec2 chord = bezier->p3 - bezier->p0;
        const double length = chord.length();
        if (length <= kGeometryEpsilon) return false;
        const Vec2 normal = chord.perpendicular() / length;
        const auto scale = [&](Vec2 point) {
            const double perpendicular = (point - bezier->p0).dot(normal);
            return point - normal * (perpendicular * (1.0 - 1.0 / factor));
        };
        bezier->p1 = scale(bezier->p1);
        bezier->p2 = scale(bezier->p2);
        return true;
    }

    if (auto* module = std::get_if<ModuleInstance>(&shape)) {
        if (module->template_id != "curve.90") return false;
        const double radius = module->param("radius", 100.0);
        module->params["radius"] = std::max(5.0, radius * factor);
        return true;
    }

    return false;
}

bool adjust_angle(Shape& shape, double delta_deg) {
    if (!std::isfinite(delta_deg)) return false;
    const double delta = delta_deg * kPi / 180.0;

    if (auto* arc = std::get_if<ArcSegment>(&shape)) {
        if (!arc->is_valid()) return false;
        const Vec2 start = arc->point_at(0.0);
        const Vec2 end = arc->point_at(1.0);
        if (distance(start, end) <= 1e-6) {
            const double sign = arc->sweep_angle >= 0.0 ? 1.0 : -1.0;
            const double magnitude =
                std::clamp(std::abs(arc->sweep_angle) + delta, kPi / 180.0, kTwoPi - 1e-3);
            arc->sweep_angle = sign * magnitude;
            return true;
        }

        const double current = arc->sweep_angle;
        const double sign = current >= 0.0 ? 1.0 : -1.0;
        const double magnitude =
            std::clamp(std::abs(current) + delta, kPi / 180.0, kPi);
        const auto updated = arc_from_chord_sweep(start, end, sign * magnitude);
        if (!updated) return false;
        *arc = *updated;
        return true;
    }

    if (auto* bezier = std::get_if<CubicBezier>(&shape)) {
        const double half = delta * 0.5;
        bezier->p1 = bezier->p0 + (bezier->p1 - bezier->p0).rotated(half);
        bezier->p2 = bezier->p3 + (bezier->p2 - bezier->p3).rotated(-half);
        return true;
    }

    return false;
}

bool adjust_length(Shape& shape, double length_mm) {
    if (!(length_mm > 0.0) || !std::isfinite(length_mm)) return false;
    auto* line = std::get_if<LineSegment>(&shape);
    if (line == nullptr) return false;
    const Vec2 direction = (line->b - line->a).normalized();
    if (direction.length_squared() <= kGeometryEpsilon) return false;
    line->b = line->a + direction * length_mm;
    return true;
}

} // namespace pf
