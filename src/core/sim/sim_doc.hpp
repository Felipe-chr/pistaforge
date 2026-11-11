#pragma once

#include <string>
#include <string_view>

#include "core/scene/scene.hpp"
#include "core/sim/path_table.hpp"

namespace pf::sim {

/// Identificador estável do documento do simulador e versão do schema.
inline constexpr std::string_view kSimFormat = "pistaforge.sim";
inline constexpr int kSimSchema = 1;

/// Opções do documento `.pfsim.json`.
struct SimDocOptions {
    /// Passo de amostragem da linha central, em mm.
    double step_mm{50.0};
    /// Tolerância de achatamento das curvas, em mm.
    double tolerance_mm{0.25};
    /// Versão do gerador gravada no cabeçalho (vazia = omitida).
    std::string generator_version{};
};

/// Documento do simulador já serializado, com a tabela que o originou.
struct SimDocument {
    PathTable table;
    std::string json;
};

/// Gera o documento `.pfsim.json` (schema 1).
///
/// O documento descreve a arena, a linha central amostrada na ordem de
/// percurso, os trechos por elemento, as métricas do traçado e os pontos
/// críticos. Coordenadas em mm, rumo em graus (0° = +X, positivo = horário na
/// tela) e curvatura assinada em 1/mm — a mesma convenção de `PathTable`.
[[nodiscard]] SimDocument build_sim_document(const Scene& scene,
                                             const SimDocOptions& options = {});

/// Atalho para `build_sim_document(...).json`.
[[nodiscard]] std::string to_sim_json(const Scene& scene,
                                      const SimDocOptions& options = {});

} // namespace pf::sim
