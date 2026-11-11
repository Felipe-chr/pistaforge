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
#include "core/analysis/track_analysis.hpp"
#include "core/io/robot_path.hpp"
#include "core/io/scene_json.hpp"
#include "core/io/scene_obj.hpp"
#include "core/io/scene_pdf.hpp"
#include "core/io/scene_svg.hpp"
#include "core/scene/element_ops.hpp"
#include "core/scene/snap.hpp"
#include "core/scene/validation.hpp"
#include "core/sim/path_table.hpp"

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

void write_text_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        fail("io_error", "Não foi possível gravar: " + path.string() + ".");
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        fail("io_error", "Falha ao gravar: " + path.string() + ".");
    }
}

/// Exporta a cena para SVG 1:1, em arquivo único ou em mosaico de páginas.
Json export_svg(AppState& state, const Json& params) {
    require_object(params);
    const std::filesystem::path path{string_param(params, "path")};

    io::SvgOptions options;
    options.tiled = bool_or(params, "tiled", false);
    options.page_width_mm = number_or(params, "page_width", options.page_width_mm);
    options.page_height_mm = number_or(params, "page_height", options.page_height_mm);
    options.overlap_mm = number_or(params, "overlap", options.overlap_mm);
    options.grid = bool_or(params, "grid", options.grid);
    options.calibration = bool_or(params, "calibration", options.calibration);
    options.guides = bool_or(params, "guides", options.guides);

    if (!(options.page_width_mm > 0.0) || !(options.page_height_mm > 0.0)) {
        fail("invalid_params", "As dimensões da página devem ser positivas.");
    }
    if (options.overlap_mm < 0.0) {
        fail("invalid_params", "A sobreposição não pode ser negativa.");
    }

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    Json files = Json::array();
    int pages = 1;
    if (options.tiled) {
        const std::vector<io::SvgPage> tiles = io::to_svg_pages(state.scene, options);
        pages = static_cast<int>(tiles.size());
        const std::string stem = path.stem().string();
        const std::string extension =
            path.extension().empty() ? std::string{".svg"} : path.extension().string();
        for (const io::SvgPage& page : tiles) {
            std::filesystem::path page_path = path;
            page_path.replace_filename(stem + "-c" + std::to_string(page.column + 1) + "-r" +
                                       std::to_string(page.row + 1) + extension);
            write_text_file(page_path, page.content);
            files.push_back(page_path.string());
        }
    } else {
        write_text_file(path, io::to_svg(state.scene, options));
        files.push_back(path.string());
    }

    if (path.has_parent_path()) state.last_dir = path.parent_path();
    return Json{
        {"files", files},
        {"pages", pages},
        {"tiled", options.tiled},
        {"width", state.scene.arena.width_mm},
        {"height", state.scene.arena.height_mm},
        {"rev", state.scene.revision},
    };
}

/// Matriz de coordenadas como JSON (colunas + linhas + metadados).
Json path_table_json(const PathTable& table) {
    Json points = Json::array();
    for (const PathSample& sample : table.samples) {
        points.push_back(Json::array({sample.s_mm, sample.position.x, sample.position.y,
                                      sample.heading_deg, sample.curvature, sample.width_mm,
                                      sample.element}));
    }

    Json segments = Json::array();
    for (const PathSegmentInfo& segment : table.segments) {
        segments.push_back(Json{
            {"element", segment.element},
            {"start_s_mm", segment.start_s_mm},
            {"length_mm", segment.length_mm},
            {"reversed", segment.reversed},
        });
    }

    return Json{
        {"columns", path_table_columns()},
        {"points", std::move(points)},
        {"segments", std::move(segments)},
        {"total_length_mm", table.total_length_mm},
        {"point_count", table.samples.size()},
        {"step_mm", table.step_mm},
        {"closed", table.closed},
        {"ordered", table.ordered},
        {"start_element", table.start_element},
    };
}

