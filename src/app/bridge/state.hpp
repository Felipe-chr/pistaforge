#pragma once

#include <filesystem>

#include "core/geometry/index.hpp"
#include "core/scene/history.hpp"
#include "core/scene/scene.hpp"

namespace pf {

/// Estado de edição do aplicativo (documento atual + índice espacial + histórico).
struct AppState {
    Scene scene;
    SpatialIndex index{100.0};
    std::filesystem::path path;
    History history;

    void rebuild_index() { index.rebuild(scene); }
};

} // namespace pf
