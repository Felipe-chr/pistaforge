#pragma once

#include <string>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf::io {

/// Opções de impressão em PDF.
///
/// O PDF usa milímetros convertidos para pontos (1 mm = 72/25,4 pt) e desenha
/// vetores em escala 1:1. O mosaico divide a arena em páginas com sobreposição
/// para colagem, igual à exportação SVG.
struct PdfOptions {
    bool tiled{false};
    /// Área útil da página em mm (padrão: A4 retrato com margens de 10 mm).
    double page_width_mm{190.0};
    double page_height_mm{277.0};
    double overlap_mm{10.0};
    /// Margem impressa ao redor da área útil.
    double margin_mm{10.0};
    bool grid{true};
    bool calibration{true};
    bool guides{false};
    /// Desenha somente a linha central (mais adequado para desenhar no chão).
    bool centerline{false};
    /// Marca pontos numerados com coordenadas ao longo da linha central.
    bool floor_marks{true};
    double mark_step_mm{500.0};
    /// Largura da linha central no modo `centerline`, em mm.
    double path_width_mm{1.0};
};

/// Conteúdo de uma página do PDF (já no sistema de coordenadas do PDF).
struct PdfPage {
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

/// Páginas vetoriais do desenho (sem o envelope PDF).
[[nodiscard]] std::vector<PdfPage> to_pdf_pages(const Scene& scene,
                                                const PdfOptions& options = {});

/// Documento PDF completo, com uma ou várias páginas.
[[nodiscard]] std::string to_pdf(const Scene& scene, const PdfOptions& options = {});

} // namespace pf::io
