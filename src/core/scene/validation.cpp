#include "core/scene/validation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/analysis/track_analysis.hpp"
#include "core/scene/element_ops.hpp"

namespace pf {
namespace {

constexpr double kPositionToleranceMm = 0.01;
constexpr double kAngleToleranceRad = 0.1 * std::numbers::pi / 180.0;

using PortKey = std::pair<ElementId, std::string>;

std::string port_label(const PortRef& ref) {
    return std::to_string(ref.element) + ":" + ref.port;
}

constexpr double kTightCurveRadiusMm = 60.0;
constexpr double kSharpTurnDeg = 45.0;
constexpr double kOverlapDistanceMm = 3.0;

bool is_crossing_module(const Shape& shape) {
    const auto* module = std::get_if<ModuleInstance>(&shape);
    return module != nullptr && module->template_id.find("cross") != std::string::npos;
}

struct TrackPort {
    Vec2 position{};
    Vec2 direction{};
    bool is_start{false};
    bool valid{false};
};

TrackPort track_port(const Element& element, const std::string& name) {
    TrackPort result;
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

double connection_turn_deg(const Element& a, const std::string& port_a, const Element& b,
                           const std::string& port_b, Vec2* joint_out) {
    const TrackPort first = track_port(a, port_a);
    const TrackPort second = track_port(b, port_b);
    if (!first.valid || !second.valid) return 0.0;

    const Vec2 out = (first.is_start ? -first.direction : first.direction).normalized();
    const Vec2 in = (second.is_start ? second.direction : -second.direction).normalized();
    if (joint_out != nullptr) *joint_out = (first.position + second.position) * 0.5;
    const double dot = std::clamp(out.dot(in), -1.0, 1.0);
    return std::acos(dot) * 180.0 / std::numbers::pi;
}

std::vector<Vec2> sampled_shape(const Shape& shape) {
    std::vector<Vec2> points = shape_flatten(shape, 5.0);
    if (points.size() <= 48) return points;
    std::vector<Vec2> reduced;
    const std::size_t step = std::max<std::size_t>(1, points.size() / 48);
    for (std::size_t index = 0; index < points.size(); index += step) {
        reduced.push_back(points[index]);
    }
    return reduced;
}

double polylines_min_distance(const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    double best = std::numeric_limits<double>::infinity();
    for (const Vec2& first : a) {
        for (const Vec2& second : b) {
            const double candidate = distance_squared(first, second);
            if (candidate < best) best = candidate;
        }
    }
    return std::sqrt(best);
}

} // namespace

std::string_view to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
    }
    return "error";
}

std::vector<PortInfo> all_ports(const Scene& scene) {
    std::vector<PortInfo> result;
    for (const Element& element : scene.elements) {
        const std::vector<PortInfo> ports = ports_of(element);
        result.insert(result.end(), ports.begin(), ports.end());
    }
    return result;
}

