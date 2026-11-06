#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/scene/arena.hpp"
#include "core/scene/element.hpp"
#include "core/scene/ids.hpp"

namespace pf {

struct Connection {
    ConnectionId id{};
    PortRef a;
    PortRef b;
};

enum class MarkerType { StartFinish, Start, Finish, DirectionArrow, Text };

[[nodiscard]] std::string_view to_string(MarkerType type) noexcept;
[[nodiscard]] std::optional<MarkerType> marker_type_from_string(std::string_view text) noexcept;

struct Marker {
    MarkerId id{};
    MarkerType type{MarkerType::StartFinish};
    Vec2 position{};
    double angle{0.0}; ///< radianos, sentido horário na tela
    std::string text;
};

struct Guide {
    GuideId id{};
    Vec2 a{};
    Vec2 b{};
};

/// Visibilidade das camadas (estado de exibição, persistido no documento).
struct LayerVisibility {
    bool track{true};
    bool markers{true};
    bool annotations{true};
    bool guides{false};
};

/// Documento de uma arena com um traçado principal.
struct Scene {
    int schema_version{1};
    Arena arena;
    LayerVisibility layers;
    std::vector<Element> elements;
    std::vector<Connection> connections;
    std::vector<Marker> markers;
    std::vector<Guide> guides;
    std::uint64_t next_id{1};
    std::uint64_t revision{0};

    [[nodiscard]] std::uint64_t allocate_id() { return next_id++; }

    [[nodiscard]] Element* find_element(ElementId id);
    [[nodiscard]] const Element* find_element(ElementId id) const;

    [[nodiscard]] Connection* find_connection(ConnectionId id);
    [[nodiscard]] const Connection* find_connection(ConnectionId id) const;

    Element& add_element(Shape shape, double width_mm = 19.0, LayerId layer = LayerId::Track);

    /// Remove o elemento e qualquer conexão que o referencie.
    bool remove_element(ElementId id);

    Connection& add_connection(PortRef a, PortRef b);

    bool remove_connection(ConnectionId id);

    /// Incrementa a revisão do documento.
    void touch() noexcept { ++revision; }

    [[nodiscard]] BBox bounds() const;
};

/// Visibilidade efetiva de uma camada do documento.
[[nodiscard]] bool layer_visible(const Scene& scene, LayerId layer) noexcept;

} // namespace pf
