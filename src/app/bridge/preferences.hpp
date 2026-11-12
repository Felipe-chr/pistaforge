#pragma once

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

/// Quantidade máxima de arquivos recentes guardados.
inline constexpr std::size_t kMaxRecentFiles = 10;

/// Preferências do usuário, persistidas entre execuções.
struct Preferences {
    /// Última pasta usada nos diálogos de arquivo.
    std::filesystem::path last_dir;
    /// Documentos abertos/salvos recentemente, do mais novo para o mais antigo.
    std::vector<std::filesystem::path> recent_files;
};

/// Caminho padrão: `%LOCALAPPDATA%\PistaForge\settings.json`.
/// Devolve vazio quando a pasta de dados locais não está disponível.
[[nodiscard]] std::filesystem::path default_preferences_path();

/// Converte caminhos de/para UTF-8 (formato usado no JSON e na API).
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);

[[nodiscard]] nlohmann::json preferences_to_json(const Preferences& preferences);
[[nodiscard]] Preferences preferences_from_json(const nlohmann::json& document);

/// Lê o arquivo; em qualquer falha devolve preferências vazias.
[[nodiscard]] Preferences load_preferences(const std::filesystem::path& path);

/// Grava as preferências (cria a pasta se necessário). Devolve `false` em falha.
bool save_preferences(const std::filesystem::path& path, const Preferences& preferences);

/// Move o arquivo para o topo da lista, sem duplicar e respeitando o limite.
void remember_recent(Preferences& preferences, const std::filesystem::path& file);

}  // namespace pf
