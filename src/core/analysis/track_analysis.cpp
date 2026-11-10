#include "core/analysis/track_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>

#include "core/scene/element_ops.hpp"

namespace pf {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTightRadiusMm = 150.0;
constexpr double kSensorRadiusMm = 100.0;
constexpr double kLineLossRadiusMm = 60.0;
constexpr double kSharpTurnDeg = 25.0;
constexpr double kLineLossTurnDeg = 55.0;

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

struct ElementClass {
    bool is_straight{false};
    bool is_curve{false};
    bool is_crossing{false};
    bool is_special{false};
};

ElementClass classify(const Shape& shape) {
    ElementClass result;
    if (std::holds_alternative<LineSegment>(shape)) {
        result.is_straight = true;
        return result;
    }
    if (std::holds_alternative<ArcSegment>(shape) || std::holds_alternative<CubicBezier>(shape)) {
        result.is_curve = true;
        return result;
    }
    if (const auto* module = std::get_if<ModuleInstance>(&shape)) {
        if (module->template_id == "curve.90") {
            result.is_curve = true;
        } else if (module->template_id.find("cross") != std::string::npos) {
            result.is_crossing = true;
        } else {
            result.is_special = true;
        }
        return result;
    }
    result.is_special = true;
    return result;
}

double radius_penalty(double radius) {
    if (radius < 50.0) return 5.0;
    if (radius < 100.0) return 3.0;
    if (radius < 150.0) return 1.5;
    if (radius < 250.0) return 0.5;
    return 0.0;
}

/// Direção de saída de `element` por uma porta (sentido de percurso para fora).
struct PortVector {
    Vec2 position{};
    Vec2 direction{};
    bool is_start{false};
    bool valid{false};
};

PortVector port_vector(const Element& element, const std::string& name) {
    PortVector result;
    const std::vector<PortInfo> ports = ports_of(element);
    if (ports.size() != 2) return result;

    for (std::size_t index = 0; index < ports.size(); ++index) {
        if (ports[index].ref.port != name) continue;
        result.position = ports[index].position;
        result.direction = ports[index].direction;
        result.is_start = index == 0;
        result.valid = true;
        return result;
    }
    return result;
}

} // namespace

std::string_view to_string(CriticalKind kind) noexcept {
    switch (kind) {
        case CriticalKind::TightCurve: return "tight_curve";
        case CriticalKind::AbruptTurn: return "abrupt_turn";
        case CriticalKind::LineLoss: return "line_loss";
        case CriticalKind::SensorCritical: return "sensor_critical";
        case CriticalKind::HighDifficulty: return "high_difficulty";
    }
    return "tight_curve";
}

std::string_view difficulty_label(double score) noexcept {
    if (score < 15.0) return "Muito Fácil";
    if (score < 35.0) return "Fácil";
    if (score < 55.0) return "Médio";
    if (score < 75.0) return "Difícil";
    return "Extremo";
}

int element_auto_difficulty(const Element& element) {
    const ElementClass kind = classify(element.shape);
    if (kind.is_straight) return 0;
    if (kind.is_crossing) return 2;
    if (kind.is_special) return 3;

    const std::optional<double> radius = shape_radius(element.shape);
    if (!radius) return 1;
    const double value = *radius;
    if (value < 50.0) return 5;
    if (value < 100.0) return 4;
    if (value < 150.0) return 3;
    if (value < 250.0) return 2;
    return 1;
}

