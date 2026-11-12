#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "app/bridge/api.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/bridge/preferences.hpp"
#include "app/bridge/state.hpp"

using nlohmann::json;
using pf::kMaxRecentFiles;
using pf::Preferences;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("pistaforge_prefs_" + std::to_string(++counter));
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path, error);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::vector<std::string> utf8_paths(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const std::filesystem::path& path : paths) out.push_back(pf::path_to_utf8(path));
    return out;
}

struct Api {
    pf::AppState state;
    pf::Dispatcher dispatcher;

    explicit Api(const std::filesystem::path& preferences_path) {
        state.preferences_path = preferences_path;
        pf::register_api(dispatcher, state);
    }

    json call(const json& request) const {
        return json::parse(dispatcher.handle(request.dump()));
    }
};

}  // namespace

TEST_CASE("Preferências: JSON preserva pasta e recentes em UTF-8") {
    Preferences preferences;
    preferences.last_dir = pf::path_from_utf8("C:/Pistas/Ação");
    preferences.recent_files = {
        pf::path_from_utf8("C:/Pistas/pista-ção.pftrack"),
        pf::path_from_utf8("C:/Pistas/outra.pftrack"),
    };

    const json document = pf::preferences_to_json(preferences);
    CHECK(document.at("schema").get<int>() == 1);
    CHECK(document.at("last_dir").get<std::string>() == "C:/Pistas/Ação");
    REQUIRE(document.at("recent_files").size() == 2);

    const Preferences restored = pf::preferences_from_json(document);
    CHECK(pf::path_to_utf8(restored.last_dir) == "C:/Pistas/Ação");
    CHECK(utf8_paths(restored.recent_files) ==
          std::vector<std::string>{"C:/Pistas/pista-ção.pftrack", "C:/Pistas/outra.pftrack"});
}

TEST_CASE("Preferências: leitura tolera documento inválido e campos estranhos") {
    CHECK(pf::preferences_from_json(json::array()).recent_files.empty());
    CHECK(pf::preferences_from_json(json{{"recent_files", "não é lista"}}).recent_files.empty());

    const Preferences mixed = pf::preferences_from_json(json{
        {"last_dir", 42},
        {"recent_files", json::array({1, "C:/ok.pftrack", nullptr, ""})},
    });
    CHECK(mixed.last_dir.empty());
    REQUIRE(mixed.recent_files.size() == 1);
    CHECK(pf::path_to_utf8(mixed.recent_files.front()) == "C:/ok.pftrack");
}

TEST_CASE("Preferências: recentes sem duplicar, do mais novo para o mais antigo") {
    Preferences preferences;
    for (int i = 0; i < 12; ++i) {
        pf::remember_recent(preferences, std::filesystem::path{"C:/Pistas"} /
                                             ("pista" + std::to_string(i) + ".pftrack"));
    }

    REQUIRE(preferences.recent_files.size() == kMaxRecentFiles);
    CHECK(pf::path_to_utf8(preferences.recent_files.front().filename()) == "pista11.pftrack");
    CHECK(pf::path_to_utf8(preferences.recent_files.back().filename()) == "pista2.pftrack");

    pf::remember_recent(preferences, std::filesystem::path{"c:/pistas/PISTA5.PFTRACK"});
    REQUIRE(preferences.recent_files.size() == kMaxRecentFiles);
    CHECK(pf::path_to_utf8(preferences.recent_files.front().filename()) == "PISTA5.PFTRACK");

    const std::vector<std::string> paths = utf8_paths(preferences.recent_files);
    const auto occurrences = std::count_if(paths.begin(), paths.end(), [](const std::string& path) {
        std::string name = pf::path_to_utf8(pf::path_from_utf8(path).filename());
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name == "pista5.pftrack";
    });
    CHECK(occurrences == 1);
}

