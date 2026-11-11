#pragma once

#include <string>

#include "core/scene/scene.hpp"
#include "core/sim/path_table.hpp"

namespace pf::io {

struct RobotPathOptions {
    /// Distância entre pontos de pintura, em mm.
    double step_mm{100.0};
    /// Largura nominal da faixa; 0 usa a largura de cada elemento.
    double brush_width_mm{0.0};
    /// Velocidade base sugerida, em mm/s.
    double speed_mm_s{250.0};
};

/// Lista de pontos e instruções para um robô autônomo pintar a pista.
struct RobotPath {
    PathTable table;
    std::string points_csv;
    std::string instructions;
    int point_count{0};
    double total_length_mm{0.0};
    /// Sentido predominante do percurso (positivo = horário na tela).
    bool clockwise{true};
};

[[nodiscard]] RobotPath build_robot_path(const Scene& scene,
                                         const RobotPathOptions& options = {});

} // namespace pf::io
