#include "app/bridge/api.hpp"

#include <string>

#include "app/bridge/api_doc.hpp"
#include "app/bridge/dispatcher.hpp"
#include "app/bridge/state.hpp"

#ifndef PF_VERSION
#define PF_VERSION "0.0.0"
#endif

namespace pf {
namespace {

std::string compiler_string() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER / 100) + "." + std::to_string(_MSC_VER % 100);
#elif defined(__clang__)
    return "Clang " + std::string{__clang_version__};
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "unknown";
#endif
}

} // namespace

void register_api(Dispatcher& dispatcher, AppState& state) {
    using Json = Dispatcher::Json;

    dispatcher.add("app.ping", [](const Json& params) {
        return Json{
            {"pong", true},
            {"echo", params},
        };
    });

    dispatcher.add("app.info", [&state](const Json&) {
        return Json{
            {"name", "PistaForge"},
            {"version", PF_VERSION},
            {"build", std::string{__DATE__} + " " + std::string{__TIME__}},
            {"cxx", "C++23"},
            {"compiler", compiler_string()},
            {"track_schema", 1},
            {"sim_schema", 1},
            {"arena",
             {
                 {"width", state.scene.arena.width_mm},
                 {"height", state.scene.arena.height_mm},
                 {"margin", state.scene.arena.margin_mm},
             }},
            {"capabilities",
             Json::array({"bridge", "geometry", "scene", "io"})},
        };
    });

    dispatcher.add("app.echo", [](const Json& params) { return params; });

    register_doc_api(dispatcher, state);
}

} // namespace pf
