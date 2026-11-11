#include "core/sim/path_table.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/scene/module.hpp"

namespace pf {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * kPi;

const Connection* connection_at(const Scene& scene, ElementId id, std::string_view port) {
    for (const Connection& connection : scene.connections) {
        if (connection.a.element == id && connection.a.port == port) return &connection;
        if (connection.b.element == id && connection.b.port == port) return &connection;
    }
    return nullptr;
}

PortRef other_end(const Connection& connection, ElementId id, std::string_view port) {
    if (connection.a.element == id && connection.a.port == port) return connection.b;
    return connection.a;
}

bool is_track_element(const Element& element) {
    return !element.hidden && element.layer == LayerId::Track;
}

struct OrientedPath {
    std::vector<Vec2> points;
    std::string exit_port;
    bool reversed{false};
    bool ok{false};
};

OrientedPath orient_primitive(const Element& element, const std::string& entry, double tolerance) {
    const std::vector<PortInfo> ports = ports_of(element);
    if (ports.size() != 2) return {};

    std::vector<Vec2> points = shape_flatten(element.shape, tolerance);
    if (points.size() < 2) return {};

    OrientedPath result;
    result.points = std::move(points);
    if (entry == ports[0].ref.port) {
        result.exit_port = ports[1].ref.port;
    } else if (entry == ports[1].ref.port) {
        std::reverse(result.points.begin(), result.points.end());
        result.exit_port = ports[0].ref.port;
        result.reversed = true;
    } else {
        return {};
    }
    result.ok = true;
    return result;
}

OrientedPath orient_module(const Element& element, const std::string& entry) {
    const auto* module = std::get_if<ModuleInstance>(&element.shape);
    if (module == nullptr) return {};

    const std::vector<PortInfo> ports = ports_of(element);
    if (ports.size() < 2) return {};

    Vec2 entry_position{};
    bool entry_found = false;
    for (const PortInfo& port : ports) {
        if (port.ref.port == entry) {
            entry_position = port.position;
            entry_found = true;
            break;
        }
    }
    if (!entry_found) return {};

    std::string exit_port;
    if (const ModuleTemplate* templ = find_module_template(module->template_id);
        templ != nullptr && !templ->pairs.empty()) {
        for (const auto& [first, second] : templ->pairs) {
            if (first == entry) {
                exit_port = second;
                break;
            }
            if (second == entry) {
                exit_port = first;
                break;
            }
        }
    }
    if (exit_port.empty()) {
        for (const PortInfo& port : ports) {
            if (port.ref.port != entry) {
                exit_port = port.ref.port;
                break;
            }
        }
    }
    if (exit_port.empty()) return {};

    const std::vector<std::pair<Vec2, Vec2>> local_segments = module_local_segments(*module);
    if (local_segments.empty()) return {};

    std::vector<Vec2> points;
    const bool crossing = module->template_id.find("cross") != std::string::npos;
    if (crossing) {
        double best_distance = std::numeric_limits<double>::max();
        std::pair<Vec2, Vec2> best_segment{};
        for (const auto& [local_a, local_b] : local_segments) {
            const Vec2 a = module->transform.apply(local_a);
            const Vec2 b = module->transform.apply(local_b);
            const double candidate =
                std::min(distance(a, entry_position), distance(b, entry_position));
            if (candidate < best_distance) {
                best_distance = candidate;
                best_segment = {a, b};
            }
        }
        points = {best_segment.first, best_segment.second};
    } else {
        for (const auto& [local_a, local_b] : local_segments) {
            const Vec2 a = module->transform.apply(local_a);
            const Vec2 b = module->transform.apply(local_b);
            if (points.empty()) {
                points.push_back(a);
                points.push_back(b);
            } else if (distance(points.back(), a) <= distance(points.back(), b)) {
                points.push_back(b);
            } else {
                points.push_back(a);
            }
        }
    }
    if (points.size() < 2) return {};

    if (distance(points.back(), entry_position) < distance(points.front(), entry_position)) {
        std::reverse(points.begin(), points.end());
    }

    OrientedPath result;
    result.points = std::move(points);
    result.exit_port = exit_port;
    result.ok = true;
    return result;
}

double wrap_radians(double angle) {
    while (angle > kPi) angle -= kTwoPi;
    while (angle <= -kPi) angle += kTwoPi;
    return angle;
}

std::string format_number(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    std::string text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text;
}

} // namespace

