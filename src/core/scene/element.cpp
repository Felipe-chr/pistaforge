#include "core/scene/element.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pf {
namespace {

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

} // namespace

std::string_view to_string(LayerId layer) noexcept {
    switch (layer) {
        case LayerId::Track: return "track";
        case LayerId::Markers: return "markers";
        case LayerId::Annotations: return "annotations";
        case LayerId::Guides: return "guides";
    }
    return "track";
}

std::optional<LayerId> layer_from_string(std::string_view text) noexcept {
    if (text == "track") return LayerId::Track;
    if (text == "markers") return LayerId::Markers;
    if (text == "annotations") return LayerId::Annotations;
    if (text == "guides") return LayerId::Guides;
    return std::nullopt;
}

std::vector<PortInfo> ports_of(const Element& element) {
    std::vector<PortInfo> ports;

    const auto add = [&ports, &element](std::string name, Vec2 position, Vec2 direction,
                                        double curvature) {
        ports.push_back(PortInfo{
            .ref = PortRef{element.id, std::move(name)},
            .position = position,
            .direction = direction,
            .curvature = curvature,
            .width_mm = element.width_mm,
        });
    };

    if (const auto* line = std::get_if<LineSegment>(&element.shape)) {
        const Vec2 direction = line->tangent();
        add("start", line->a, direction, 0.0);
        add("end", line->b, direction, 0.0);
    } else if (const auto* arc = std::get_if<ArcSegment>(&element.shape)) {
        const double curvature =
            arc->radius > 0.0 ? std::copysign(1.0 / arc->radius, arc->sweep_angle) : 0.0;
        add("start", arc->point_at(0.0), arc->tangent_at(0.0), curvature);
        add("end", arc->point_at(1.0), arc->tangent_at(1.0), curvature);
    } else if (const auto* bezier = std::get_if<CubicBezier>(&element.shape)) {
        add("start", bezier->p0, bezier->derivative(0.0).normalized(), bezier->curvature(0.0));
        add("end", bezier->p3, bezier->derivative(1.0).normalized(), bezier->curvature(1.0));
    } else if (const auto* module = std::get_if<ModuleInstance>(&element.shape)) {
        for (const ModulePortDef& port : module_local_ports(*module)) {
            add(port.name, module->transform.apply(port.position),
                module->transform.apply_direction(port.direction).normalized(), 0.0);
        }
    }

    return ports;
}

BBox shape_bbox(const Shape& shape) {
    return std::visit(
        Overloaded{
            [](const LineSegment& line) { return line.bbox(); },
            [](const ArcSegment& arc) { return arc.bbox(); },
            [](const CubicBezier& bezier) { return bezier.bbox(); },
            [](const ModuleInstance& module) {
                BBox box;
                for (const auto& [a, b] : module_local_segments(module)) {
                    box.expand(module.transform.apply(a));
                    box.expand(module.transform.apply(b));
                }
                return box;
            },
        },
        shape);
}

double shape_length(const Shape& shape) {
    return std::visit(
        Overloaded{
            [](const LineSegment& line) { return line.length(); },
            [](const ArcSegment& arc) { return arc.length(); },
            [](const CubicBezier& bezier) { return bezier.length(); },
            [](const ModuleInstance& module) {
                double total = 0.0;
                for (const auto& [a, b] : module_local_segments(module)) {
                    total += distance(module.transform.apply(a), module.transform.apply(b));
                }
                return total;
            },
        },
        shape);
}

Vec2 shape_closest_point(const Shape& shape, Vec2 point, double* t_out) {
    if (t_out != nullptr) *t_out = 0.0;

    return std::visit(
        Overloaded{
            [point, t_out](const LineSegment& line) { return line.closest_point(point, t_out); },
            [point, t_out](const ArcSegment& arc) { return arc.closest_point(point, t_out); },
            [point, t_out](const CubicBezier& bezier) { return bezier.closest_point(point, t_out); },
            [point](const ModuleInstance& module) {
                Vec2 best = point;
                double best_distance = std::numeric_limits<double>::max();
                for (const auto& [a, b] : module_local_segments(module)) {
                    const LineSegment segment{module.transform.apply(a),
                                              module.transform.apply(b)};
                    const Vec2 candidate = segment.closest_point(point);
                    const double candidate_distance = distance_squared(candidate, point);
                    if (candidate_distance < best_distance) {
                        best_distance = candidate_distance;
                        best = candidate;
                    }
                }
                return best;
            },
        },
        shape);
}

Vec2 shape_tangent(const Shape& shape, Vec2 point) {
    return std::visit(
        Overloaded{
            [](const LineSegment& line) { return line.tangent(); },
            [point](const ArcSegment& arc) {
                double t = 0.0;
                static_cast<void>(arc.closest_point(point, &t));
                return arc.tangent_at(t).normalized();
            },
            [point](const CubicBezier& bezier) {
                double t = 0.0;
                static_cast<void>(bezier.closest_point(point, &t));
                Vec2 tangent = bezier.derivative(t);
                if (tangent.length_squared() <= kGeometryEpsilon) {
                    const double before = std::max(0.0, t - 1e-3);
                    const double after = std::min(1.0, t + 1e-3);
                    tangent = bezier.eval(after) - bezier.eval(before);
                }
                return tangent.normalized();
            },
            [point](const ModuleInstance& module) {
                Vec2 best{};
                double best_distance = std::numeric_limits<double>::max();
                for (const auto& [a, b] : module_local_segments(module)) {
                    const Vec2 start = module.transform.apply(a);
                    const Vec2 end = module.transform.apply(b);
                    const Vec2 candidate = LineSegment{start, end}.closest_point(point);
                    const double candidate_distance = distance_squared(candidate, point);
                    if (candidate_distance < best_distance) {
                        best_distance = candidate_distance;
                        best = (end - start).normalized();
                    }
                }
                return best;
            },
        },
        shape);
}

std::vector<Vec2> shape_flatten(const Shape& shape, double tolerance_mm) {
    return std::visit(
        Overloaded{
            [tolerance_mm](const LineSegment& line) { return line.flatten(tolerance_mm); },
            [tolerance_mm](const ArcSegment& arc) { return arc.flatten(tolerance_mm); },
            [tolerance_mm](const CubicBezier& bezier) { return bezier.flatten(tolerance_mm); },
            [](const ModuleInstance& module) {
                std::vector<Vec2> points;
                for (const auto& [a, b] : module_local_segments(module)) {
                    if (points.empty()) points.push_back(module.transform.apply(a));
                    points.push_back(module.transform.apply(b));
                }
                return points;
            },
        },
        shape);
}

void translate(LineSegment& line, Vec2 delta) noexcept {
    line.a += delta;
    line.b += delta;
}

void translate(ArcSegment& arc, Vec2 delta) noexcept {
    arc.center += delta;
}

void translate(CubicBezier& bezier, Vec2 delta) noexcept {
    bezier.p0 += delta;
    bezier.p1 += delta;
    bezier.p2 += delta;
    bezier.p3 += delta;
}

void translate(ModuleInstance& module, Vec2 delta) noexcept {
    module.transform.translation += delta;
}

void translate(Shape& shape, Vec2 delta) noexcept {
    std::visit(
        Overloaded{
            [delta](LineSegment& line) { translate(line, delta); },
            [delta](ArcSegment& arc) { translate(arc, delta); },
            [delta](CubicBezier& bezier) { translate(bezier, delta); },
            [delta](ModuleInstance& module) { translate(module, delta); },
        },
        shape);
}

void translate(Element& element, Vec2 delta) noexcept {
    translate(element.shape, delta);
}

} // namespace pf
