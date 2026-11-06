#include "core/scene/snap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace pf {
namespace {

/// Elementos que participam do encaixe: visíveis, não ocultos e diferentes de `ignore`.
[[nodiscard]] bool snappable(const Scene& scene, const Element& element, ElementId ignore) {
    if (element.id == ignore) return false;
    if (element.hidden) return false;
    return layer_visible(scene, element.layer);
}

} // namespace

std::string_view to_string(SnapKind kind) noexcept {
    switch (kind) {
        case SnapKind::None: return "none";
        case SnapKind::Port: return "port";
        case SnapKind::Element: return "element";
        case SnapKind::Grid: return "grid";
    }
    return "none";
}

SnapResult snap_point(const Scene& scene, Vec2 point, const SnapOptions& options) {
    const double tolerance = std::max(options.tolerance_mm, 0.0);

    // 1. Portas têm prioridade absoluta: se alguma estiver dentro da tolerância,
    //    ela vence mesmo que outra geometria esteja mais perto.
    if (options.ports) {
        SnapResult best;
        double best_distance = std::numeric_limits<double>::infinity();

        for (const Element& element : scene.elements) {
            if (!snappable(scene, element, options.ignore)) continue;

            for (const PortInfo& port : ports_of(element)) {
                const double candidate_distance = distance(port.position, point);
                if (candidate_distance > tolerance) continue;
                if (candidate_distance >= best_distance) continue;

                best_distance = candidate_distance;
                best.kind = SnapKind::Port;
                best.point = port.position;
                best.distance = candidate_distance;
                best.element = element.id;
                best.port = port.ref.port;
                best.direction = port.direction;
            }
        }

        if (best.kind != SnapKind::None) return best;
    }

    // 2. Qualquer ponto sobre a geometria de um elemento.
    if (options.elements) {
        SnapResult best;
        double best_distance = std::numeric_limits<double>::infinity();

        for (const Element& element : scene.elements) {
            if (!snappable(scene, element, options.ignore)) continue;

            const Vec2 closest = shape_closest_point(element.shape, point);
            const double candidate_distance = distance(closest, point);
            if (candidate_distance > tolerance) continue;
            if (candidate_distance >= best_distance) continue;

            best_distance = candidate_distance;
            best.kind = SnapKind::Element;
            best.point = closest;
            best.distance = candidate_distance;
            best.element = element.id;
            best.direction = shape_tangent(element.shape, closest);
        }

        if (best.kind != SnapKind::None) return best;
    }

    // 3. Grade, apenas se nada mais estiver perto.
    SnapResult result;
    if (options.grid && options.grid_mm > 0.0) {
        const Vec2 snapped{
            std::round(point.x / options.grid_mm) * options.grid_mm,
            std::round(point.y / options.grid_mm) * options.grid_mm,
        };
        result.kind = SnapKind::Grid;
        result.point = snapped;
        result.distance = distance(snapped, point);
    }
    return result;
}

} // namespace pf
