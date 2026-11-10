#include <algorithm>
#include <cmath>
#include <numbers>
#include <ostream>
#include <string>

#include <doctest/doctest.h>

#include "core/analysis/track_analysis.hpp"

using pf::ArcSegment;
using pf::CriticalKind;
using pf::LineSegment;
using pf::ModuleInstance;
using pf::Scene;
using pf::TrackAnalysis;
using pf::Vec2;

namespace {

bool has_kind(const TrackAnalysis& analysis, CriticalKind kind) {
    return std::any_of(analysis.critical_points.begin(), analysis.critical_points.end(),
                       [kind](const pf::CriticalPoint& point) { return point.kind == kind; });
}

ArcSegment arc_at(Vec2 center, double radius, double start_deg, double sweep_deg) {
    return ArcSegment{
        .center = center,
        .radius = radius,
        .start_angle = start_deg * std::numbers::pi / 180.0,
        .sweep_angle = sweep_deg * std::numbers::pi / 180.0,
    };
}

} // namespace

TEST_CASE("Análise: cena vazia") {
    const Scene scene;
    const TrackAnalysis analysis = pf::analyze_track(scene);

    CHECK(analysis.metrics.total_length_mm == doctest::Approx(0.0));
    CHECK(analysis.metrics.element_count == 0);
    CHECK(analysis.metrics.difficulty == doctest::Approx(0.0));
    CHECK(analysis.metrics.difficulty_label == "Muito Fácil");
    CHECK(analysis.critical_points.empty());
}

TEST_CASE("Análise: reta conta comprimento e tempo") {
    Scene scene;
    scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}});

    const TrackAnalysis analysis = pf::analyze_track(scene);
    CHECK(analysis.metrics.total_length_mm == doctest::Approx(1000.0));
    CHECK(analysis.metrics.straight_count == 1);
    CHECK(analysis.metrics.curve_count == 0);
    CHECK(analysis.metrics.straight_length_mm == doctest::Approx(1000.0));
    CHECK(analysis.metrics.recommended_speed_mps == doctest::Approx(1.4));
    CHECK(analysis.metrics.estimated_time_s == doctest::Approx(1.0 / 1.4));
}

TEST_CASE("Análise: curva fechada gera pontos críticos") {
    Scene scene;
    scene.add_element(arc_at({0.0, 0.0}, 40.0, 0.0, 90.0));

    const TrackAnalysis analysis = pf::analyze_track(scene);
    CHECK(analysis.metrics.curve_count == 1);
    CHECK(analysis.metrics.tight_curve_count == 1);
    CHECK(has_kind(analysis, CriticalKind::TightCurve));
    CHECK(has_kind(analysis, CriticalKind::SensorCritical));
    CHECK(has_kind(analysis, CriticalKind::LineLoss));
    CHECK(analysis.metrics.difficulty > 0.0);
}

TEST_CASE("Análise: cruzamento e elementos especiais") {
    Scene scene;
    scene.add_element(ModuleInstance{.template_id = "cross.90"});
    scene.add_element(ModuleInstance{.template_id = "curve.90"});

    const TrackAnalysis analysis = pf::analyze_track(scene);
    CHECK(analysis.metrics.crossing_count == 1);
    CHECK(analysis.metrics.curve_count == 1);
    CHECK(analysis.metrics.element_count == 2);
}

TEST_CASE("Análise: curvas consecutivas e virada brusca") {
    Scene scene;
    const auto first = scene.add_element(LineSegment{{0.0, 0.0}, {100.0, 0.0}});
    const auto second = scene.add_element(LineSegment{{100.0, 0.0}, {100.0, 100.0}});
    scene.add_connection(pf::PortRef{first.id, "end"}, pf::PortRef{second.id, "start"});

    const TrackAnalysis analysis = pf::analyze_track(scene);
    CHECK(analysis.metrics.connection_count == 1);
    CHECK(analysis.metrics.sharp_turn_count == 1);
    CHECK(has_kind(analysis, CriticalKind::AbruptTurn));
    CHECK(has_kind(analysis, CriticalKind::LineLoss));
}

TEST_CASE("Análise: dificuldade automática por elemento") {
    Scene scene;
    const auto line = scene.add_element(LineSegment{{0.0, 0.0}, {100.0, 0.0}});
    CHECK(pf::element_auto_difficulty(line) == 0);

    const auto wide = scene.add_element(arc_at({0.0, 0.0}, 500.0, 0.0, 90.0));
    CHECK(pf::element_auto_difficulty(wide) == 1);

    const auto medium = scene.add_element(arc_at({0.0, 0.0}, 120.0, 0.0, 90.0));
    CHECK(pf::element_auto_difficulty(medium) == 3);

    const auto tight = scene.add_element(arc_at({0.0, 0.0}, 40.0, 0.0, 90.0));
    CHECK(pf::element_auto_difficulty(tight) == 5);
}

TEST_CASE("Análise: rótulos de dificuldade") {
    CHECK(pf::difficulty_label(0.0) == "Muito Fácil");
    CHECK(pf::difficulty_label(14.9) == "Muito Fácil");
    CHECK(pf::difficulty_label(20.0) == "Fácil");
    CHECK(pf::difficulty_label(40.0) == "Médio");
    CHECK(pf::difficulty_label(60.0) == "Difícil");
    CHECK(pf::difficulty_label(90.0) == "Extremo");
}
