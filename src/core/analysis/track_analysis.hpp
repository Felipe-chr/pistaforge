#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf {

/// Métricas agregadas do traçado, recalculadas a cada alteração.
struct TrackMetrics {
    double total_length_mm{0.0};
    double straight_length_mm{0.0};
    double curve_length_mm{0.0};
    int straight_count{0};
    int curve_count{0};
    int crossing_count{0};
    int special_count{0};
    int element_count{0};
    int connection_count{0};
    int consecutive_curve_links{0};
    int sharp_turn_count{0};
    int tight_curve_count{0};

    /// Pontuação de dificuldade de 0 a 100.
    double difficulty{0.0};
    std::string difficulty_label{"Muito Fácil"};

    /// Estimativa de percurso em segundos e velocidade média recomendada (m/s).
    double estimated_time_s{0.0};
    double recommended_speed_mps{0.0};
};

/// Pontos onde o robô pode perder a linha ou exigir cuidado extra.
enum class CriticalKind { TightCurve, AbruptTurn, LineLoss, SensorCritical, HighDifficulty };

[[nodiscard]] std::string_view to_string(CriticalKind kind) noexcept;

struct CriticalPoint {
    CriticalKind kind{CriticalKind::TightCurve};
    Vec2 position{};
    ElementId element{kInvalidId};
    /// Severidade normalizada de 0 a 1 (para intensidade visual).
    double severity{0.0};
    /// Parâmetro ao longo do elemento (0..1), quando aplicável.
    double t{0.0};
    /// Ângulo de virada em graus (para `AbruptTurn`).
    double angle_deg{0.0};
    /// Raio característico em mm (para curvas).
    double radius_mm{0.0};
};

struct TrackAnalysis {
    TrackMetrics metrics;
    std::vector<CriticalPoint> critical_points;
};

/// Analisa a pista: métricas, dificuldade e pontos críticos.
[[nodiscard]] TrackAnalysis analyze_track(const Scene& scene);

/// Faixa textual da dificuldade para uma pontuação de 0 a 100.
[[nodiscard]] std::string_view difficulty_label(double score) noexcept;

/// Dificuldade local de um elemento (0 = fácil, 5 = extremo), sem estado manual.
[[nodiscard]] int element_auto_difficulty(const Element& element);

} // namespace pf
