#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

#include "app/bridge/preferences.hpp"
#include "core/geometry/index.hpp"
#include "core/scene/clipboard.hpp"
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
    Preferences preferences;
    std::filesystem::path preferences_path;
    History history;
    FileDialogHandler file_dialog;
    /// Elementos copiados por `doc.copyElements`, prontos para `doc.pasteElements`.
    ElementClipboard clipboard;

    void rebuild_index() { index.rebuild(scene); }

    /// Atualiza a última pasta usada e persiste as preferências.
    void remember_directory(const std::filesystem::path& dir) {
        if (dir.empty()) return;
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(dir, error);
        preferences.last_dir = (error ? dir : absolute).lexically_normal();
        save_preferences(preferences_path, preferences);
    }

    /// Registra o documento como recente e persiste as preferências.
    void remember_file(const std::filesystem::path& file) {
        if (file.empty()) return;
        remember_recent(preferences, file);
        if (file.has_parent_path()) {
            std::error_code error;
            const std::filesystem::path absolute =
                std::filesystem::absolute(file.parent_path(), error);
            preferences.last_dir =
                (error ? file.parent_path() : absolute).lexically_normal();
        }
        save_preferences(preferences_path, preferences);
    }
};

} // namespace pf
