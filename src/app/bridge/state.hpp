#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "core/geometry/index.hpp"
#include "core/scene/history.hpp"
#include "core/scene/scene.hpp"

namespace pf {

/// Pedido de diálogo nativo de arquivo.
struct FileDialogRequest {
    bool save{false};
    std::string title;
    std::filesystem::path initial_path;
    std::string default_name;
    /// Filtro exibido no diálogo (padrão: documentos PistaForge).
    std::string filter_label;
    /// Padrão do filtro no formato Win32 (ex.: `*.svg`).
    std::string filter_pattern;
    /// Extensão aplicada quando o usuário não digita uma.
    std::string default_extension;
};

/// Mostra o diálogo nativo e devolve o caminho escolhido (ou vazio se cancelado).
using FileDialogHandler =
    std::function<std::optional<std::filesystem::path>(const FileDialogRequest&)>;

/// Estado de edição do aplicativo (documento atual + índice espacial + histórico).
struct AppState {
    Scene scene;
    SpatialIndex index{100.0};
    std::filesystem::path path;
    std::filesystem::path last_dir;
    History history;
    FileDialogHandler file_dialog;

    void rebuild_index() { index.rebuild(scene); }
};

} // namespace pf
