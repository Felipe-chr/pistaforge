#pragma once

#include <string>

#include "core/scene/scene.hpp"

namespace pf::io {

/// Opções da malha 3D exportada para o Blender (formato OBJ + MTL).
struct ObjOptions {
    /// Fator aplicado às coordenadas em mm. O padrão converte para metros.
    double scale{0.001};
    /// Espessura da faixa em mm.
    double height_mm{2.0};
    /// Inclui o piso da arena.
    bool floor{true};
    /// Inclui a linha central como objeto separado.
    bool centerline{true};
    /// Passo de amostragem da malha em mm.
    double step_mm{20.0};
    /// Nome do arquivo de materiais referenciado pelo OBJ.
    std::string mtl_name{"pista.mtl"};
};

struct ObjBundle {
    std::string obj;
    std::string mtl;
    int vertices{0};
    int faces{0};
};

/// Gera a pista em 3D (faixa extrudada) pronta para importar no Blender.
///
/// O eixo vertical da cena vira +Y no OBJ (up do Blender) e o plano da pista
/// fica em XZ, com a escala padrão em metros.
[[nodiscard]] ObjBundle to_obj(const Scene& scene, const ObjOptions& options = {});

} // namespace pf::io
