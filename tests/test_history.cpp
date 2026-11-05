#include <cstdint>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "core/scene/element.hpp"
#include "core/scene/history.hpp"
#include "core/scene/scene.hpp"

using pf::ElementId;
using pf::History;
using pf::LineSegment;
using pf::Scene;
using pf::Vec2;

namespace {

ElementId add_line(Scene& scene, Vec2 a, Vec2 b) {
    const ElementId id = scene.add_element(LineSegment{a, b}).id;
    scene.touch();
    return id;
}

} // namespace

TEST_CASE("Histórico: desfazer e refazer restauram a cena") {
    Scene scene;
    History history;

    const ElementId first = add_line(scene, {0.0, 0.0}, {100.0, 0.0});
    const Scene before = scene;
    const ElementId second = add_line(scene, {100.0, 0.0}, {200.0, 0.0});
    history.record(before, "Inserir linha");

    REQUIRE(history.can_undo());
    CHECK_FALSE(history.can_redo());
    CHECK(history.undo_depth() == 1);
    CHECK(history.undo_label() == "Inserir linha");
    CHECK(history.redo_label().empty());

    const std::uint64_t revision_before_undo = scene.revision;
    std::string label;
    REQUIRE(history.undo(scene, &label));
    CHECK(label == "Inserir linha");
    CHECK(scene.elements.size() == 1);
    CHECK(scene.find_element(first) != nullptr);
    CHECK(scene.find_element(second) == nullptr);
    CHECK(scene.revision == revision_before_undo + 1);
    CHECK(scene.next_id > second); // IDs nunca são reutilizados
    CHECK_FALSE(history.can_undo());
    CHECK(history.can_redo());
    CHECK(history.redo_label() == "Inserir linha");

    REQUIRE(history.redo(scene, &label));
    CHECK(label == "Inserir linha");
    CHECK(scene.elements.size() == 2);
    CHECK(scene.find_element(second) != nullptr);
    CHECK(scene.revision > revision_before_undo + 1);
    CHECK(history.can_undo());
    CHECK_FALSE(history.can_redo());
}

TEST_CASE("Histórico: desfazer não libera identificadores") {
    Scene scene;
    History history;

    const ElementId first = add_line(scene, {0.0, 0.0}, {10.0, 0.0});
    const Scene before = scene;
    const ElementId second = add_line(scene, {10.0, 0.0}, {20.0, 0.0});
    history.record(before, "Inserir linha");

    REQUIRE(history.undo(scene));
    CHECK(scene.find_element(second) == nullptr);

    // Novo elemento depois do desfazer deve receber um id inédito.
    const ElementId third = add_line(scene, {20.0, 0.0}, {30.0, 0.0});
    CHECK(third > second);
    CHECK(third != first);
    CHECK(third != second);
}

TEST_CASE("Histórico: merge agrupa arrastos contínuos") {
    Scene scene;
    History history;

    const ElementId id = add_line(scene, {0.0, 0.0}, {100.0, 0.0});
    const Scene before_drag = scene;

    for (int step = 0; step < 3; ++step) {
        const Scene before = scene;
        pf::translate(*scene.find_element(id), Vec2{10.0, 0.0});
        history.record(before, "Mover elemento", "drag:1");
    }

    CHECK(history.undo_depth() == 1);
    CHECK(history.undo_label() == "Mover elemento");

    REQUIRE(history.undo(scene));
    const auto* line = std::get_if<LineSegment>(&scene.find_element(id)->shape);
    REQUIRE(line != nullptr);
    CHECK(line->a == Vec2{0.0, 0.0});
    CHECK(line->b == Vec2{100.0, 0.0});
}

TEST_CASE("Histórico: chaves de merge diferentes geram passos separados") {
    Scene scene;
    History history;

    const ElementId id = add_line(scene, {0.0, 0.0}, {100.0, 0.0});

    for (const char* key : {"drag:1", "drag:1", "drag:2", "drag:2"}) {
        const Scene before = scene;
        pf::translate(*scene.find_element(id), Vec2{1.0, 0.0});
        history.record(before, "Mover elemento", key);
    }

    CHECK(history.undo_depth() == 2);
}

TEST_CASE("Histórico: registrar após desfazer limpa o refazer") {
    Scene scene;
    History history;

    add_line(scene, {0.0, 0.0}, {10.0, 0.0});
    const Scene before = scene;
    add_line(scene, {10.0, 0.0}, {20.0, 0.0});
    history.record(before, "Inserir linha");

    REQUIRE(history.undo(scene));
    REQUIRE(history.can_redo());

    const Scene before_new = scene;
    add_line(scene, {50.0, 0.0}, {60.0, 0.0});
    history.record(before_new, "Inserir linha");

    CHECK_FALSE(history.can_redo());
    CHECK(history.redo_label().empty());
    CHECK(history.undo_depth() == 1);
}

TEST_CASE("Histórico: limite descarta os passos mais antigos") {
    Scene scene;
    History history(2);

    const ElementId id = add_line(scene, {0.0, 0.0}, {10.0, 0.0});
    for (const char* label : {"primeira", "segunda", "terceira"}) {
        const Scene before = scene;
        pf::translate(*scene.find_element(id), Vec2{1.0, 0.0});
        history.record(before, label);
    }

    CHECK(history.limit() == 2);
    CHECK(history.undo_depth() == 2);
    CHECK(history.undo_label() == "terceira");

    REQUIRE(history.undo(scene));
    CHECK(history.undo_label() == "segunda");
    REQUIRE(history.undo(scene));
    CHECK_FALSE(history.can_undo());
}

TEST_CASE("Histórico: reset esquece tudo") {
    Scene scene;
    History history;

    add_line(scene, {0.0, 0.0}, {10.0, 0.0});
    const Scene before = scene;
    add_line(scene, {10.0, 0.0}, {20.0, 0.0});
    history.record(before, "Inserir linha");
    REQUIRE(history.undo(scene));

    history.reset();
    CHECK_FALSE(history.can_undo());
    CHECK_FALSE(history.can_redo());
    CHECK(history.undo_depth() == 0);
    CHECK(history.redo_depth() == 0);
    CHECK(history.undo_label().empty());
    CHECK(history.redo_label().empty());
}

TEST_CASE("Histórico: desfazer sem passos devolve falso") {
    Scene scene;
    History history;

    CHECK_FALSE(history.undo(scene));
    CHECK_FALSE(history.redo(scene));
}