/// Exporta a cena para PDF 1:1, em página única ou mosaico.
Json export_pdf(AppState& state, const Json& params) {
    require_object(params);
    const std::filesystem::path path{string_param(params, "path")};

    io::PdfOptions options;
    options.tiled = bool_or(params, "tiled", false);
    options.page_width_mm = number_or(params, "page_width", options.page_width_mm);
    options.page_height_mm = number_or(params, "page_height", options.page_height_mm);
    options.overlap_mm = number_or(params, "overlap", options.overlap_mm);
    options.margin_mm = number_or(params, "margin", options.margin_mm);
    options.grid = bool_or(params, "grid", options.grid);
    options.calibration = bool_or(params, "calibration", options.calibration);
    options.guides = bool_or(params, "guides", options.guides);
    options.centerline = bool_or(params, "centerline", options.centerline);
    options.floor_marks = bool_or(params, "marks", options.floor_marks);
    options.mark_step_mm = number_or(params, "mark_step", options.mark_step_mm);
    options.path_width_mm = number_or(params, "path_width", options.path_width_mm);

    if (!(options.page_width_mm > 0.0) || !(options.page_height_mm > 0.0)) {
        fail("invalid_params", "As dimensões da página devem ser positivas.");
    }
    if (options.overlap_mm < 0.0) {
        fail("invalid_params", "A sobreposição não pode ser negativa.");
    }
    if (options.margin_mm < 0.0) {
        fail("invalid_params", "A margem não pode ser negativa.");
    }
    if (!(options.mark_step_mm > 0.0)) {
        fail("invalid_params", "O passo das marcas deve ser positivo.");
    }
    if (!(options.path_width_mm > 0.0)) {
        fail("invalid_params", "A largura da linha central deve ser positiva.");
    }

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    const std::string pdf = io::to_pdf(state.scene, options);
    write_text_file(path, pdf);

    int pages = 0;
    std::size_t cursor = 0;
    while ((cursor = pdf.find("/Type /Page ", cursor)) != std::string::npos) {
        ++pages;
        cursor += 12;
    }

    if (path.has_parent_path()) state.last_dir = path.parent_path();
    return Json{
        {"files", Json::array({path.string()})},
        {"pages", pages},
        {"tiled", options.tiled},
        {"marks", options.floor_marks},
        {"centerline", options.centerline},
        {"width", state.scene.arena.width_mm},
        {"height", state.scene.arena.height_mm},
        {"rev", state.scene.revision},
    };
}

/// Exporta a pista em 3D (OBJ + MTL) para uso no Blender.
Json export_obj(AppState& state, const Json& params) {
    require_object(params);
    const std::filesystem::path path{string_param(params, "path")};

    io::ObjOptions options;
    options.scale = number_or(params, "scale", options.scale);
    options.height_mm = number_or(params, "height", options.height_mm);
    options.floor = bool_or(params, "floor", options.floor);
    options.centerline = bool_or(params, "centerline", options.centerline);
    options.step_mm = number_or(params, "step", options.step_mm);

    if (!(options.scale > 0.0)) fail("invalid_params", "A escala deve ser positiva.");
    if (!(options.height_mm > 0.0)) fail("invalid_params", "A espessura deve ser positiva.");
    if (!(options.step_mm > 0.0)) fail("invalid_params", "O passo da malha deve ser positivo.");

    std::filesystem::path mtl_path = path;
    mtl_path.replace_extension(".mtl");
    options.mtl_name = mtl_path.filename().string();

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    const io::ObjBundle bundle = io::to_obj(state.scene, options);
    write_text_file(path, bundle.obj);
    write_text_file(mtl_path, bundle.mtl);

    if (path.has_parent_path()) state.last_dir = path.parent_path();
    return Json{
        {"files", Json::array({path.string(), mtl_path.string()})},
        {"vertices", bundle.vertices},
        {"faces", bundle.faces},
        {"scale", options.scale},
        {"height_mm", options.height_mm},
        {"rev", state.scene.revision},
    };
}

/// Exporta os pontos e as instruções de pintura para um robô autônomo.
Json export_robot(AppState& state, const Json& params) {
    require_object(params);
    const std::filesystem::path path{string_param(params, "path")};

    io::RobotPathOptions options;
    options.step_mm = number_or(params, "step", options.step_mm);
    options.brush_width_mm = number_or(params, "brush", options.brush_width_mm);
    options.speed_mm_s = number_or(params, "speed", options.speed_mm_s);

    if (!(options.step_mm > 0.0)) fail("invalid_params", "O passo deve ser positivo.");
    if (options.brush_width_mm < 0.0) {
        fail("invalid_params", "A largura do pincel não pode ser negativa.");
    }
    if (!(options.speed_mm_s > 0.0)) {
        fail("invalid_params", "A velocidade deve ser positiva.");
    }

    const io::RobotPath robot = io::build_robot_path(state.scene, options);

    std::filesystem::path csv_path = path;
    std::string base = path.stem().string();
    if (base.ends_with("-pontos")) base.resize(base.size() - 7);
    csv_path.replace_filename(base + "-pontos.csv");
    std::filesystem::path txt_path = path;
    txt_path.replace_filename(base + "-instrucoes.txt");

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    write_text_file(csv_path, robot.points_csv);
    write_text_file(txt_path, robot.instructions);

    if (path.has_parent_path()) state.last_dir = path.parent_path();
    return Json{
        {"files", Json::array({csv_path.string(), txt_path.string()})},
        {"points", robot.point_count},
        {"total_length_mm", robot.total_length_mm},
        {"step_mm", options.step_mm},
        {"clockwise", robot.clockwise},
        {"closed", robot.table.closed},
        {"rev", state.scene.revision},
    };
}

