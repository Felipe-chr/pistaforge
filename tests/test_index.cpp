#include <vector>

#include <doctest/doctest.h>

#include "core/geometry/index.hpp"
#include "core/scene/scene.hpp"

using pf::BBox;
using pf::ElementId;
using pf::Scene;
using pf::SpatialIndex;
using pf::Vec2;

TEST_CASE("Índice: vazio") {
    const SpatialIndex index{100.0};

    CHECK(index.empty());
    CHECK(index.query_point({10.0, 10.0}, 5.0).empty());
    CHECK(index.query(BBox{{0.0, 0.0}, {100.0, 100.0}}).empty());
}

TEST_CASE("Índice: consulta por ponto e por caixa") {
    Scene scene;
    const ElementId first = scene.add_element(pf::LineSegment{{0.0, 0.0}, {100.0, 0.0}}).id;
    const ElementId second =
        scene.add_element(pf::LineSegment{{1000.0, 1000.0}, {1100.0, 1000.0}}).id;

    SpatialIndex index{100.0};
    index.rebuild(scene);
    CHECK_FALSE(index.empty());

    const std::vector<ElementId> near_first = index.query_point({50.0, 5.0}, 20.0);
    CHECK(near_first == std::vector<ElementId>{first});

    const std::vector<ElementId> near_second = index.query_point({1050.0, 1000.0}, 10.0);
    CHECK(near_second == std::vector<ElementId>{second});

    const std::vector<ElementId> both =
        index.query(BBox{{0.0, 0.0}, {1100.0, 1100.0}});
    CHECK(both == std::vector<ElementId>{first, second});

    CHECK(index.query_point({500.0, 500.0}, 10.0).empty());
}

TEST_CASE("Índice: reconstrução reflete remoções") {
    Scene scene;
    const ElementId id = scene.add_element(pf::LineSegment{{0.0, 0.0}, {100.0, 0.0}}).id;

    SpatialIndex index{100.0};
    index.rebuild(scene);
    CHECK(index.query_point({50.0, 0.0}, 1.0) == std::vector<ElementId>{id});

    REQUIRE(scene.remove_element(id));
    index.rebuild(scene);
    CHECK(index.empty());
    CHECK(index.query_point({50.0, 0.0}, 1.0).empty());
}

TEST_CASE("Índice: elementos grandes vão para a lista global") {
    SpatialIndex index{1.0};
    index.insert(42, BBox{{0.0, 0.0}, {100.0, 100.0}});

    CHECK_FALSE(index.empty());
    CHECK(index.query_point({1000.0, 1000.0}, 0.0) == std::vector<ElementId>{42});
}

TEST_CASE("Índice: resultados não se repetem") {
    SpatialIndex index{10.0};
    index.insert(7, BBox{{0.0, 0.0}, {25.0, 25.0}});

    const std::vector<ElementId> result =
        index.query(BBox{{-10.0, -10.0}, {40.0, 40.0}});
    CHECK(result == std::vector<ElementId>{7});
}
