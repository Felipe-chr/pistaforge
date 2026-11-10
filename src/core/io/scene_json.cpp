#include "core/io/scene_json.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>

namespace pf::io {
namespace {

using nlohmann::json;

constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kDegToRad = std::numbers::pi / 180.0;

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

json vec2_to_json(Vec2 point) { return json::array({point.x, point.y}); }

Vec2 vec2_from_json(const json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
        throw ParseError(std::string{field} + " deve ser um par [x, y].");
    }
    return Vec2{value[0].get<double>(), value[1].get<double>()};
}

json element_to_json(const Element& element) {
    json out{
        {"id", element.id},
        {"layer", std::string{to_string(element.layer)}},
        {"width", element.width_mm},
    };
    if (element.hidden) out["hidden"] = true;
    if (element.locked) out["locked"] = true;
    if (!element.label.empty()) out["label"] = element.label;
    if (element.difficulty > 0) out["difficulty"] = element.difficulty;

    std::visit(
        Overloaded{
            [&out](const LineSegment& line) {
                out["type"] = "line";
                out["a"] = vec2_to_json(line.a);
                out["b"] = vec2_to_json(line.b);
            },
            [&out](const ArcSegment& arc) {
                out["type"] = "arc";
                out["center"] = vec2_to_json(arc.center);
                out["radius"] = arc.radius;
                out["start"] = arc.start_angle * kRadToDeg;
                out["sweep"] = arc.sweep_angle * kRadToDeg;
            },
            [&out](const CubicBezier& bezier) {
                out["type"] = "cubic";
                out["p0"] = vec2_to_json(bezier.p0);
                out["p1"] = vec2_to_json(bezier.p1);
                out["p2"] = vec2_to_json(bezier.p2);
                out["p3"] = vec2_to_json(bezier.p3);
            },
            [&out](const ModuleInstance& module) {
                out["type"] = "module";
                out["template"] = module.template_id;
                out["version"] = module.template_version;
                out["params"] = module.params;
                out["transform"] = {
                    {"tx", module.transform.translation.x},
                    {"ty", module.transform.translation.y},
                    {"rot", module.transform.rotation * kRadToDeg},
                    {"mirrored", module.transform.mirrored},
                };
            },
        },
        element.shape);

    return out;
}

Element element_from_json(const json& value) {
    Element element;
    element.id = value.at("id").get<ElementId>();
    element.width_mm = value.value("width", 19.0);
    element.layer =
        layer_from_string(value.value("layer", std::string{"track"})).value_or(LayerId::Track);
    element.hidden = value.value("hidden", false);
    element.locked = value.value("locked", false);
    element.label = value.value("label", std::string{});
    element.difficulty = value.value("difficulty", 0);

    const std::string type = value.at("type").get<std::string>();
    if (type == "line") {
        element.shape = LineSegment{
            .a = vec2_from_json(value.at("a"), "a"),
            .b = vec2_from_json(value.at("b"), "b"),
        };
    } else if (type == "arc") {
        const json& center = value.at("center");
        element.shape = ArcSegment{
            .center = vec2_from_json(center, "center"),
            .radius = value.at("radius").get<double>(),
            .start_angle =
                value.value("start", value.value("start_deg", 0.0)) * kDegToRad,
            .sweep_angle =
                value.value("sweep", value.value("sweep_deg", 0.0)) * kDegToRad,
        };
    } else if (type == "cubic") {
        element.shape = CubicBezier{
            .p0 = vec2_from_json(value.at("p0"), "p0"),
            .p1 = vec2_from_json(value.at("p1"), "p1"),
            .p2 = vec2_from_json(value.at("p2"), "p2"),
            .p3 = vec2_from_json(value.at("p3"), "p3"),
        };
    } else if (type == "module") {
        ModuleInstance module;
        module.template_id = value.at("template").get<std::string>();
        module.template_version = value.value("version", 1);
        if (value.contains("params")) {
            for (const auto& [key, entry] : value.at("params").items()) {
                module.params[key] = entry.get<double>();
            }
        }
        if (value.contains("transform")) {
            const json& transform = value.at("transform");
            module.transform.translation = {
                transform.value("tx", 0.0),
                transform.value("ty", 0.0),
            };
            module.transform.rotation =
                transform.value("rot", transform.value("rot_deg", 0.0)) * kDegToRad;
            module.transform.mirrored = transform.value("mirrored", false);
        }
        element.shape = std::move(module);
    } else {
        throw ParseError("Tipo de elemento desconhecido: " + type + ".");
    }

    return element;
}

PortRef port_from_json(const json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 2) {
        throw ParseError(std::string{field} + " deve ser [elemento, porta].");
    }
    const bool valid_id = value[0].is_number_unsigned() || value[0].is_number_integer();
    if (!valid_id || !value[1].is_string()) {
        throw ParseError(std::string{field} + " deve ser [elemento, porta].");
    }
    return PortRef{value[0].get<ElementId>(), value[1].get<std::string>()};
}

json port_to_json(const PortRef& port) { return json::array({port.element, port.port}); }

} // namespace

