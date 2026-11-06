#include "app/bridge/api_doc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "app/bridge/dispatcher.hpp"
#include "app/bridge/error.hpp"
#include "app/bridge/state.hpp"
#include "core/io/scene_json.hpp"
#include "core/scene/snap.hpp"
#include "core/scene/validation.hpp"

namespace pf {
namespace {

using Json = Dispatcher::Json;
using Handler = Dispatcher::Handler;

constexpr double kDegToRad = std::numbers::pi / 180.0;

[[noreturn]] void fail(std::string code, std::string message) {
    throw ApiError(std::move(code), std::move(message));
}

const Json& require_object(const Json& params) {
    if (!params.is_object()) {
        fail("invalid_params", "Os parâmetros devem ser um objeto JSON.");
    }
    return params;
}

double positive_number(const Json& value, std::string_view label) {
    if (!value.is_number()) {
        fail("invalid_params", "Campo '" + std::string{label} + "' deve ser numérico.");
    }
    const double number = value.get<double>();
    if (!(number > 0.0) || !std::isfinite(number)) {
        fail("invalid_params", "Campo '" + std::string{label} + "' deve ser positivo.");
    }
    return number;
}

double non_negative_number(const Json& value, std::string_view label) {
    if (!value.is_number()) {
        fail("invalid_params", "Campo '" + std::string{label} + "' deve ser numérico.");
    }
    const double number = value.get<double>();
    if (number < 0.0 || !std::isfinite(number)) {
        fail("invalid_params", "Campo '" + std::string{label} + "' deve ser não negativo.");
    }
    return number;
}

const Json& field(const Json& params, std::string_view key) {
    const auto it = params.find(std::string{key});
    if (it == params.end()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' ausente.");
    }
    return *it;
}

std::string string_param(const Json& params, std::string_view key) {
    const Json& value = field(params, key);
    if (!value.is_string()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' deve ser texto.");
    }
    return value.get<std::string>();
}

double number_param(const Json& params, std::string_view key) {
    const Json& value = field(params, key);
    if (!value.is_number()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' deve ser numérico.");
    }
    return value.get<double>();
}

double number_or(const Json& params, std::string_view key, double fallback) {
    const auto it = params.find(std::string{key});
    if (it == params.end()) return fallback;
    if (!it->is_number()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' deve ser numérico.");
    }
    return it->get<double>();
}

bool bool_or(const Json& params, std::string_view key, bool fallback) {
    const auto it = params.find(std::string{key});
    if (it == params.end()) return fallback;
    if (!it->is_boolean()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' deve ser booleano.");
    }
    return it->get<bool>();
}

std::uint64_t id_param(const Json& params, std::string_view key) {
    const Json& value = field(params, key);
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        fail("invalid_params", "Campo '" + std::string{key} + "' deve ser um identificador.");
    }
    return value.get<std::uint64_t>();
}

Vec2 vec2_from_json(const Json& value, std::string_view label) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() ||
        !value[1].is_number()) {
        fail("invalid_params",
             "Campo '" + std::string{label} + "' deve ser um par [x, y].");
    }
    return Vec2{value[0].get<double>(), value[1].get<double>()};
}

Vec2 point_param(const Json& params, std::string_view key) {
    return vec2_from_json(field(params, key), key);
}

PortRef port_from_json(const Json& value, std::string_view label) {
    if (!value.is_array() || value.size() != 2) {
        fail("invalid_params",
             "Campo '" + std::string{label} + "' deve ser [elemento, porta].");
    }
    const bool valid_id = value[0].is_number_unsigned() || value[0].is_number_integer();
    if (!valid_id || !value[1].is_string()) {
        fail("invalid_params",
             "Campo '" + std::string{label} + "' deve ser [elemento, porta].");
    }
    return PortRef{value[0].get<ElementId>(), value[1].get<std::string>()};
}

PortRef port_param(const Json& params, std::string_view key) {
    return port_from_json(field(params, key), key);
}