/// Exporta a matriz de coordenadas em CSV.
Json export_table(AppState& state, const Json& params) {
    require_object(params);
    const std::filesystem::path path{string_param(params, "path")};

    PathTableOptions options;
    options.step_mm = number_or(params, "step", 50.0);
    options.tolerance_mm = 0.25;
    if (!(options.step_mm > 0.0)) fail("invalid_params", "O passo deve ser positivo.");

    const PathTable table = build_path_table(state.scene, options);

    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }
    write_text_file(path, path_table_csv(table));

    if (path.has_parent_path()) state.last_dir = path.parent_path();
    return Json{
        {"files", Json::array({path.string()})},
        {"points", table.samples.size()},
        {"total_length_mm", table.total_length_mm},
        {"step_mm", table.step_mm},
        {"closed", table.closed},
        {"ordered", table.ordered},
        {"rev", state.scene.revision},
    };
}

/// Elemento editável (existe e não está bloqueado) ou erro estruturado.
Element& element_or_fail(AppState& state, ElementId id) {
    Element* element = state.scene.find_element(id);
    if (element == nullptr) {
        fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
    }
    if (element->locked) {
        fail("element_locked", "O elemento está bloqueado.");
    }
    return *element;
}

/// Renomeia portas `in`/`out` para `start`/`end` ao converter um módulo em primitiva.
void migrate_module_ports(Scene& scene, ElementId id) {
    for (Connection& connection : scene.connections) {
        if (connection.a.element == id) {
            if (connection.a.port == "in") connection.a.port = "start";
            else if (connection.a.port == "out") connection.a.port = "end";
        }
        if (connection.b.element == id) {
            if (connection.b.port == "in") connection.b.port = "start";
            else if (connection.b.port == "out") connection.b.port = "end";
        }
    }
}