json to_json(const Scene& scene) {
    json document{
        {"schema", scene.schema_version},
        {"arena",
         {
             {"name", scene.arena.name},
             {"width", scene.arena.width_mm},
             {"height", scene.arena.height_mm},
             {"margin", scene.arena.margin_mm},
             {"show_dimensions", scene.arena.show_dimensions},
         }},
        {"revision", scene.revision},
        {"next_id", scene.next_id},
        {"layers",
         {
             {"track", scene.layers.track},
             {"markers", scene.layers.markers},
             {"annotations", scene.layers.annotations},
             {"guides", scene.layers.guides},
         }},
        {"elements", json::array()},
        {"connections", json::array()},
        {"markers", json::array()},
        {"guides", json::array()},
    };

    for (const Element& element : scene.elements) {
        document["elements"].push_back(element_to_json(element));
    }

    for (const Connection& connection : scene.connections) {
        document["connections"].push_back({
            {"id", connection.id},
            {"a", port_to_json(connection.a)},
            {"b", port_to_json(connection.b)},
        });
    }

    for (const Marker& marker : scene.markers) {
        json entry{
            {"id", marker.id},
            {"type", std::string{to_string(marker.type)}},
            {"x", marker.position.x},
            {"y", marker.position.y},
            {"angle", marker.angle * kRadToDeg},
        };
        if (!marker.text.empty()) entry["text"] = marker.text;
        document["markers"].push_back(std::move(entry));
    }

    for (const Guide& guide : scene.guides) {
        document["guides"].push_back({
            {"id", guide.id},
            {"a", vec2_to_json(guide.a)},
            {"b", vec2_to_json(guide.b)},
        });
    }

    return document;
}

Scene from_json(const json& document) {
    try {
        if (!document.is_object()) {
            throw ParseError("O documento deve ser um objeto JSON.");
        }

        Scene scene;
        scene.schema_version = document.value("schema", 1);
        scene.revision = document.value("revision", 0);

        const json& arena = document.at("arena");
        scene.arena.name = arena.value("name", std::string{"Arena"});
        scene.arena.width_mm = arena.value("width", 4000.0);
        scene.arena.height_mm = arena.value("height", 3000.0);
        scene.arena.margin_mm = arena.value("margin", 100.0);
        scene.arena.show_dimensions = arena.value("show_dimensions", true);

        if (document.contains("layers")) {
            const json& layers = document.at("layers");
            scene.layers.track = layers.value("track", true);
            scene.layers.markers = layers.value("markers", true);
            scene.layers.annotations = layers.value("annotations", true);
            scene.layers.guides = layers.value("guides", false);
        }

        if (document.contains("elements")) {
            for (const json& entry : document.at("elements")) {
                scene.elements.push_back(element_from_json(entry));
            }
        }

        if (document.contains("connections")) {
            for (const json& entry : document.at("connections")) {
                scene.connections.push_back(Connection{
                    .id = entry.at("id").get<ConnectionId>(),
                    .a = port_from_json(entry.at("a"), "a"),
                    .b = port_from_json(entry.at("b"), "b"),
                });
            }
        }

        if (document.contains("markers")) {
            for (const json& entry : document.at("markers")) {
                const auto type = marker_type_from_string(entry.value("type", std::string{}));
                if (!type.has_value()) {
                    throw ParseError("Tipo de marcador desconhecido.");
                }
                scene.markers.push_back(Marker{
                    .id = entry.at("id").get<MarkerId>(),
                    .type = *type,
                    .position = {entry.value("x", 0.0), entry.value("y", 0.0)},
                    .angle = entry.value("angle", entry.value("angle_deg", 0.0)) *
                             kDegToRad,
                    .text = entry.value("text", std::string{}),
                });
            }
        }

        if (document.contains("guides")) {
            for (const json& entry : document.at("guides")) {
                scene.guides.push_back(Guide{
                    .id = entry.at("id").get<GuideId>(),
                    .a = vec2_from_json(entry.at("a"), "a"),
                    .b = vec2_from_json(entry.at("b"), "b"),
                });
            }
        }

        std::uint64_t max_id = 0;
        for (const Element& element : scene.elements) max_id = std::max(max_id, element.id);
        for (const Connection& connection : scene.connections) {
            max_id = std::max({max_id, connection.id, connection.a.element, connection.b.element});
        }
        for (const Marker& marker : scene.markers) max_id = std::max(max_id, marker.id);
        for (const Guide& guide : scene.guides) max_id = std::max(max_id, guide.id);

        scene.next_id = std::max(document.value("next_id", std::uint64_t{1}), max_id + 1);
        return scene;
    } catch (const ParseError&) {
        throw;
    } catch (const nlohmann::json::exception& error) {
        throw ParseError(std::string{"Documento inválido: "} + error.what());
    }
}

std::string to_string(const Scene& scene) { return to_json(scene).dump(2); }

Scene from_string(std::string_view text) {
    try {
        return from_json(json::parse(text.begin(), text.end()));
    } catch (const ParseError&) {
        throw;
    } catch (const nlohmann::json::exception& error) {
        throw ParseError(std::string{"JSON inválido: "} + error.what());
    }
}

} // namespace pf::io