PathTable build_path_table(const Scene& scene, const PathTableOptions& options) {
    PathTable table;
    table.step_mm = std::max(1.0, options.step_mm);
    const double tolerance = std::max(0.01, options.tolerance_mm);

    std::vector<const Element*> track;
    track.reserve(scene.elements.size());
    for (const Element& element : scene.elements) {
        if (is_track_element(element)) track.push_back(&element);
    }
    if (track.empty()) return table;

    std::map<ElementId, double> widths;
    for (const Element* element : track) widths[element->id] = element->width_mm;

    // ------------------------------------------------------------------
    // Ponto de partida: marcador de largada, porta livre ou primeiro elemento.
    // ------------------------------------------------------------------
    ElementId start_element = kInvalidId;
    std::string entry_port;

    const Marker* start_marker = nullptr;
    for (const Marker& marker : scene.markers) {
        if (marker.type == MarkerType::Start || marker.type == MarkerType::StartFinish) {
            start_marker = &marker;
            break;
        }
    }
    if (start_marker != nullptr) {
        double best = std::numeric_limits<double>::max();
        for (const Element* element : track) {
            for (const PortInfo& port : ports_of(*element)) {
                const double candidate = distance(port.position, start_marker->position);
                if (candidate < best) {
                    best = candidate;
                    start_element = element->id;
                    entry_port = port.ref.port;
                }
            }
        }
    }

    if (start_element == kInvalidId) {
        for (const Element* element : track) {
            for (const PortInfo& port : ports_of(*element)) {
                if (connection_at(scene, element->id, port.ref.port) == nullptr) {
                    start_element = element->id;
                    entry_port = port.ref.port;
                    break;
                }
            }
            if (start_element != kInvalidId) break;
        }
    }

    if (start_element == kInvalidId) {
        const std::vector<PortInfo> ports = ports_of(*track.front());
        if (!ports.empty()) {
            start_element = track.front()->id;
            entry_port = ports.front().ref.port;
        }
    }
    if (start_element == kInvalidId) return table;
    table.start_element = start_element;

    // ------------------------------------------------------------------
    // Percurso seguindo as conexões.
    // ------------------------------------------------------------------
    std::vector<std::vector<Vec2>> chains;
    std::vector<ElementId> chain_elements;
    std::vector<bool> chain_reversed;
    std::set<ElementId> visited;
    bool used_connection = false;

    ElementId current = start_element;
    std::string current_entry = entry_port;
    while (current != kInvalidId) {
        if (visited.contains(current)) break;
        const Element* element = scene.find_element(current);
        if (element == nullptr || !is_track_element(*element)) break;
        visited.insert(current);

        OrientedPath oriented;
        if (std::holds_alternative<ModuleInstance>(element->shape)) {
            oriented = orient_module(*element, current_entry);
        } else {
            oriented = orient_primitive(*element, current_entry, tolerance);
        }
        if (!oriented.ok) break;

        chains.push_back(oriented.points);
        chain_elements.push_back(current);
        chain_reversed.push_back(oriented.reversed);

        const Connection* connection = connection_at(scene, current, oriented.exit_port);
        if (connection == nullptr) break;
        used_connection = true;

        const PortRef next = other_end(*connection, current, oriented.exit_port);
        if (next.element == start_element && next.port == entry_port) {
            table.closed = true;
            break;
        }
        current = next.element;
        current_entry = next.port;
    }

    table.ordered = used_connection;

    // Sem conexões: percorre todos os elementos na ordem do documento.
    if (!used_connection) {
        chains.clear();
        chain_elements.clear();
        chain_reversed.clear();
        for (const Element* element : track) {
            const std::vector<PortInfo> ports = ports_of(*element);
            if (ports.empty()) continue;
            OrientedPath oriented;
            if (std::holds_alternative<ModuleInstance>(element->shape)) {
                oriented = orient_module(*element, ports.front().ref.port);
            } else {
                oriented = orient_primitive(*element, ports.front().ref.port, tolerance);
            }
            if (!oriented.ok) continue;
            chains.push_back(oriented.points);
            chain_elements.push_back(element->id);
            chain_reversed.push_back(oriented.reversed);
        }
    }

    // ------------------------------------------------------------------
    // Polilinha global e trechos.
    // ------------------------------------------------------------------
    std::vector<Vec2> vertices;
    std::vector<PathSegmentInfo> segments;
    double total = 0.0;

    for (std::size_t index = 0; index < chains.size(); ++index) {
        const std::vector<Vec2>& chain = chains[index];
        if (chain.size() < 2) continue;

        double length = 0.0;
        for (std::size_t i = 1; i < chain.size(); ++i) length += distance(chain[i - 1], chain[i]);
        if (length <= 1e-9) continue;

        const double start_s = total;
        std::size_t begin = 0;
        if (!vertices.empty() && distance(vertices.back(), chain.front()) < 1e-6) begin = 1;
        for (std::size_t i = begin; i < chain.size(); ++i) vertices.push_back(chain[i]);

        segments.push_back(PathSegmentInfo{
            .element = chain_elements[index],
            .start_s_mm = start_s,
            .length_mm = length,
            .reversed = chain_reversed[index],
        });
        total += length;
    }

    if (vertices.size() < 2) return table;
    table.total_length_mm = total;

    std::vector<double> cumulative(vertices.size(), 0.0);
    for (std::size_t i = 1; i < vertices.size(); ++i) {
        cumulative[i] = cumulative[i - 1] + distance(vertices[i - 1], vertices[i]);
    }
    const double actual_total = cumulative.back();
    if (actual_total <= 1e-9) return table;
    table.total_length_mm = actual_total;

    const double step = table.step_mm;
    const int intervals = std::max(1, static_cast<int>(std::ceil(actual_total / step)));
    std::vector<double> targets;
    targets.reserve(static_cast<std::size_t>(intervals) + 1);
    for (int i = 0; i <= intervals; ++i) {
        targets.push_back(std::min(actual_total, static_cast<double>(i) * step));
    }
    if (targets.back() < actual_total - 1e-9) targets.push_back(actual_total);

    const auto element_at = [&segments](double s) -> const PathSegmentInfo* {
        for (const PathSegmentInfo& segment : segments) {
            if (s >= segment.start_s_mm - 1e-6 &&
                s <= segment.start_s_mm + segment.length_mm + 1e-6) {
                return &segment;
            }
        }
        return segments.empty() ? nullptr : &segments.back();
    };

    std::vector<double> headings;
    headings.reserve(targets.size());

    std::size_t segment_index = 0;
    for (const double target : targets) {
        while (segment_index + 2 < vertices.size() && cumulative[segment_index + 1] < target) {
            ++segment_index;
        }
        const double span = cumulative[segment_index + 1] - cumulative[segment_index];
        const double local = span > 1e-12 ? (target - cumulative[segment_index]) / span : 0.0;
        const Vec2 position =
            lerp(vertices[segment_index], vertices[segment_index + 1], std::clamp(local, 0.0, 1.0));

        Vec2 direction = vertices[segment_index + 1] - vertices[segment_index];
        if (direction.length_squared() <= 1e-12 && segment_index > 0) {
            direction = vertices[segment_index] - vertices[segment_index - 1];
        }
        const double heading = std::atan2(direction.y, direction.x);
        headings.push_back(heading);

        const PathSegmentInfo* segment = element_at(target);
        PathSample sample;
        sample.s_mm = target;
        sample.position = position;
        sample.heading_deg = heading * 180.0 / kPi;
        sample.element = segment != nullptr ? segment->element : kInvalidId;
        const auto width = widths.find(sample.element);
        sample.width_mm = width != widths.end() ? width->second : 19.0;
        table.samples.push_back(sample);
    }

    table.segments = std::move(segments);

    for (std::size_t i = 0; i < table.samples.size(); ++i) {
        double ds = 0.0;
        double dh = 0.0;
        if (i + 1 < headings.size()) {
            ds = table.samples[i + 1].s_mm - table.samples[i].s_mm;
            dh = wrap_radians(headings[i + 1] - headings[i]);
        } else if (i > 0) {
            ds = table.samples[i].s_mm - table.samples[i - 1].s_mm;
            dh = wrap_radians(headings[i] - headings[i - 1]);
        }
        table.samples[i].curvature = ds > 1e-9 ? dh / ds : 0.0;
    }

    if (!table.closed && table.samples.size() >= 2 &&
        distance(table.samples.front().position, table.samples.back().position) < 1e-3) {
        table.closed = true;
    }

    return table;
}

std::vector<std::string> path_table_columns() {
    return {"indice", "s_mm", "x_mm", "y_mm", "rumo_deg", "curvatura_1_mm",
            "largura_mm", "elemento"};
}

std::string path_table_csv(const PathTable& table) {
    std::ostringstream stream;
    const std::vector<std::string> columns = path_table_columns();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) stream << ';';
        stream << columns[i];
    }
    stream << '\n';

    for (std::size_t i = 0; i < table.samples.size(); ++i) {
        const PathSample& sample = table.samples[i];
        stream << (i + 1) << ';' << format_number(sample.s_mm) << ';'
               << format_number(sample.position.x) << ';'
               << format_number(sample.position.y) << ';'
               << format_number(sample.heading_deg) << ';'
               << format_number(sample.curvature) << ';'
               << format_number(sample.width_mm) << ';' << sample.element << '\n';
    }
    return stream.str();
}

} // namespace pf