namespace {

std::string format_mm(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

void append_track_diagnostics(const Scene& scene, std::vector<Diagnostic>& diagnostics) {
    // Curvas extremamente fechadas.
    for (const Element& element : scene.elements) {
        if (element.layer != LayerId::Track) continue;
        const std::optional<double> radius = shape_radius(element.shape);
        if (radius && *radius < kTightCurveRadiusMm) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Warning,
                .code = "curve.tight",
                .message = "Curva muito fechada (raio " + format_mm(*radius) + " mm).",
                .element = element.id,
            });
        }
    }

    std::set<PortKey> used;
    std::set<std::pair<ElementId, ElementId>> connected;
    for (const Connection& connection : scene.connections) {
        used.insert({connection.a.element, connection.a.port});
        used.insert({connection.b.element, connection.b.port});
        connected.insert({connection.a.element, connection.b.element});
        connected.insert({connection.b.element, connection.a.element});
    }

    int open_ports = 0;
    ElementId first_open = kInvalidId;
    for (const Element& element : scene.elements) {
        if (element.layer != LayerId::Track) continue;
        for (const PortInfo& port : ports_of(element)) {
            if (used.contains({port.ref.element, port.ref.port})) continue;
            ++open_ports;
            if (first_open == kInvalidId) first_open = element.id;
        }
    }
    if (open_ports > 0) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Warning,
            .code = "track.open",
            .message = "Pista com " + std::to_string(open_ports) +
                       " ponta(s) desconectada(s); o trajeto pode não estar fechado.",
            .element = first_open,
        });
    }

    // Viradas bruscas entre elementos conectados.
    for (const Connection& connection : scene.connections) {
        const Element* a = scene.find_element(connection.a.element);
        const Element* b = scene.find_element(connection.b.element);
        if (a == nullptr || b == nullptr) continue;
        if (a->layer != LayerId::Track || b->layer != LayerId::Track) continue;

        const double turn = connection_turn_deg(*a, connection.a.port, *b, connection.b.port, nullptr);
        if (turn >= kSharpTurnDeg) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Warning,
                .code = "turn.sharp",
                .message = "Mudança brusca de direção (" + format_mm(turn) + "°).",
                .element = a->id,
                .port = connection.a.port,
            });
        }
    }

    // Sobreposição incorreta entre trechos não conectados.
    std::vector<const Element*> primitives;
    for (const Element& element : scene.elements) {
        if (element.layer != LayerId::Track) continue;
        if (std::holds_alternative<ModuleInstance>(element.shape)) continue;
        primitives.push_back(&element);
    }

    std::vector<std::vector<Vec2>> samples;
    std::vector<BBox> boxes;
    samples.reserve(primitives.size());
    boxes.reserve(primitives.size());
    for (const Element* element : primitives) {
        samples.push_back(sampled_shape(element->shape));
        boxes.push_back(shape_bbox(element->shape));
    }

    for (std::size_t i = 0; i < primitives.size(); ++i) {
        for (std::size_t j = i + 1; j < primitives.size(); ++j) {
            if (connected.contains({primitives[i]->id, primitives[j]->id})) continue;
            if (!boxes[i].is_valid() || !boxes[j].is_valid()) continue;
            const bool boxes_overlap = boxes[i].min.x <= boxes[j].max.x &&
                                       boxes[j].min.x <= boxes[i].max.x &&
                                       boxes[i].min.y <= boxes[j].max.y &&
                                       boxes[j].min.y <= boxes[i].max.y;
            if (!boxes_overlap) continue;
            if (samples[i].empty() || samples[j].empty()) continue;

            if (polylines_min_distance(samples[i], samples[j]) < kOverlapDistanceMm) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Warning,
                    .code = "track.overlap",
                    .message = "Trechos sobrepostos sem conexão entre os elementos " +
                               std::to_string(primitives[i]->id) + " e " +
                               std::to_string(primitives[j]->id) + ".",
                    .element = primitives[i]->id,
                });
            }
        }
    }

    // Cruzamentos com ligação direta entre portas opostas (conflito de percurso).
    for (const Element& element : scene.elements) {
        if (element.layer != LayerId::Track) continue;
        const auto* module = std::get_if<ModuleInstance>(&element.shape);
        if (module == nullptr) continue;
        const ModuleTemplate* definition = find_module_template(module->template_id);
        if (definition == nullptr) continue;
        if (module->template_id.find("cross") == std::string::npos) continue;

        for (const auto& [first, second] : definition->pairs) {
            for (const Connection& connection : scene.connections) {
                if (connection.a.element != element.id || connection.b.element != element.id) {
                    continue;
                }
                const bool direct = (connection.a.port == first && connection.b.port == second) ||
                                    (connection.a.port == second && connection.b.port == first);
                if (direct) {
                    diagnostics.push_back(Diagnostic{
                        .severity = Severity::Warning,
                        .code = "crossing.conflict",
                        .message = "Cruzamento com ligação direta entre portas opostas (" + first +
                                   " ↔ " + second + ").",
                        .element = element.id,
                    });
                }
            }
        }
    }

    const TrackAnalysis analysis = analyze_track(scene);
    if (analysis.metrics.difficulty >= 75.0) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Warning,
            .code = "track.difficulty",
            .message = "Dificuldade " + analysis.metrics.difficulty_label + " (" +
                       format_mm(analysis.metrics.difficulty) + "/100).",
        });
    }
    if (analysis.metrics.total_length_mm > 0.0 &&
        analysis.metrics.recommended_speed_mps < 0.5) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Info,
            .code = "track.slow",
            .message = "Velocidade recomendada muito baixa (" +
                       std::to_string(analysis.metrics.recommended_speed_mps) + " m/s).",
        });
    }
}

} // namespace

