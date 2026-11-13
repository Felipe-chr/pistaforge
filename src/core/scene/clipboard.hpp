#pragma once

#include <utility>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf {

/// Conjunto de elementos copiado, com as conexões internas do conjunto.
///
/// O conteúdo é independente da cena de origem: as cópias guardam forma,
/// largura, camada, rótulo e dificuldade, mas não os ids antigos. Por isso o
/// mesmo clipboard pode ser colado no documento atual ou em outro documento.
struct ElementClipboard {
    std::vector<Element> elements;
    std::vector<std::pair<PortRef, PortRef>> connections;

    [[nodiscard]] bool empty() const noexcept { return elements.empty(); }
};

/// Copia os elementos indicados (ids desconhecidos são ignorados) e as conexões
/// cujas duas pontas estão no conjunto.
[[nodiscard]] ElementClipboard copy_elements(const Scene& scene,
                                             const std::vector<ElementId>& ids);

/// Cola o conteúdo do clipboard deslocado por `offset`, atribuindo ids novos e
/// recriando as conexões internas. Devolve os ids criados, na ordem de
/// `clipboard.elements`.
[[nodiscard]] std::vector<ElementId> paste_elements(Scene& scene,
                                                    const ElementClipboard& clipboard,
                                                    Vec2 offset);

} // namespace pf