LayerId layer_param(const Json& params, LayerId fallback = LayerId::Track) {
    const auto it = params.find("layer");
    if (it == params.end()) return fallback;
    if (!it->is_string()) {
        fail("invalid_params", "Campo 'layer' deve ser texto.");
    }
    const auto layer = layer_from_string(it->get<std::string>());
    if (!layer.has_value()) {
        fail("invalid_params", "Camada desconhecida: " + it->get<std::string>() + ".");
    }
    return *layer;
}

/// Pasta inicial dos diálogos de arquivo: última usada, senão a do documento.
std::filesystem::path dialog_directory(const AppState& state) {
    if (!state.last_dir.empty()) return state.last_dir;
    if (!state.path.empty() && state.path.has_parent_path()) {
        return state.path.parent_path();
    }
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

Json arena_json(const Arena& arena) {
    return Json{
        {"name", arena.name},
        {"width", arena.width_mm},
        {"height", arena.height_mm},
        {"margin", arena.margin_mm},
        {"show_dimensions", arena.show_dimensions},
    };
}

Json layers_json(const LayerVisibility& layers) {
    return Json{
        {"track", layers.track},
        {"markers", layers.markers},
        {"annotations", layers.annotations},
        {"guides", layers.guides},
    };
}

void apply_arena(Arena& arena, const Json& value) {
    if (!value.is_object()) {
        fail("invalid_params", "Campo 'arena' deve ser um objeto.");
    }
    Arena updated = arena;
    if (value.contains("name")) {
        if (!value.at("name").is_string()) {
            fail("invalid_params", "Campo 'arena.name' deve ser texto.");
        }
        updated.name = value.at("name").get<std::string>();
    }
    if (value.contains("width")) {
        updated.width_mm = positive_number(value.at("width"), "arena.width");
    }
    if (value.contains("height")) {
        updated.height_mm = positive_number(value.at("height"), "arena.height");
    }
    if (value.contains("margin")) {
        updated.margin_mm = non_negative_number(value.at("margin"), "arena.margin");
    }
    if (value.contains("show_dimensions")) {
        if (!value.at("show_dimensions").is_boolean()) {
            fail("invalid_params", "Campo 'arena.show_dimensions' deve ser booleano.");
        }
        updated.show_dimensions = value.at("show_dimensions").get<bool>();
    }
    arena = updated;
}

Json doc_summary(const AppState& state) {
    return Json{
        {"rev", state.scene.revision},
        {"path", state.path.empty() ? Json(nullptr) : Json(state.path.string())},
        {"arena", arena_json(state.scene.arena)},
        {"elements", state.scene.elements.size()},
        {"connections", state.scene.connections.size()},
        {"markers", state.scene.markers.size()},
        {"guides", state.scene.guides.size()},
    };
}

Json history_json(const AppState& state) {
    return Json{
        {"undo", state.history.undo_depth()},
        {"redo", state.history.redo_depth()},
        {"undo_label", state.history.undo_label()},
        {"redo_label", state.history.redo_label()},
        {"limit", state.history.limit()},
    };
}

void ensure_port_exists(const Scene& scene, const PortRef& ref) {
    const Element* element = scene.find_element(ref.element);
    if (element == nullptr) {
        fail("not_found",
             "Elemento inexistente: " + std::to_string(ref.element) + ".");
    }
    const std::vector<PortInfo> ports = ports_of(*element);
    const bool found = std::any_of(ports.begin(), ports.end(), [&ref](const PortInfo& port) {
        return port.ref.port == ref.port;
    });
    if (!found) {
        fail("invalid_port", "Porta inexistente: " + ref.port + ".");
    }
}

/// Registra um método que altera o documento.
///
/// Guarda o estado anterior no histórico, atualiza a revisão e reconstrói o
/// índice espacial. O campo `rev` da resposta é preenchido automaticamente.
/// `merge_param` nomeia um parâmetro de texto usado para agrupar alterações
/// contínuas (arrastos) em um único passo de desfazer.
void add_change(Dispatcher& dispatcher, AppState& state, std::string method,
                std::string label, Handler handler, std::string merge_param = {}) {
    dispatcher.add(
        std::move(method),
        [&state, label = std::move(label), handler = std::move(handler),
         merge_param = std::move(merge_param)](const Json& params) -> Json {
            const Scene before = state.scene;

            std::string merge_key;
            if (!merge_param.empty() && params.is_object()) {
                const auto it = params.find(merge_param);
                if (it != params.end() && it->is_string()) {
                    merge_key = it->get<std::string>();
                }
            }

            Json result = handler(params);
            state.history.record(before, label, merge_key);
            state.scene.touch();
            state.rebuild_index();

            if (result.is_object()) result["rev"] = state.scene.revision;
            return result;
        });
}

/// Alteração que não entra no histórico (ex.: visibilidade de camada).
void add_untracked_change(Dispatcher& dispatcher, AppState& state, std::string method,
                          Handler handler) {
    dispatcher.add(std::move(method),
                   [&state, handler = std::move(handler)](const Json& params) -> Json {
                       Json result = handler(params);
                       state.scene.touch();
                       state.rebuild_index();
                       if (result.is_object()) result["rev"] = state.scene.revision;
                       return result;
                   });
}

} // namespace