std::vector<Diagnostic> validate_scene(const Scene& scene) {
    std::vector<Diagnostic> diagnostics;

    if (!scene.arena.is_valid()) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .code = "arena.invalid",
            .message = "Dimensões ou margem da arena inválidas.",
        });
    }

    if (scene.schema_version != 1) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Warning,
            .code = "schema.unknown",
            .message = "Versão de schema desconhecida: " +
                       std::to_string(scene.schema_version) + ".",
        });
    }

    std::map<PortKey, PortInfo> ports;
    for (const Element& element : scene.elements) {
        if (element.width_mm <= 0.0) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Error,
                .code = "element.width",
                .message = "Largura do elemento deve ser positiva.",
                .element = element.id,
            });
        }

        if (const auto* line = std::get_if<LineSegment>(&element.shape)) {
            if (line->length() <= 1e-9) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "shape.degenerate",
                    .message = "Reta com comprimento zero.",
                    .element = element.id,
                });
            }
        } else if (const auto* arc = std::get_if<ArcSegment>(&element.shape)) {
            if (!arc->is_valid()) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "shape.degenerate",
                    .message = "Arco com raio ou varredura inválidos.",
                    .element = element.id,
                });
            }
        } else if (const auto* bezier = std::get_if<CubicBezier>(&element.shape)) {
            if (bezier->is_degenerate()) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "shape.degenerate",
                    .message = "Bézier degenerada.",
                    .element = element.id,
                });
            }
        } else if (const auto* module = std::get_if<ModuleInstance>(&element.shape)) {
            const ModuleTemplate* definition = find_module_template(module->template_id);
            if (definition == nullptr) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "module.unknown_template",
                    .message = "Template desconhecido: " + module->template_id + ".",
                    .element = element.id,
                });
            } else if (definition->version != module->template_version) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Warning,
                    .code = "module.version",
                    .message = "Versão do template difere da instância.",
                    .element = element.id,
                });
            }

            for (const auto& [name, value] : module->params) {
                if (value <= 0.0) {
                    diagnostics.push_back(Diagnostic{
                        .severity = Severity::Error,
                        .code = "module.param",
                        .message = "Parâmetro '" + name + "' deve ser positivo.",
                        .element = element.id,
                    });
                }
            }
        }

        for (const PortInfo& port : ports_of(element)) {
            ports[PortKey{port.ref.element, port.ref.port}] = port;
        }
    }

    std::map<PortKey, int> usage;
    std::set<std::string> seen_connections;

    for (const Connection& connection : scene.connections) {
        const PortKey key_a{connection.a.element, connection.a.port};
        const PortKey key_b{connection.b.element, connection.b.port};

        const auto it_a = ports.find(key_a);
        const auto it_b = ports.find(key_b);

        if (it_a == ports.end()) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Error,
                .code = "connection.missing_port",
                .message = "Porta inexistente: " + port_label(connection.a) + ".",
                .element = connection.a.element,
                .port = connection.a.port,
            });
        }
        if (it_b == ports.end()) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Error,
                .code = "connection.missing_port",
                .message = "Porta inexistente: " + port_label(connection.b) + ".",
                .element = connection.b.element,
                .port = connection.b.port,
            });
        }

        if (it_a != ports.end() && it_b != ports.end()) {
            const double gap = distance(it_a->second.position, it_b->second.position);
            if (gap > kPositionToleranceMm) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "connection.gap",
                    .message = "Portas separadas por " + std::to_string(gap) + " mm.",
                    .element = connection.a.element,
                    .port = connection.a.port,
                });
            }

            const double alignment =
                std::abs(it_a->second.direction.dot(it_b->second.direction));
            if (alignment < std::cos(kAngleToleranceRad)) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "connection.angle",
                    .message = "Tangentes desalinhadas na conexão.",
                    .element = connection.a.element,
                    .port = connection.a.port,
                });
            }
        }

        for (const PortKey& key : {key_a, key_b}) {
            const int count = ++usage[key];
            if (count > 1) {
                diagnostics.push_back(Diagnostic{
                    .severity = Severity::Error,
                    .code = "connection.duplicate_port",
                    .message = "Porta conectada mais de uma vez: " +
                               std::to_string(key.first) + ":" + key.second + ".",
                    .element = key.first,
                    .port = key.second,
                });
            }
        }

        std::string canonical = port_label(connection.a) + "|" + port_label(connection.b);
        std::string reversed = port_label(connection.b) + "|" + port_label(connection.a);
        if (canonical > reversed) std::swap(canonical, reversed);
        if (!seen_connections.insert(canonical).second) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Error,
                .code = "connection.duplicate",
                .message = "Conexão duplicada entre " + port_label(connection.a) + " e " +
                           port_label(connection.b) + ".",
                .element = connection.a.element,
                .port = connection.a.port,
            });
        }
    }

    for (const auto& [key, port] : ports) {
        if (usage.find(key) == usage.end()) {
            diagnostics.push_back(Diagnostic{
                .severity = Severity::Warning,
                .code = "port.unconnected",
                .message = "Porta sem conexão: " + key.second + ".",
                .element = key.first,
                .port = key.second,
            });
        }
    }

    append_track_diagnostics(scene, diagnostics);

    return diagnostics;
}

} // namespace pf
