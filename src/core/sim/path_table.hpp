#pragma once

#include <string>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf {

/// Uma amostra da linha central do traçado, em coordenadas de cena (mm).
///
/// A tabela é a representação usada pelo simulador, pela exportação para robô,
/// pela impressão com pontos de marcação e pela malha 3D. O rumo segue o
/// sentido de percurso e usa a convenção da cena: 0° aponta para +X e ângulos
/// positivos giram no sentido horário na tela (Y para baixo).
struct PathSample {
    double s_mm{0.0};
    Vec2 position{};
    double heading_deg{0.0};
    /// Curvatura assinada em 1/mm (positiva = curva para a direita na tela).
    double curvature{0.0};
    double width_mm{19.0};
    ElementId element{kInvalidId};
};

/// Trecho contínuo percorrido de um elemento.
struct PathSegmentInfo {
    ElementId element{kInvalidId};
    double start_s_mm{0.0};
    double length_mm{0.0};
    bool reversed{false};
};

struct PathTableOptions {
    /// Passo de amostragem em mm. O último ponto sempre cai no fim da pista.
    double step_mm{50.0};
    /// Tolerância de achatamento das curvas em mm.
    double tolerance_mm{0.25};
};

/// Matriz de coordenadas da pista (linha central amostrada e ordenada).
struct PathTable {
    std::vector<PathSample> samples;
    std::vector<PathSegmentInfo> segments;
    double total_length_mm{0.0};
    double step_mm{50.0};
    /// Verdadeiro quando o último trecho reconecta ao início.
    bool closed{false};
    /// Verdadeiro quando o percurso foi ordenado pelas conexões do documento.
    bool ordered{false};
    ElementId start_element{kInvalidId};
};

/// Constrói a tabela de coordenadas percorrendo o traçado na ordem das conexões.
///
/// Quando não há conexões, os elementos da camada Traçado são percorridos na
/// ordem do documento. Elementos ocultos e de outras camadas são ignorados.
[[nodiscard]] PathTable build_path_table(const Scene& scene,
                                         const PathTableOptions& options = {});

/// Cabeçalho da matriz de coordenadas em CSV (separador `;`).
[[nodiscard]] std::string path_table_csv(const PathTable& table);

/// Nomes das colunas da matriz, na mesma ordem de `path_table_csv`.
[[nodiscard]] std::vector<std::string> path_table_columns();

} // namespace pf