void register_doc_api(Dispatcher& dispatcher, AppState& state) {
    dispatcher.add("doc.new", [&state](const Json& params) {
        require_object(params);
        state.scene = Scene{};
        if (params.contains("arena")) {
            apply_arena(state.scene.arena, params.at("arena"));
        }
        state.path.clear();
        state.history.reset();
        state.scene.touch();
        state.rebuild_index();
        return doc_summary(state);
    });

    dispatcher.add("doc.state", [&state](const Json&) {
        Json document = io::to_json(state.scene);
        document["path"] = state.path.empty() ? Json(nullptr) : Json(state.path.string());
        return document;
    });

    dispatcher.add("doc.load", [&state](const Json& params) {
        require_object(params);
        const std::filesystem::path path{string_param(params, "path")};

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            fail("io_error", "Não foi possível abrir: " + path.string() + ".");
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (!file.good() && !file.eof()) {
            fail("io_error", "Falha ao ler: " + path.string() + ".");
        }

        try {
            state.scene = io::from_string(buffer.str());
        } catch (const io::ParseError& error) {
            fail("invalid_document", error.what());
        }

        state.path = std::filesystem::absolute(path);
        if (state.path.has_parent_path()) state.last_dir = state.path.parent_path();
        state.history.reset();
        state.rebuild_index();
        return doc_summary(state);
    });

    dispatcher.add("doc.openDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = false;
        request.title = "Abrir pista";
        request.initial_path = dialog_directory(state);

        const std::optional<std::filesystem::path> path = state.file_dialog(request);
        if (!path) return Json{{"cancelled", true}};
        if (path->has_parent_path()) state.last_dir = path->parent_path();
        return Json{{"path", path->string()}};
    });

    dispatcher.add("doc.saveDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Salvar pista como";
        request.initial_path = dialog_directory(state);
        request.default_name =
            state.path.empty() ? "pista.pftrack" : state.path.filename().string();

        const std::optional<std::filesystem::path> path = state.file_dialog(request);
        if (!path) return Json{{"cancelled", true}};
        if (path->has_parent_path()) state.last_dir = path->parent_path();
        return Json{{"path", path->string()}};
    });

    dispatcher.add("doc.save", [&state](const Json& params) {
        require_object(params);

        std::filesystem::path path;
        if (params.contains("path")) {
            path = string_param(params, "path");
        } else if (!state.path.empty()) {
            path = state.path;
        } else {
            fail("invalid_params", "Informe 'path' para salvar o documento.");
        }

        if (path.has_parent_path()) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            fail("io_error", "Não foi possível gravar: " + path.string() + ".");
        }
        file << io::to_string(state.scene);
        if (!file.good()) {
            fail("io_error", "Falha ao gravar: " + path.string() + ".");
        }

        state.path = std::filesystem::absolute(path);
        if (state.path.has_parent_path()) state.last_dir = state.path.parent_path();
        return Json{{"path", state.path.string()}, {"rev", state.scene.revision}};
    });

    add_change(dispatcher, state, "doc.addLine", "Inserir linha", [&state](const Json& params) {
        require_object(params);
        const Vec2 a = point_param(params, "a");
        const Vec2 b = point_param(params, "b");
        const double width = number_or(params, "width", 19.0);
        if (!(width > 0.0)) fail("invalid_params", "Campo 'width' deve ser positivo.");
        const LayerId layer = layer_param(params);

        const ElementId id = state.scene.add_element(LineSegment{a, b}, width, layer).id;
        return Json{{"id", id}};
    });

    add_change(dispatcher, state, "doc.addCubic", "Inserir Bézier", [&state](const Json& params) {
        require_object(params);
        const CubicBezier curve{
            .p0 = point_param(params, "p0"),
            .p1 = point_param(params, "p1"),
            .p2 = point_param(params, "p2"),
            .p3 = point_param(params, "p3"),
        };
        const double width = number_or(params, "width", 19.0);
        if (!(width > 0.0)) fail("invalid_params", "Campo 'width' deve ser positivo.");
        const LayerId layer = layer_param(params);

        const ElementId id = state.scene.add_element(curve, width, layer).id;
        return Json{{"id", id}};
    });

    add_change(dispatcher, state, "doc.addArc", "Inserir arco", [&state](const Json& params) {
        require_object(params);
        const ArcSegment arc{
            .center = point_param(params, "center"),
            .radius = number_param(params, "radius"),
            .start_angle =
                number_or(params, "start", number_or(params, "start_deg", 0.0)) * kDegToRad,
            .sweep_angle =
                number_or(params, "sweep", number_or(params, "sweep_deg", 90.0)) * kDegToRad,
        };
        if (!arc.is_valid()) {
            fail("invalid_params", "Arco com raio ou varredura inválidos.");
        }
        const double width = number_or(params, "width", 19.0);
        if (!(width > 0.0)) fail("invalid_params", "Campo 'width' deve ser positivo.");
        const LayerId layer = layer_param(params);

        const ElementId id = state.scene.add_element(arc, width, layer).id;
        return Json{{"id", id}};
    });

    add_change(dispatcher, state, "doc.addModule", "Inserir módulo",
               [&state](const Json& params) {
                   require_object(params);
                   const std::string template_id = string_param(params, "template");
                   const ModuleTemplate* definition = find_module_template(template_id);
                   if (definition == nullptr) {
                       fail("unknown_template", "Template desconhecido: " + template_id + ".");
                   }

                   ModuleInstance module;
                   module.template_id = template_id;
                   module.template_version = params.value("version", definition->version);

                   if (params.contains("params")) {
                       const Json& values = params.at("params");
                       if (!values.is_object()) {
                           fail("invalid_params", "Campo 'params' deve ser um objeto.");
                       }
                       for (const auto& [key, entry] : values.items()) {
                           if (!entry.is_number()) {
                               fail("invalid_params",
                                    "Parâmetro '" + key + "' deve ser numérico.");
                           }
                           module.params[key] = entry.get<double>();
                       }
                   }

                   if (params.contains("transform")) {
                       const Json& transform = params.at("transform");
                       if (!transform.is_object()) {
                           fail("invalid_params", "Campo 'transform' deve ser um objeto.");
                       }
                       module.transform.translation = {
                           transform.value("tx", 0.0),
                           transform.value("ty", 0.0),
                       };
                       module.transform.rotation =
                           transform.value("rot", transform.value("rot_deg", 0.0)) * kDegToRad;
                       module.transform.mirrored = transform.value("mirrored", false);
                   }

                   const double width = number_or(params, "width", 19.0);
                   if (!(width > 0.0)) fail("invalid_params", "Campo 'width' deve ser positivo.");
                   const LayerId layer = layer_param(params);

                   const ElementId id =
                       state.scene.add_element(std::move(module), width, layer).id;
                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.removeElement", "Remover elemento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   if (!state.scene.remove_element(id)) {
                       fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
                   }
                   return Json{{"removed", id}};
               });

    add_change(dispatcher, state, "doc.connect", "Conectar portas", [&state](const Json& params) {
        require_object(params);
        const PortRef a = port_param(params, "a");
        const PortRef b = port_param(params, "b");
        if (a == b) {
            fail("invalid_params", "Não é possível conectar uma porta a si mesma.");
        }

        ensure_port_exists(state.scene, a);
        ensure_port_exists(state.scene, b);

        for (const Connection& connection : state.scene.connections) {
            if (connection.a == a || connection.b == a || connection.a == b ||
                connection.b == b) {
                fail("port_in_use", "Uma das portas já está conectada.");
            }
        }

        const ConnectionId id = state.scene.add_connection(a, b).id;
        return Json{{"id", id}};
    });

    add_change(dispatcher, state, "doc.disconnect", "Desconectar portas",
               [&state](const Json& params) {
                   require_object(params);
                   const ConnectionId id = id_param(params, "id");
                   if (!state.scene.remove_connection(id)) {
                       fail("not_found", "Conexão inexistente: " + std::to_string(id) + ".");
                   }
                   return Json{{"removed", id}};
               });

    add_change(dispatcher, state, "doc.addMarker", "Inserir marcador",
               [&state](const Json& params) {
                   require_object(params);

                   MarkerType type = MarkerType::StartFinish;
                   if (params.contains("type")) {
                       const auto parsed = marker_type_from_string(string_param(params, "type"));
                       if (!parsed.has_value()) {
                           fail("invalid_params", "Tipo de marcador desconhecido.");
                       }
                       type = *parsed;
                   }

                   Marker marker;
                   marker.id = state.scene.allocate_id();
                   marker.type = type;
                   marker.position = {number_or(params, "x", 0.0), number_or(params, "y", 0.0)};
                   marker.angle =
                       number_or(params, "angle", number_or(params, "angle_deg", 0.0)) * kDegToRad;
                   marker.text = params.value("text", std::string{});
                   const MarkerId id = marker.id;
                   state.scene.markers.push_back(std::move(marker));

                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.moveElement", "Mover elemento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element* element = state.scene.find_element(id);
                   if (element == nullptr) {
                       fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
                   }
                   if (element->locked) {
                       fail("element_locked", "O elemento está bloqueado.");
                   }

                   const Vec2 delta = point_param(params, "delta");
                   translate(*element, delta);
                   return Json{{"id", id}, {"delta", Json::array({delta.x, delta.y})}};
               },
               "merge");

    add_change(dispatcher, state, "doc.setElement", "Alterar elemento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element* element = state.scene.find_element(id);
                   if (element == nullptr) {
                       fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
                   }

                   if (params.contains("width")) {
                       element->width_mm = positive_number(params.at("width"), "width");
                   }
                   if (params.contains("layer")) {
                       element->layer = layer_param(params);
                   }
                   if (params.contains("hidden")) {
                       element->hidden = bool_or(params, "hidden", element->hidden);
                   }
                   if (params.contains("locked")) {
                       element->locked = bool_or(params, "locked", element->locked);
                   }
                   if (params.contains("label")) {
                       element->label = string_param(params, "label");
                   }

                   return Json{{"id", id}};
               },
               "merge");

    add_change(dispatcher, state, "doc.setGeometry", "Editar geometria",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element* element = state.scene.find_element(id);
                   if (element == nullptr) {
                       fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
                   }
                   if (element->locked) {
                       fail("element_locked", "O elemento está bloqueado.");
                   }

                   if (auto* line = std::get_if<LineSegment>(&element->shape)) {
                       if (params.contains("a")) line->a = point_param(params, "a");
                       if (params.contains("b")) line->b = point_param(params, "b");
                       return Json{{"id", id}};
                   }

                   if (auto* curve = std::get_if<CubicBezier>(&element->shape)) {
                       if (params.contains("p0")) curve->p0 = point_param(params, "p0");
                       if (params.contains("p1")) curve->p1 = point_param(params, "p1");
                       if (params.contains("p2")) curve->p2 = point_param(params, "p2");
                       if (params.contains("p3")) curve->p3 = point_param(params, "p3");
                       return Json{{"id", id}};
                   }

                   if (auto* arc = std::get_if<ArcSegment>(&element->shape)) {
                       if (params.contains("center")) {
                           arc->center = point_param(params, "center");
                       }
                       if (params.contains("radius")) {
                           arc->radius = positive_number(params.at("radius"), "radius");
                       }
                       if (params.contains("start") || params.contains("start_deg")) {
                           arc->start_angle =
                               number_or(params, "start",
                                         number_or(params, "start_deg", 0.0)) *
                               kDegToRad;
                       }
                       if (params.contains("sweep") || params.contains("sweep_deg")) {
                           arc->sweep_angle =
                               number_or(params, "sweep",
                                         number_or(params, "sweep_deg", 90.0)) *
                               kDegToRad;
                       }
                       if (!arc->is_valid()) {
                           fail("invalid_params", "Arco com raio ou varredura inválidos.");
                       }
                       return Json{{"id", id}};
                   }

                   fail("unsupported", "Este elemento não permite editar a geometria.");
               },
               "merge");

    add_change(dispatcher, state, "doc.setArena", "Alterar arena",
               [&state](const Json& params) {
                   require_object(params);
                   apply_arena(state.scene.arena, params);
                   return Json{{"arena", arena_json(state.scene.arena)}};
               });

    add_untracked_change(dispatcher, state, "doc.setLayers", [&state](const Json& params) {
        require_object(params);
        const auto apply = [&params](std::string_view key, bool& target) {
            const auto it = params.find(std::string{key});
            if (it == params.end()) return;
            if (!it->is_boolean()) {
                fail("invalid_params",
                     "Campo '" + std::string{key} + "' deve ser booleano.");
            }
            target = it->get<bool>();
        };
        apply("track", state.scene.layers.track);
        apply("markers", state.scene.layers.markers);
        apply("annotations", state.scene.layers.annotations);
        apply("guides", state.scene.layers.guides);
        return Json{{"layers", layers_json(state.scene.layers)}};
    });

    dispatcher.add("doc.undo", [&state](const Json&) {
        std::string label;
        if (!state.history.undo(state.scene, &label)) {
            fail("nothing_to_undo", "Nada para desfazer.");
        }
        state.rebuild_index();
        return Json{
            {"rev", state.scene.revision},
            {"label", label},
            {"can_undo", state.history.can_undo()},
            {"can_redo", state.history.can_redo()},
            {"history", history_json(state)},
        };
    });

    dispatcher.add("doc.redo", [&state](const Json&) {
        std::string label;
        if (!state.history.redo(state.scene, &label)) {
            fail("nothing_to_redo", "Nada para refazer.");
        }
        state.rebuild_index();
        return Json{
            {"rev", state.scene.revision},
            {"label", label},
            {"can_undo", state.history.can_undo()},
            {"can_redo", state.history.can_redo()},
            {"history", history_json(state)},
        };
    });

    dispatcher.add("doc.history", [&state](const Json&) { return history_json(state); });

    dispatcher.add("scene.ports", [&state](const Json&) {
        Json ports = Json::array();
        for (const Element& element : state.scene.elements) {
            for (const PortInfo& port : ports_of(element)) {
                ports.push_back({
                    {"element", port.ref.element},
                    {"port", port.ref.port},
                    {"x", port.position.x},
                    {"y", port.position.y},
                    {"dx", port.direction.x},
                    {"dy", port.direction.y},
                    {"curvature", port.curvature},
                    {"width", port.width_mm},
                });
            }
        }
        return ports;
    });

    dispatcher.add("scene.pick", [&state](const Json& params) {
        require_object(params);
        const Vec2 point = point_param(params, "point");
        const double tolerance = std::max(number_or(params, "tolerance", 10.0), 0.0);

        std::vector<ElementId> candidates = state.index.query_point(point, tolerance);
        if (candidates.empty() && state.index.empty()) {
            candidates.reserve(state.scene.elements.size());
            for (const Element& element : state.scene.elements) {
                candidates.push_back(element.id);
            }
        }

        const Element* best_element = nullptr;
        double best_distance = std::numeric_limits<double>::infinity();
        double best_t = 0.0;
        Vec2 best_point{};

        for (ElementId id : candidates) {
            const Element* element = state.scene.find_element(id);
            if (element == nullptr || element->hidden) continue;
            if (!layer_visible(state.scene, element->layer)) continue;

            double t = 0.0;
            const Vec2 closest = shape_closest_point(element->shape, point, &t);
            const double candidate_distance = distance(closest, point);
            if (candidate_distance < best_distance) {
                best_distance = candidate_distance;
                best_element = element;
                best_t = t;
                best_point = closest;
            }
        }

        if (best_element == nullptr || best_distance > tolerance) {
            return Json{{"element", nullptr}, {"distance", nullptr}};
        }

        Json result{
            {"element", best_element->id},
            {"t", best_t},
            {"distance", best_distance},
            {"point", Json::array({best_point.x, best_point.y})},
            {"port", nullptr},
        };

        double best_port_distance = std::numeric_limits<double>::infinity();
        for (const PortInfo& port : ports_of(*best_element)) {
            const double candidate_distance = distance(port.position, point);
            if (candidate_distance <= tolerance && candidate_distance < best_port_distance) {
                best_port_distance = candidate_distance;
                result["port"] = port.ref.port;
            }
        }

        return result;
    });

    dispatcher.add("scene.snap", [&state](const Json& params) {
        require_object(params);
        const Vec2 point = point_param(params, "point");

        SnapOptions options;
        options.tolerance_mm = std::max(number_or(params, "tolerance", 12.0), 0.0);
        options.grid_mm = number_or(params, "grid", 10.0);
        options.ports = bool_or(params, "ports", true);
        options.elements = bool_or(params, "elements", true);
        options.grid = bool_or(params, "snap_grid", true);
        if (params.contains("ignore")) {
            options.ignore = id_param(params, "ignore");
        }

        const SnapResult snap = snap_point(state.scene, point, options);

        Json result{
            {"kind", std::string{to_string(snap.kind)}},
            {"point", Json::array({snap.point.x, snap.point.y})},
            {"distance", snap.distance},
        };
        if (snap.kind == SnapKind::Port) {
            result["element"] = snap.element;
            result["port"] = snap.port;
            result["direction"] = Json::array({snap.direction.x, snap.direction.y});
        } else if (snap.kind == SnapKind::Element) {
            result["element"] = snap.element;
        }
        return result;
    });

    dispatcher.add("scene.validate", [&state](const Json&) {
        Json diagnostics = Json::array();
        for (const Diagnostic& diagnostic : validate_scene(state.scene)) {
            Json entry{
                {"severity", std::string{to_string(diagnostic.severity)}},
                {"code", diagnostic.code},
                {"message", diagnostic.message},
            };
            if (diagnostic.element != kInvalidId) entry["element"] = diagnostic.element;
            if (!diagnostic.port.empty()) entry["port"] = diagnostic.port;
            diagnostics.push_back(std::move(entry));
        }
        return diagnostics;
    });

    dispatcher.add("scene.bounds", [&state](const Json&) {
        const BBox box = state.scene.bounds();
        return Json{
            {"min", Json::array({box.min.x, box.min.y})},
            {"max", Json::array({box.max.x, box.max.y})},
            {"width", box.width()},
            {"height", box.height()},
        };
    });
}

} // namespace pf