Json element_info_json(const Element& element) {
    const std::optional<ShapeType> type = infer_shape_type(element.shape);
    const std::optional<double> radius = shape_radius(element.shape);
    const std::optional<double> angle = shape_angle_deg(element.shape);
    const int automatic = element_auto_difficulty(element);
    const auto endpoints = shape_endpoints(element.shape);

    std::string type_name = "shape";
    if (type) {
        type_name = std::string{to_string(*type)};
    } else if (std::holds_alternative<ArcSegment>(element.shape)) {
        type_name = "arc";
    } else if (const auto* module = std::get_if<ModuleInstance>(&element.shape)) {
        type_name = module->template_id;
    }

    Json result{
        {"id", element.id},
        {"type", type_name},
        {"width", element.width_mm},
        {"difficulty", element.difficulty},
        {"auto_difficulty", automatic},
        {"effective_difficulty", element.difficulty > 0 ? element.difficulty : automatic},
        {"flippable", type != ShapeType::Line &&
                          !std::holds_alternative<LineSegment>(element.shape)},
        {"splittable", std::holds_alternative<LineSegment>(element.shape) ||
                           std::holds_alternative<ArcSegment>(element.shape) ||
                           std::holds_alternative<CubicBezier>(element.shape)},
        {"closed", std::holds_alternative<ArcSegment>(element.shape) &&
                       std::abs(std::get<ArcSegment>(element.shape).sweep_angle) >
                           2.0 * std::numbers::pi - 1e-3},
    };
    if (radius) result["radius_mm"] = *radius;
    if (angle) result["angle_deg"] = *angle;
    if (endpoints) {
        result["start"] = Json::array({endpoints->first.x, endpoints->first.y});
        result["end"] = Json::array({endpoints->second.x, endpoints->second.y});
    }
    return result;
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

    dispatcher.add("doc.exportSvg", [&state](const Json& params) -> Json {
        return export_svg(state, params);
    });

    dispatcher.add("doc.exportSvgDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Exportar desenho SVG";
        request.initial_path = dialog_directory(state);
        request.default_name =
            state.path.empty() ? "pista.svg" : state.path.stem().string() + ".svg";
        request.filter_label = "Desenho SVG (*.svg)";
        request.filter_pattern = "*.svg";
        request.default_extension = "svg";

        const std::optional<std::filesystem::path> chosen = state.file_dialog(request);
        if (!chosen) return Json{{"cancelled", true}};
        if (chosen->has_parent_path()) state.last_dir = chosen->parent_path();

        Json forwarded = params;
        forwarded["path"] = chosen->string();
        return export_svg(state, forwarded);
    });

    dispatcher.add("doc.exportPdf", [&state](const Json& params) -> Json {
        return export_pdf(state, params);
    });

    dispatcher.add("doc.exportPdfDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Exportar PDF 1:1";
        request.initial_path = dialog_directory(state);
        request.default_name =
            state.path.empty() ? "pista.pdf" : state.path.stem().string() + ".pdf";
        request.filter_label = "PDF (*.pdf)";
        request.filter_pattern = "*.pdf";
        request.default_extension = "pdf";

        const std::optional<std::filesystem::path> chosen = state.file_dialog(request);
        if (!chosen) return Json{{"cancelled", true}};
        if (chosen->has_parent_path()) state.last_dir = chosen->parent_path();

        Json forwarded = params;
        forwarded["path"] = chosen->string();
        return export_pdf(state, forwarded);
    });

    dispatcher.add("doc.exportObj", [&state](const Json& params) -> Json {
        return export_obj(state, params);
    });

    dispatcher.add("doc.exportObjDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Exportar pista 3D (Blender)";
        request.initial_path = dialog_directory(state);
        request.default_name =
            state.path.empty() ? "pista.obj" : state.path.stem().string() + ".obj";
        request.filter_label = "Malha OBJ (*.obj)";
        request.filter_pattern = "*.obj";
        request.default_extension = "obj";

        const std::optional<std::filesystem::path> chosen = state.file_dialog(request);
        if (!chosen) return Json{{"cancelled", true}};
        if (chosen->has_parent_path()) state.last_dir = chosen->parent_path();

        Json forwarded = params;
        forwarded["path"] = chosen->string();
        return export_obj(state, forwarded);
    });

    dispatcher.add("doc.exportRobot", [&state](const Json& params) -> Json {
        return export_robot(state, params);
    });

    dispatcher.add("doc.exportRobotDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Exportar pontos do robô";
        request.initial_path = dialog_directory(state);
        request.default_name = state.path.empty() ? "pista-pontos.csv"
                                                  : state.path.stem().string() + "-pontos.csv";
        request.filter_label = "Pontos do robô (*.csv)";
        request.filter_pattern = "*.csv";
        request.default_extension = "csv";

        const std::optional<std::filesystem::path> chosen = state.file_dialog(request);
        if (!chosen) return Json{{"cancelled", true}};
        if (chosen->has_parent_path()) state.last_dir = chosen->parent_path();

        Json forwarded = params;
        forwarded["path"] = chosen->string();
        return export_robot(state, forwarded);
    });

    dispatcher.add("doc.exportTable", [&state](const Json& params) -> Json {
        return export_table(state, params);
    });

    dispatcher.add("doc.exportTableDialog", [&state](const Json& params) -> Json {
        require_object(params);
        if (!state.file_dialog) {
            fail("dialog_unavailable", "Diálogo de arquivos indisponível.");
        }

        FileDialogRequest request;
        request.save = true;
        request.title = "Exportar tabela de coordenadas";
        request.initial_path = dialog_directory(state);
        request.default_name = state.path.empty() ? "pista-tabela.csv"
                                                  : state.path.stem().string() + "-tabela.csv";
        request.filter_label = "Tabela de coordenadas (*.csv)";
        request.filter_pattern = "*.csv";
        request.default_extension = "csv";

        const std::optional<std::filesystem::path> chosen = state.file_dialog(request);
        if (!chosen) return Json{{"cancelled", true}};
        if (chosen->has_parent_path()) state.last_dir = chosen->parent_path();

        Json forwarded = params;
        forwarded["path"] = chosen->string();
        return export_table(state, forwarded);
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

    // Define (ou reposiciona) a marca de largada/chegada do circuito.
    add_change(dispatcher, state, "doc.setStartFinish", "Definir largada/chegada",
               [&state](const Json& params) {
                   require_object(params);

                   MarkerType type = MarkerType::StartFinish;
                   if (params.contains("kind")) {
                       const auto parsed = marker_type_from_string(string_param(params, "kind"));
                       if (!parsed.has_value() || (*parsed != MarkerType::Start &&
                                                   *parsed != MarkerType::Finish &&
                                                   *parsed != MarkerType::StartFinish)) {
                           fail("invalid_params",
                                "Campo 'kind' deve ser 'start', 'finish' ou 'start_finish'.");
                       }
                       type = *parsed;
                   }

                   const Vec2 position{
                       number_param(params, "x"),
                       number_param(params, "y"),
                   };
                   const double angle =
                       number_or(params, "angle", number_or(params, "angle_deg", 0.0)) *
                       kDegToRad;

                   const auto existing =
                       std::find_if(state.scene.markers.begin(), state.scene.markers.end(),
                                    [type](const Marker& marker) {
                                        return marker.type == type;
                                    });

                   Marker* marker = nullptr;
                   if (existing != state.scene.markers.end()) {
                       marker = &*existing;
                       marker->position = position;
                       marker->angle = angle;
                   } else {
                       Marker created;
                       created.id = state.scene.allocate_id();
                       created.type = type;
                       created.position = position;
                       created.angle = angle;
                       state.scene.markers.push_back(std::move(created));
                       marker = &state.scene.markers.back();
                   }

                   return Json{
                       {"id", marker->id},
                       {"type", std::string{to_string(marker->type)}},
                       {"x", marker->position.x},
                       {"y", marker->position.y},
                       {"angle", marker->angle / kDegToRad},
                   };
               });

    add_change(dispatcher, state, "doc.clearStartFinish", "Remover largada/chegada",
               [&state](const Json& params) {
                   require_object(params);

                   const auto matches = [](MarkerType type) {
                       return type == MarkerType::Start || type == MarkerType::Finish ||
                              type == MarkerType::StartFinish;
                   };

                   MarkerType type = MarkerType::StartFinish;
                   const bool filter = params.contains("kind");
                   if (filter) {
                       const auto parsed = marker_type_from_string(string_param(params, "kind"));
                       if (!parsed.has_value() || !matches(*parsed)) {
                           fail("invalid_params",
                                "Campo 'kind' deve ser 'start', 'finish' ou 'start_finish'.");
                       }
                       type = *parsed;
                   }

                   const std::size_t before = state.scene.markers.size();
                   std::erase_if(state.scene.markers, [&](const Marker& marker) {
                       return matches(marker.type) && (!filter || marker.type == type);
                   });
                   return Json{{"removed", before - state.scene.markers.size()}};
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
                   if (params.contains("difficulty")) {
                       const double raw = number_param(params, "difficulty");
                       const int level = static_cast<int>(std::lround(raw));
                       if (level < 0 || level > 5) {
                           fail("invalid_params",
                                "Campo 'difficulty' deve estar entre 0 e 5.");
                       }
                       element->difficulty = level;
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

    add_change(dispatcher, state, "doc.flipElement", "Inverter curva",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   if (!flip_shape(element.shape)) {
                       fail("unsupported", "Este elemento não pode ser invertido.");
                   }
                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.mirrorElement", "Espelhar elemento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   const BBox box = shape_bbox(element.shape);
                   const double axis = params.contains("axis")
                                           ? number_param(params, "axis")
                                           : (box.is_valid() ? box.center().x : 0.0);
                   if (!mirror_shape(element.shape, axis)) {
                       fail("unsupported", "Este elemento não pode ser espelhado.");
                   }
                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.duplicateElement", "Duplicar elemento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   const Element* source = state.scene.find_element(id);
                   if (source == nullptr) {
                       fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
                   }

                   Vec2 offset{20.0, 20.0};
                   if (params.contains("offset")) {
                       offset = point_param(params, "offset");
                   } else {
                       offset = {number_or(params, "dx", 20.0), number_or(params, "dy", 20.0)};
                   }

                   Element copy = *source;
                   copy.id = state.scene.allocate_id();
                   translate(copy, offset);
                   const ElementId created = copy.id;
                   state.scene.elements.push_back(std::move(copy));
                   return Json{{"id", created}, {"source", id}};
               });

    add_change(dispatcher, state, "doc.adjustLength", "Ajustar comprimento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);

                   double length = 0.0;
                   if (params.contains("length")) {
                       length = positive_number(params.at("length"), "length");
                   } else {
                       length = shape_length(element.shape) + number_or(params, "delta", 0.0);
                   }
                   if (!adjust_length(element.shape, length)) {
                       fail("unsupported",
                            "Ajuste de comprimento disponível apenas para retas.");
                   }
                   return Json{{"id", id}, {"length", length}};
               });

    const Handler radius_handler = [&state](const Json& params) {
        require_object(params);
        const ElementId id = id_param(params, "id");
        Element& element = element_or_fail(state, id);
        const std::optional<double> current = shape_radius(element.shape);
        if (!current) {
            fail("unsupported", "Este elemento não possui raio ajustável.");
        }

        double factor = 1.25;
        if (params.contains("factor")) {
            factor = positive_number(params.at("factor"), "factor");
        } else if (params.contains("radius")) {
            factor = positive_number(params.at("radius"), "radius") / *current;
        } else if (params.contains("delta")) {
            const double target = std::max(*current + number_param(params, "delta"), 1.0);
            factor = target / *current;
        }

        if (!adjust_radius(element.shape, factor)) {
            fail("unsupported", "Não foi possível ajustar o raio.");
        }
        return Json{
            {"id", id},
            {"factor", factor},
            {"radius_mm", shape_radius(element.shape).value_or(0.0)},
        };
    };
    add_change(dispatcher, state, "doc.adjustRadius", "Ajustar raio", radius_handler);
    add_change(dispatcher, state, "doc.setRadius", "Definir raio", radius_handler);

    const Handler angle_handler = [&state](const Json& params) {
        require_object(params);
        const ElementId id = id_param(params, "id");
        Element& element = element_or_fail(state, id);
        const std::optional<double> current = shape_angle_deg(element.shape);
        if (!current) {
            fail("unsupported", "Este elemento não possui ângulo ajustável.");
        }

        double delta = number_or(params, "delta", 15.0);
        if (params.contains("angle")) {
            delta = std::abs(number_param(params, "angle")) - std::abs(*current);
        }
        if (!adjust_angle(element.shape, delta)) {
            fail("unsupported", "Não foi possível ajustar o ângulo.");
        }
        return Json{
            {"id", id},
            {"delta", delta},
            {"angle_deg", shape_angle_deg(element.shape).value_or(0.0)},
        };
    };
    add_change(dispatcher, state, "doc.adjustAngle", "Ajustar ângulo", angle_handler);
    add_change(dispatcher, state, "doc.setAngle", "Definir ângulo", angle_handler);

    add_change(dispatcher, state, "doc.smoothElement", "Suavizar curva",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   const bool was_module = std::holds_alternative<ModuleInstance>(element.shape);
                   const std::optional<Shape> converted = smooth_shape(element.shape);
                   if (!converted) {
                       fail("unsupported", "Este elemento não pode ser suavizado.");
                   }
                   element.shape = *converted;
                   if (was_module) migrate_module_ports(state.scene, id);
                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.closeElement", "Fechar curva",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   const bool was_module = std::holds_alternative<ModuleInstance>(element.shape);
                   const std::optional<Shape> converted = closed_shape(element.shape);
                   if (!converted) {
                       fail("unsupported", "Este elemento não pode ser fechado.");
                   }
                   element.shape = *converted;
                   if (was_module) migrate_module_ports(state.scene, id);
                   return Json{{"id", id}};
               });

    add_change(dispatcher, state, "doc.convertElement", "Converter forma",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   const std::string name = string_param(params, "shape");
                   const std::optional<ShapeType> type = shape_type_from_string(name);
                   if (!type) {
                       fail("invalid_params", "Forma desconhecida: " + name + ".");
                   }
                   const std::optional<Shape> converted = convert_shape(element.shape, *type);
                   if (!converted) {
                       fail("unsupported", "Não foi possível converter a forma.");
                   }
                   const bool was_module = std::holds_alternative<ModuleInstance>(element.shape);
                   element.shape = *converted;
                   if (was_module) migrate_module_ports(state.scene, id);
                   return Json{{"id", id}, {"shape", std::string{to_string(*type)}}};
               });

    add_change(dispatcher, state, "doc.splitElement", "Dividir segmento",
               [&state](const Json& params) {
                   require_object(params);
                   const ElementId id = id_param(params, "id");
                   Element& element = element_or_fail(state, id);
                   const std::optional<SplitShapes> parts =
                       split_shape(element.shape, number_or(params, "t", 0.5));
                   if (!parts) {
                       fail("unsupported", "Este elemento não pode ser dividido.");
                   }

                   Element second = element;
                   second.id = state.scene.allocate_id();
                   second.shape = parts->second;
                   element.shape = parts->first;

                   for (Connection& connection : state.scene.connections) {
                       if (connection.a.element == id && connection.a.port == "end") {
                           connection.a.element = second.id;
                       }
                       if (connection.b.element == id && connection.b.port == "end") {
                           connection.b.element = second.id;
                       }
                   }

                   const ElementId second_id = second.id;
                   state.scene.elements.push_back(std::move(second));
                   const ConnectionId connection_id =
                       state.scene
                           .add_connection(PortRef{id, "end"}, PortRef{second_id, "start"})
                           .id;
                   return Json{
                       {"id", id},
                       {"second", second_id},
                       {"connection", connection_id},
                   };
               });

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
            result["direction"] = Json::array({snap.direction.x, snap.direction.y});
        }
        return result;
    });

    dispatcher.add("scene.pathTable", [&state](const Json& params) {
        const double step = number_or(params, "step", 50.0);
        if (!(step > 0.0)) {
            fail("invalid_params", "O passo da tabela deve ser positivo.");
        }
        PathTableOptions options;
        options.step_mm = step;
        options.tolerance_mm = 0.25;
        return path_table_json(build_path_table(state.scene, options));
    });

    dispatcher.add("scene.analysis", [&state](const Json&) {
        const TrackAnalysis analysis = analyze_track(state.scene);
        const TrackMetrics& metrics = analysis.metrics;

        PathTableOptions path_options;
        path_options.step_mm = 50.0;
        path_options.tolerance_mm = 0.25;
        const PathTable path = build_path_table(state.scene, path_options);

        Json critical = Json::array();
        for (const CriticalPoint& point : analysis.critical_points) {
            Json entry{
                {"kind", std::string{to_string(point.kind)}},
                {"x", point.position.x},
                {"y", point.position.y},
                {"severity", point.severity},
            };
            if (point.element != kInvalidId) entry["element"] = point.element;
            if (point.t > 0.0) entry["t"] = point.t;
            if (point.angle_deg > 0.0) entry["angle_deg"] = point.angle_deg;
            if (point.radius_mm > 0.0) entry["radius_mm"] = point.radius_mm;
            critical.push_back(std::move(entry));
        }

        return Json{
            {"total_length_mm", metrics.total_length_mm},
            {"straight_length_mm", metrics.straight_length_mm},
            {"curve_length_mm", metrics.curve_length_mm},
            {"straight_count", metrics.straight_count},
            {"curve_count", metrics.curve_count},
            {"crossing_count", metrics.crossing_count},
            {"special_count", metrics.special_count},
            {"element_count", metrics.element_count},
            {"connection_count", metrics.connection_count},
            {"consecutive_curve_links", metrics.consecutive_curve_links},
            {"sharp_turn_count", metrics.sharp_turn_count},
            {"tight_curve_count", metrics.tight_curve_count},
            {"difficulty", metrics.difficulty},
            {"difficulty_label", metrics.difficulty_label},
            {"estimated_time_s", metrics.estimated_time_s},
            {"recommended_speed_mps", metrics.recommended_speed_mps},
            {"critical_points", std::move(critical)},
            {"path", Json{
                         {"point_count", path.samples.size()},
                         {"step_mm", path.step_mm},
                         {"total_length_mm", path.total_length_mm},
                         {"closed", path.closed},
                         {"ordered", path.ordered},
                     }},
            {"rev", state.scene.revision},
        };
    });

    dispatcher.add("scene.elementInfo", [&state](const Json& params) {
        require_object(params);
        const ElementId id = id_param(params, "id");
        const Element* element = state.scene.find_element(id);
        if (element == nullptr) {
            fail("not_found", "Elemento inexistente: " + std::to_string(id) + ".");
        }
        return element_info_json(*element);
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
