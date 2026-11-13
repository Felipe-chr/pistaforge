#include "core/scene/scene.hpp"

#include <algorithm>

namespace pf {

std::string_view to_string(MarkerType type) noexcept {
    switch (type) {
        case MarkerType::StartFinish: return "start_finish";
        case MarkerType::Start: return "start";
        case MarkerType::Finish: return "finish";
        case MarkerType::DirectionArrow: return "direction_arrow";
        case MarkerType::Text: return "text";
    }
    return "start_finish";
}

std::optional<MarkerType> marker_type_from_string(std::string_view text) noexcept {
    if (text == "start_finish") return MarkerType::StartFinish;
    if (text == "start") return MarkerType::Start;
    if (text == "finish") return MarkerType::Finish;
    if (text == "direction_arrow") return MarkerType::DirectionArrow;
    if (text == "text") return MarkerType::Text;
    return std::nullopt;
}

Element* Scene::find_element(ElementId id) {
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [id](const Element& element) { return element.id == id; });
    return it != elements.end() ? &*it : nullptr;
}

const Element* Scene::find_element(ElementId id) const {
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [id](const Element& element) { return element.id == id; });
    return it != elements.end() ? &*it : nullptr;
}

Connection* Scene::find_connection(ConnectionId id) {
    const auto it = std::find_if(connections.begin(), connections.end(),
                                 [id](const Connection& connection) { return connection.id == id; });
    return it != connections.end() ? &*it : nullptr;
}

const Connection* Scene::find_connection(ConnectionId id) const {
    const auto it = std::find_if(connections.begin(), connections.end(),
                                 [id](const Connection& connection) { return connection.id == id; });
    return it != connections.end() ? &*it : nullptr;
}

Element& Scene::add_element(Shape shape, double width_mm, LayerId layer) {
    Element element{
        .id = allocate_id(),
        .shape = std::move(shape),
        .width_mm = width_mm,
        .layer = layer,
    };
    elements.push_back(std::move(element));
    return elements.back();
}

bool Scene::remove_element(ElementId id) {
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [id](const Element& element) { return element.id == id; });
    if (it == elements.end()) return false;

    elements.erase(it);
    std::erase_if(connections, [id](const Connection& connection) {
        return connection.a.element == id || connection.b.element == id;
    });
    return true;
}

std::size_t Scene::remove_elements(const std::vector<ElementId>& ids) {
    std::size_t removed = 0;
    for (const ElementId id : ids) {
        if (remove_element(id)) ++removed;
    }
    return removed;
}

Connection& Scene::add_connection(PortRef a, PortRef b) {
    Connection connection{
        .id = allocate_id(),
        .a = std::move(a),
        .b = std::move(b),
    };
    connections.push_back(std::move(connection));
    return connections.back();
}

bool Scene::remove_connection(ConnectionId id) {
    const auto it = std::find_if(connections.begin(), connections.end(),
                                 [id](const Connection& connection) { return connection.id == id; });
    if (it == connections.end()) return false;
    connections.erase(it);
    return true;
}

BBox Scene::bounds() const {
    BBox box = arena.bounds();
    for (const Element& element : elements) {
        BBox element_box = shape_bbox(element.shape);
        const double half_width = element.width_mm * 0.5;
        element_box.min.x -= half_width;
        element_box.min.y -= half_width;
        element_box.max.x += half_width;
        element_box.max.y += half_width;
        box.expand(element_box);
    }
    return box;
}

bool layer_visible(const Scene& scene, LayerId layer) noexcept {
    switch (layer) {
        case LayerId::Track: return scene.layers.track;
        case LayerId::Markers: return scene.layers.markers;
        case LayerId::Annotations: return scene.layers.annotations;
        case LayerId::Guides: return scene.layers.guides;
    }
    return true;
}

} // namespace pf
