#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/scene/clipboard.hpp"
#include "core/scene/scene.hpp"

using pf::ElementClipboard;
using pf::ElementId;
using pf::LineSegment;
using pf::PortRef;
using pf::Scene;
using pf::Vec2;

namespace {

struct Chain {
    Scene scene;
    ElementId first{};
    ElementId second{};

    Chain() {
        first = scene.add_element(LineSegment{{0.0, 0.0}, {100.0, 0.0}}).id;
        second = scene.add_element(LineSegment{{100.0, 0.0}, {200.0, 0.0}}).id;
        scene.add_connection(PortRef{first, "end"}, PortRef{second, "start"});
    }
};

} // namespace

TEST_CASE("Clipboard: copia elementos e conexões internas") {
    const Chain chain;
    const ElementClipboard clipboard = pf::copy_elements(chain.scene, {chain.first, chain.second});

    REQUIRE(clipboard.elements.size() == 2);
    CHECK(clipboard.elements[0].id == chain.first);
    CHECK(clipboard.elements[1].id == chain.second);
    REQUIRE(clipboard.connections.size() == 1);
    CHECK(clipboard.connections[0].first.element == chain.first);
    CHECK(clipboard.connections[0].second.element == chain.second);
}

TEST_CASE("Clipboard: ignora ids inexistentes e repetidos") {
    const Chain chain;
    const ElementClipboard clipboard =
        pf::copy_elements(chain.scene, {chain.first, 9999, chain.first});

    REQUIRE(clipboard.elements.size() == 1);
    CHECK(clipboard.elements[0].id == chain.first);
    CHECK(clipboard.connections.empty());
}

TEST_CASE("Clipboard: cola com ids novos, deslocamento e conexões remapeadas") {
    Chain chain;
    const ElementClipboard clipboard = pf::copy_elements(chain.scene, {chain.first, chain.second});

    const std::vector<ElementId> pasted =
        pf::paste_elements(chain.scene, clipboard, Vec2{20.0, 30.0});

    REQUIRE(pasted.size() == 2);
    CHECK(pasted[0] != chain.first);
    CHECK(pasted[1] != chain.second);
    CHECK(chain.scene.elements.size() == 4);

    const pf::Element* copy = chain.scene.find_element(pasted[0]);
    REQUIRE(copy != nullptr);
    const auto* line = std::get_if<LineSegment>(&copy->shape);
    REQUIRE(line != nullptr);
    CHECK(line->a == (Vec2{20.0, 30.0}));
    CHECK(line->b == (Vec2{120.0, 30.0}));

    REQUIRE(chain.scene.connections.size() == 2);
    const pf::Connection& connection = chain.scene.connections.back();
    CHECK(connection.a.element == pasted[0]);
    CHECK(connection.a.port == "end");
    CHECK(connection.b.element == pasted[1]);
    CHECK(connection.b.port == "start");

    // A cena original permanece intacta.
    const pf::Element* original = chain.scene.find_element(chain.first);
    REQUIRE(original != nullptr);
    const auto* original_line = std::get_if<LineSegment>(&original->shape);
    REQUIRE(original_line != nullptr);
    CHECK(original_line->a == (Vec2{0.0, 0.0}));
}

TEST_CASE("Clipboard: colar vazio não altera a cena") {
    Scene scene;
    const std::vector<ElementId> pasted =
        pf::paste_elements(scene, ElementClipboard{}, Vec2{10.0, 10.0});

    CHECK(pasted.empty());
    CHECK(scene.elements.empty());
    CHECK(scene.connections.empty());
}

TEST_CASE("Clipboard: remove vários elementos de uma vez") {
    Chain chain;
    const std::size_t removed = chain.scene.remove_elements({chain.first, chain.second, 9999});

    CHECK(removed == 2);
    CHECK(chain.scene.elements.empty());
    CHECK(chain.scene.connections.empty());
}