TEST_CASE("Preferências: grava e lê o arquivo de configuração") {
    const TempDir directory;
    const std::filesystem::path path = directory.path / "settings.json";

    Preferences preferences;
    preferences.last_dir = directory.path / "Pistas";
    preferences.recent_files = {directory.path / "a.pftrack", directory.path / "b.pftrack"};

    REQUIRE(pf::save_preferences(path, preferences));
    REQUIRE(std::filesystem::exists(path));

    const Preferences loaded = pf::load_preferences(path);
    CHECK(pf::path_to_utf8(loaded.last_dir) == pf::path_to_utf8(preferences.last_dir));
    CHECK(utf8_paths(loaded.recent_files) == utf8_paths(preferences.recent_files));

    CHECK_FALSE(pf::save_preferences({}, preferences));
    CHECK(pf::load_preferences(directory.path / "ausente.json").recent_files.empty());
}

TEST_CASE("API: app.preferences expõe e limpa a lista persistida") {
    const TempDir directory;
    const std::filesystem::path settings = directory.path / "settings.json";
    Api api{settings};

    pf::remember_recent(api.state.preferences, directory.path / "um.pftrack");
    pf::remember_recent(api.state.preferences, directory.path / "dois.pftrack");
    REQUIRE(pf::save_preferences(settings, api.state.preferences));

    const json listed = api.call(json{{"id", 1}, {"method", "app.preferences"}});
    REQUIRE(listed.at("ok").get<bool>());
    CHECK(listed.at("result").at("path").get<std::string>() == pf::path_to_utf8(settings));
    CHECK(listed.at("result").at("max_recent").get<std::size_t>() == kMaxRecentFiles);
    REQUIRE(listed.at("result").at("recent_files").size() == 2);
    CHECK(listed.at("result").at("recent_files").at(0).get<std::string>() ==
          pf::path_to_utf8(directory.path / "dois.pftrack"));

    const json cleared = api.call(json{{"id", 2}, {"method", "app.clearRecent"}});
    REQUIRE(cleared.at("ok").get<bool>());
    CHECK(cleared.at("result").at("recent_files").empty());
    CHECK(api.state.preferences.recent_files.empty());
    CHECK(pf::load_preferences(settings).recent_files.empty());
}

TEST_CASE("API: diálogo de arquivo começa na última pasta usada") {
    const TempDir directory;
    Api api{directory.path / "settings.json"};
    api.state.preferences.last_dir = directory.path;

    pf::FileDialogRequest captured;
    api.state.file_dialog =
        [&captured](const pf::FileDialogRequest& request) -> std::optional<std::filesystem::path> {
        captured = request;
        return std::nullopt;
    };

    const json opened = api.call(json{{"id", 1}, {"method", "doc.openDialog"}});
    REQUIRE(opened.at("ok").get<bool>());
    CHECK(opened.at("result").at("cancelled").get<bool>());
    CHECK(pf::path_to_utf8(captured.initial_path) == pf::path_to_utf8(directory.path));
}

TEST_CASE("API: caminhos com acentos são gravados e lidos em UTF-8") {
    const TempDir directory;
    Api api{directory.path / "settings.json"};

    const std::filesystem::path folder = directory.path / pf::path_from_utf8("Pistas/Ação");
    const std::filesystem::path document = folder / pf::path_from_utf8("pista-ção.pftrack");
    const std::filesystem::path exported = folder / pf::path_from_utf8("tabela-ção.csv");

    const json saved = api.call(json{
        {"id", 1},
        {"method", "doc.save"},
        {"params", {{"path", pf::path_to_utf8(document)}}},
    });
    const std::string expected = pf::path_to_utf8(std::filesystem::absolute(document));
    REQUIRE(saved.at("ok").get<bool>());
    CHECK(saved.at("result").at("path").get<std::string>() == expected);
    CHECK(std::filesystem::exists(document));

    const json state = api.call(json{{"id", 2}, {"method", "doc.state"}});
    CHECK(state.at("result").at("path").get<std::string>() == expected);

    REQUIRE(api.state.preferences.recent_files.size() == 1);
    CHECK(pf::path_to_utf8(api.state.preferences.recent_files.front()) == expected);

    const json preferences = api.call(json{{"id", 3}, {"method", "app.preferences"}});
    REQUIRE(preferences.at("result").at("recent_files").size() == 1);
    CHECK(preferences.at("result").at("recent_files").at(0).get<std::string>() == expected);

    const json table = api.call(json{
        {"id", 4},
        {"method", "doc.exportTable"},
        {"params", {{"path", pf::path_to_utf8(exported)}, {"step", 100}}},
    });
    REQUIRE(table.at("ok").get<bool>());
    CHECK(table.at("result").at("files").at(0).get<std::string>() == pf::path_to_utf8(exported));
    CHECK(std::filesystem::exists(exported));
}

