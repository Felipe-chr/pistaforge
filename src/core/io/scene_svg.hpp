#pragma once

#include <string>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf::io {

/// Opções de exportação/impressão em SVG.
///
/// As unidades do desenho são milímetros e o `viewBox` usa a mesma escala, de
/// modo que `width="190mm"` imprime exatamente 190 mm. O mosaico divide a arena
/// em páginas com sobreposição para colagem.
struct SvgOptions {
    /// Quando verdadeiro, gera um SVG por página em vez de um único arquivo.
    bool tiled{false};
    /// Área útil da página em mm (padrão: A4 retrato com margens de 10 mm).
    double page_width_mm{190.0};
    double page_height_mm{277.0};
    /// Sobreposição entre páginas vizinhas, em mm.
    double overlap_mm{10.0};
    /// Grade de referência a cada 100 mm dentro da arena.
    bool grid{true};
    /// Régua de calibração e rótulo de página.
    bool calibration{true};
    /// Inclui as guias mesmo que a camada esteja oculta no editor.
    bool guides{false};
};

/// Uma página do mosaico.
struct SvgPage {
    std::string content;
    int row{0};
    int column{0};
    int rows{1};
    int columns{1};
    double origin_x_mm{0.0};
    double origin_y_mm{0.0};
    double width_mm{0.0};
    double height_mm{0.0};
};

/// Desenho único da arena inteira, em escala 1:1 (1 unidade = 1 mm).
[[nodiscard]] std::string to_svg(const Scene& scene, const SvgOptions& options = {});

/// Mosaico de páginas 1:1 com sobreposição, marcas de corte e calibração.
[[nodiscard]] std::vector<SvgPage> to_svg_pages(const Scene& scene,
                                                const SvgOptions& options = {});

} // namespace pf::io
