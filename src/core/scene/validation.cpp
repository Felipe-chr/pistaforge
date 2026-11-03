#include "core/scene/validation.hpp"

#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <utility>

namespace pf {
namespace {

constexpr double kPositionToleranceMm = 0.01;
constexpr double kAngleToleranceRad = 0.1 * std::numbers::pi / 180.0;

using PortKey = std::pair<ElementId, std::string>;

std::string port_label(const PortRef& ref) {
    return std::to_string(ref.element) + ":" + ref.port;
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

    return diagnostics;
}

} // namespace pf
