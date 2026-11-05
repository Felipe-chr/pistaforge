#include "core/scene/history.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace pf {
namespace {

const std::string kEmptyLabel{};

} // namespace

History::History(std::size_t limit) : limit_(std::max<std::size_t>(limit, 1)) {}

void History::reset() noexcept {
    undo_.clear();
    redo_.clear();
}

void History::record(const Scene& before, std::string label, std::string merge_key) {
    if (!merge_key.empty() && !undo_.empty() && undo_.back().merge_key == merge_key) {
        // Arrasto em andamento: mantém o instantâneo mais antigo e só atualiza o rótulo.
        undo_.back().label = std::move(label);
        redo_.clear();
        return;
    }

    undo_.push_back(Entry{before, std::move(label), std::move(merge_key)});
    redo_.clear();

    if (undo_.size() > limit_) {
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - limit_));
    }
}

void History::restore(Scene& target, const Scene& snapshot) {
    const std::uint64_t next_revision = target.revision + 1;
    const std::uint64_t highest_id = std::max(target.next_id, snapshot.next_id);

    target = snapshot;
    target.revision = next_revision;
    // IDs nunca são reutilizados, mesmo depois de desfazer.
    target.next_id = highest_id;
}

bool History::undo(Scene& scene, std::string* label_out) {
    if (undo_.empty()) return false;

    Entry entry = std::move(undo_.back());
    undo_.pop_back();

    redo_.push_back(Entry{scene, entry.label, entry.merge_key});
    restore(scene, entry.scene);

    if (label_out != nullptr) *label_out = entry.label;
    return true;
}

bool History::redo(Scene& scene, std::string* label_out) {
    if (redo_.empty()) return false;

    Entry entry = std::move(redo_.back());
    redo_.pop_back();

    undo_.push_back(Entry{scene, entry.label, entry.merge_key});
    restore(scene, entry.scene);

    if (label_out != nullptr) *label_out = entry.label;
    return true;
}

const std::string& History::undo_label() const noexcept {
    return undo_.empty() ? kEmptyLabel : undo_.back().label;
}

const std::string& History::redo_label() const noexcept {
    return redo_.empty() ? kEmptyLabel : redo_.back().label;
}

} // namespace pf
