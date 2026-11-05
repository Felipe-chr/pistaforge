#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/scene/scene.hpp"

namespace pf {

/// Pilha de desfazer/refazer baseada em instantâneos da cena.
///
/// Cada entrada guarda o estado *anterior* a uma alteração. Desfazer restaura
/// esse estado e move a entrada para a pilha de refazer. A revisão do documento
/// continua crescendo e `next_id` nunca regride, então IDs não são reutilizados.
class History {
public:
    explicit History(std::size_t limit = 128);

    /// Esquece todo o histórico (documento novo ou recém-carregado).
    void reset() noexcept;

    /// Registra o estado anterior a uma alteração.
    ///
    /// Quando `merge_key` não é vazio e é igual ao da última entrada, a entrada
    /// existente é mantida (o instantâneo mais antigo vence). É usado para
    /// agrupar arrastos contínuos em um único passo de desfazer.
    void record(const Scene& before, std::string label, std::string merge_key = {});

    /// Restaura o estado anterior. Devolve falso quando não há o que desfazer.
    bool undo(Scene& scene, std::string* label_out = nullptr);

    /// Refaz a última alteração desfeita.
    bool redo(Scene& scene, std::string* label_out = nullptr);

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }

    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_.size(); }

    /// Rótulo da próxima ação de desfazer, ou string vazia.
    [[nodiscard]] const std::string& undo_label() const noexcept;

    /// Rótulo da próxima ação de refazer, ou string vazia.
    [[nodiscard]] const std::string& redo_label() const noexcept;

    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

private:
    struct Entry {
        Scene scene;
        std::string label;
        std::string merge_key;
    };

    static void restore(Scene& target, const Scene& snapshot);

    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    std::size_t limit_{128};
};

} // namespace pf
