#pragma once

#include <filesystem>
#include <optional>

#include "app/bridge/state.hpp"

namespace pf {

/// Diálogo nativo do Windows para abrir ou salvar um documento `.pftrack`.
///
/// Devolve o caminho escolhido ou `std::nullopt` quando o usuário cancela.
/// Deve ser chamado fora da thread da interface; o diálogo roda o próprio
/// laço modal na thread chamadora.
std::optional<std::filesystem::path> show_file_dialog(const FileDialogRequest& request);

} // namespace pf
