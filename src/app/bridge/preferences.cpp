#include "app/bridge/preferences.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pf {
namespace {

using nlohmann::json;

/// Chave de comparação de caminhos: normalizada e sem diferença de caixa.
std::string path_key(const std::filesystem::path& path) {
    std::string key = path_to_utf8(path.lexically_normal());
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return key;
}

std::filesystem::path stored_path(const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(file, error);
    return (error ? file : absolute).lexically_normal();
}

}  // namespace

std::filesystem::path default_preferences_path() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path{buffer} / "PistaForge" / "settings.json";
    }
#endif
    return {};
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{value.begin(), value.end()};
}

std::filesystem::path path_from_utf8(std::string_view text) {
    std::u8string value;
    value.assign(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return std::filesystem::path{value};
}

json preferences_to_json(const Preferences& preferences) {
    json recent = json::array();
    for (const std::filesystem::path& file : preferences.recent_files) {
        recent.push_back(path_to_utf8(file));
    }

    json document{{"schema", 1}, {"recent_files", std::move(recent)}};
    if (!preferences.last_dir.empty()) {
        document["last_dir"] = path_to_utf8(preferences.last_dir);
    }
    return document;
}

Preferences preferences_from_json(const json& document) {
    Preferences preferences;
    if (!document.is_object())
        return preferences;

    const auto last_dir = document.find("last_dir");
    if (last_dir != document.end() && last_dir->is_string()) {
        preferences.last_dir = path_from_utf8(last_dir->get<std::string>());
    }

    const auto recent = document.find("recent_files");
    if (recent != document.end() && recent->is_array()) {
        for (const json& entry : *recent) {
            if (!entry.is_string())
                continue;
            const std::filesystem::path file = path_from_utf8(entry.get<std::string>());
            if (file.empty())
                continue;
            if (preferences.recent_files.size() >= kMaxRecentFiles)
                break;
            preferences.recent_files.push_back(file);
        }
    }
    return preferences;
}

Preferences load_preferences(const std::filesystem::path& path) {
    if (path.empty())
        return {};

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    json document;
    try {
        document = json::parse(file);
    } catch (const json::exception&) {
        return {};
    }
    return preferences_from_json(document);
}

bool save_preferences(const std::filesystem::path& path, const Preferences& preferences) {
    if (path.empty())
        return false;

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    file << preferences_to_json(preferences).dump(2);
    return file.good();
}

void remember_recent(Preferences& preferences, const std::filesystem::path& file) {
    if (file.empty())
        return;

    const std::filesystem::path stored = stored_path(file);
    const std::string key = path_key(stored);

    preferences.recent_files.erase(
        std::remove_if(
            preferences.recent_files.begin(), preferences.recent_files.end(),
            [&key](const std::filesystem::path& entry) { return path_key(entry) == key; }),
        preferences.recent_files.end());

    preferences.recent_files.insert(preferences.recent_files.begin(), stored);
    if (preferences.recent_files.size() > kMaxRecentFiles) {
        preferences.recent_files.resize(kMaxRecentFiles);
    }
}

}  // namespace pf