TrackAnalysis analyze_track(const Scene& scene) {
    TrackAnalysis analysis;
    TrackMetrics& metrics = analysis.metrics;

    std::vector<const Element*> track;
    track.reserve(scene.elements.size());
    for (const Element& element : scene.elements) {
        if (element.layer != LayerId::Track) continue;
        track.push_back(&element);
    }
    metrics.element_count = static_cast<int>(track.size());

    double radius_penalty_total = 0.0;
    const Element* hardest = nullptr;
    int hardest_score = -1;

    for (const Element* element : track) {
        const ElementClass kind = classify(element->shape);
        const double length = shape_length(element->shape);
        metrics.total_length_mm += length;

        if (kind.is_straight) {
            ++metrics.straight_count;
            metrics.straight_length_mm += length;
        } else if (kind.is_curve) {
            ++metrics.curve_count;
            metrics.curve_length_mm += length;

            const std::optional<double> radius = shape_radius(element->shape);
            if (radius) {
                radius_penalty_total += radius_penalty(*radius);
                const Vec2 position = shape_point_at(element->shape, 0.5);

                if (*radius < kTightRadiusMm) {
                    ++metrics.tight_curve_count;
                    analysis.critical_points.push_back(CriticalPoint{
                        .kind = CriticalKind::TightCurve,
                        .position = position,
                        .element = element->id,
                        .severity = std::clamp((kTightRadiusMm - *radius) / kTightRadiusMm, 0.2, 1.0),
                        .t = 0.5,
                        .radius_mm = *radius,
                    });
                }
                if (*radius < kSensorRadiusMm) {
                    analysis.critical_points.push_back(CriticalPoint{
                        .kind = CriticalKind::SensorCritical,
                        .position = position,
                        .element = element->id,
                        .severity = std::clamp((kSensorRadiusMm - *radius) / kSensorRadiusMm, 0.25, 1.0),
                        .t = 0.5,
                        .radius_mm = *radius,
                    });
                }
                if (*radius < kLineLossRadiusMm) {
                    analysis.critical_points.push_back(CriticalPoint{
                        .kind = CriticalKind::LineLoss,
                        .position = position,
                        .element = element->id,
                        .severity = std::clamp((kLineLossRadiusMm - *radius) / kLineLossRadiusMm, 0.4, 1.0),
                        .t = 0.5,
                        .radius_mm = *radius,
                    });
                }
            }
        } else if (kind.is_crossing) {
            ++metrics.crossing_count;
        } else {
            ++metrics.special_count;
        }

        const int score = element_auto_difficulty(*element);
        if (score > hardest_score) {
            hardest_score = score;
            hardest = element;
        }
        if (score >= 4) {
            analysis.critical_points.push_back(CriticalPoint{
                .kind = CriticalKind::HighDifficulty,
                .position = shape_point_at(element->shape, 0.5),
                .element = element->id,
                .severity = static_cast<double>(score) / 5.0,
                .t = 0.5,
            });
        }
    }

    // Ligações: curvas consecutivas e viradas bruscas.
    for (const Connection& connection : scene.connections) {
        const Element* a = scene.find_element(connection.a.element);
        const Element* b = scene.find_element(connection.b.element);
        if (a == nullptr || b == nullptr) continue;
        if (a->layer != LayerId::Track || b->layer != LayerId::Track) continue;
        ++metrics.connection_count;

        const ElementClass kind_a = classify(a->shape);
        const ElementClass kind_b = classify(b->shape);
        if (kind_a.is_curve && kind_b.is_curve) {
            ++metrics.consecutive_curve_links;
        }

        const PortVector port_a = port_vector(*a, connection.a.port);
        const PortVector port_b = port_vector(*b, connection.b.port);
        if (!port_a.valid || !port_b.valid) continue;

        const Vec2 out = port_a.is_start ? -port_a.direction : port_a.direction;
        const Vec2 in = port_b.is_start ? port_b.direction : -port_b.direction;
        const double dot = std::clamp(out.normalized().dot(in.normalized()), -1.0, 1.0);
        const double turn_deg = std::acos(dot) * 180.0 / kPi;

        if (turn_deg >= kSharpTurnDeg) {
            ++metrics.sharp_turn_count;
            const Vec2 joint = (port_a.position + port_b.position) * 0.5;
            analysis.critical_points.push_back(CriticalPoint{
                .kind = CriticalKind::AbruptTurn,
                .position = joint,
                .element = a->id,
                .severity = std::clamp((turn_deg - kSharpTurnDeg) / (90.0 - kSharpTurnDeg), 0.2, 1.0),
                .angle_deg = turn_deg,
            });
            if (turn_deg >= kLineLossTurnDeg) {
                analysis.critical_points.push_back(CriticalPoint{
                    .kind = CriticalKind::LineLoss,
                    .position = joint,
                    .element = a->id,
                    .severity = std::clamp((turn_deg - kLineLossTurnDeg) / (180.0 - kLineLossTurnDeg),
                                           0.4, 1.0),
                    .angle_deg = turn_deg,
                });
            }
        }
    }

    if (hardest != nullptr && hardest_score < 4 && hardest_score > 0) {
        analysis.critical_points.push_back(CriticalPoint{
            .kind = CriticalKind::HighDifficulty,
            .position = shape_point_at(hardest->shape, 0.5),
            .element = hardest->id,
            .severity = static_cast<double>(hardest_score) / 5.0,
            .t = 0.5,
        });
    }

    double score = static_cast<double>(metrics.curve_count) * 2.2 +
                   static_cast<double>(metrics.crossing_count) * 7.0 +
                   static_cast<double>(metrics.special_count) * 4.0 +
                   static_cast<double>(metrics.consecutive_curve_links) * 2.5 +
                   static_cast<double>(metrics.sharp_turn_count) * 3.0 + radius_penalty_total;
    metrics.difficulty = std::clamp(score, 0.0, 100.0);
    metrics.difficulty_label = std::string{difficulty_label(metrics.difficulty)};

    const double difficulty_ratio = metrics.difficulty / 100.0;
    metrics.recommended_speed_mps = std::clamp(1.4 - difficulty_ratio * 1.0, 0.35, 1.4);

    const double length_m = metrics.total_length_mm / 1000.0;
    const double slow_factor = 1.0 + static_cast<double>(metrics.sharp_turn_count) * 0.02;
    metrics.estimated_time_s =
        metrics.recommended_speed_mps > 0.0
            ? length_m / metrics.recommended_speed_mps * slow_factor
            : 0.0;

    return analysis;
}

} // namespace pf
