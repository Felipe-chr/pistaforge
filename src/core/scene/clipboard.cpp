#include "core/scene/clipboard.hpp"

#include <algorithm>
#include <unordered_map>

namespace pf {

ElementClipboard copy_elements(const Scene& scene, const std::vector<ElementId>& ids) {
    ElementClipboard clipboard;

    for (const ElementId id : ids) {
        const Element* element = scene.find_element(id);
        if (element == nullptr) continue;
        const bool duplicated = std::any_of(
            clipboard.elements.begin(), clipboard.elements.end(),
            [id](const Element& copied) { return copied.id == id; });
        if (!duplicated) clipboard.elements.push_back(*element);
    }

    const auto in_clipboard = [&clipboard](ElementId id) {
        return std::any_of(clipboard.elements.begin(), clipboard.elements.end(),
                           [id](const Element& element) { return element.id == id; });
    };

    for (const Connection& connection : scene.connections) {
        if (in_clipboard(connection.a.element) && in_clipboard(connection.b.element)) {
            clipboard.connections.emplace_back(connection.a, connection.b);
        }
    }

    return clipboard;
}

std::vector<ElementId> paste_elements(Scene& scene, const ElementClipboard& clipboard,
                                      Vec2 offset) {
    std::vector<ElementId> created;
    created.reserve(clipboard.elements.size());
    if (clipboard.elements.empty()) return created;

    std::unordered_map<ElementId, ElementId> remap;
    remap.reserve(clipboard.elements.size());

    for (const Element& source : clipboard.elements) {
        Element copy = source;
        copy.id = scene.allocate_id();
        translate(copy, offset);
        remap.emplace(source.id, copy.id);
        created.push_back(copy.id);
        scene.elements.push_back(std::move(copy));
    }

    for (const auto& [a, b] : clipboard.connections) {
        const auto from = remap.find(a.element);
        const auto to = remap.find(b.element);
        if (from == remap.end() || to == remap.end()) continue;
        scene.add_connection(PortRef{from->second, a.port}, PortRef{to->second, b.port});
    }

    return created;
}

} // namespace pf