TEST_CASE("API: diálogos devolvem e reaproveitam caminhos acentuados em UTF-8") {
    const TempDir directory;
    Api api{directory.path / "settings.json"};
    const std::filesystem::path folder = directory.path / pf::path_from_utf8("Pistas/Ação");
    const std::filesystem::path document = folder / pf::path_from_utf8("pista-ção.pftrack");
    const std::filesystem::path exported = folder / pf::path_from_utf8("tabela-ção.csv");

    api.state.file_dialog = [&document](const pf::FileDialogRequest&)
        -> std::optional<std::filesystem::path> { return document; };

    const json opened = api.call(json{{"id", 1}, {"method", "doc.openDialog"}});
    REQUIRE(opened.at("ok").get<bool>());
    const std::string document_utf8 = opened.at("result").at("path").get<std::string>();
    CHECK(document_utf8 == pf::path_to_utf8(document));

    const json saved_dialog = api.call(json{{"id", 2}, {"method", "doc.saveDialog"}});
    REQUIRE(saved_dialog.at("ok").get<bool>());
    CHECK(saved_dialog.at("result").at("path").get<std::string>() == document_utf8);

    const json saved = api.call(json{
        {"id", 3},
        {"method", "doc.save"},
        {"params", {{"path", document_utf8}}},
    });
    REQUIRE(saved.at("ok").get<bool>());
    CHECK(std::filesystem::exists(document));

    api.state.file_dialog = [&exported](const pf::FileDialogRequest&)
        -> std::optional<std::filesystem::path> { return exported; };
    const json table = api.call(json{
        {"id", 4},
        {"method", "doc.exportTableDialog"},
        {"params", {{"step", 100}}},
    });
    REQUIRE(table.at("ok").get<bool>());
    CHECK(table.at("result").at("files").at(0).get<std::string>() ==
          pf::path_to_utf8(exported));
    CHECK(std::filesystem::exists(exported));
}

TEST_CASE("API: salvar e abrir registram o documento nos recentes") {
    const TempDir directory;
    const std::filesystem::path settings = directory.path / "settings.json";
    const std::filesystem::path document = directory.path / "pista.pftrack";
    Api api{settings};

    const json saved = api.call(json{
        {"id", 1},
        {"method", "doc.save"},
        {"params", {{"path", pf::path_to_utf8(document)}}},
    });
    REQUIRE(saved.at("ok").get<bool>());
    REQUIRE(api.state.preferences.recent_files.size() == 1);
    CHECK(pf::path_to_utf8(api.state.preferences.recent_files.front()) ==
          pf::path_to_utf8(std::filesystem::absolute(document)));
    CHECK(pf::path_to_utf8(api.state.preferences.last_dir) ==
          pf::path_to_utf8(std::filesystem::absolute(directory.path)));
    CHECK(pf::load_preferences(settings).recent_files.size() == 1);

    api.call(json{{"id", 2}, {"method", "app.clearRecent"}});
    REQUIRE(api.state.preferences.recent_files.empty());

    const json loaded = api.call(json{
        {"id", 3},
        {"method", "doc.load"},
        {"params", {{"path", pf::path_to_utf8(document)}}},
    });
    REQUIRE(loaded.at("ok").get<bool>());
    REQUIRE(api.state.preferences.recent_files.size() == 1);
    CHECK(pf::path_to_utf8(api.state.preferences.recent_files.front()) ==
          pf::path_to_utf8(std::filesystem::absolute(document)));
}
