#include "core/io/scene_svg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace pf::io {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;

/// Formata um número em mm com até três casas, sem zeros à direita.
std::string fmt(double value) {
    if (!std::isfinite(value)) return "0";
    if (std::abs(value) < 0.0005) value = 0.0;

    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    std::string text{buffer};
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text.empty() || text == "-0") text = "0";
    return text;
}

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(character);
        }
    }
    return out;
}

std::string line_tag(double x1, double y1, double x2, double y2, std::string_view color,
                     double width, std::string_view extra = {}) {
    return "<line x1=\"" + fmt(x1) + "\" y1=\"" + fmt(y1) + "\" x2=\"" + fmt(x2) +
           "\" y2=\"" + fmt(y2) + "\" stroke=\"" + std::string{color} +
           "\" stroke-width=\"" + fmt(width) + "\"" + std::string{extra} + "/>\n";
}

std::string rect_tag(double x, double y, double width, double height, std::string_view fill,
                     std::string_view stroke, double stroke_width,
                     std::string_view extra = {}) {
    return "<rect x=\"" + fmt(x) + "\" y=\"" + fmt(y) + "\" width=\"" + fmt(width) +
           "\" height=\"" + fmt(height) + "\" fill=\"" + std::string{fill} +
           "\" stroke=\"" + std::string{stroke} + "\" stroke-width=\"" +
           fmt(stroke_width) + "\"" + std::string{extra} + "/>\n";
}

std::string text_tag(double x, double y, std::string_view text, double size,
                     std::string_view fill, std::string_view anchor = "start",
                     std::string_view weight = "normal") {
    return "<text x=\"" + fmt(x) + "\" y=\"" + fmt(y) + "\" font-family=\"Segoe UI, Arial, "
           "sans-serif\" font-size=\"" + fmt(size) + "\" font-weight=\"" +
           std::string{weight} + "\" fill=\"" + std::string{fill} + "\" text-anchor=\"" +
           std::string{anchor} + "\">" + escape(text) + "</text>\n";
}

std::string arc_path(const ArcSegment& arc) {
    const Vec2 start = arc.point_at(0.0);
    std::string path = "M " + fmt(start.x) + " " + fmt(start.y);

    const int sweep_flag = arc.sweep_angle >= 0.0 ? 1 : 0;
    const std::string radius = fmt(arc.radius);
    const std::string flags = " 0 " + std::to_string(std::abs(arc.sweep_angle) > kPi ? 1 : 0) +
                              " " + std::to_string(sweep_flag) + " ";

    if (std::abs(arc.sweep_angle) >= kTau - 1e-6) {
        const Vec2 middle = arc.point_at(0.5);
        path += " A " + radius + " " + radius + flags + fmt(middle.x) + " " + fmt(middle.y);
        path += " A " + radius + " " + radius + flags + fmt(start.x) + " " + fmt(start.y);
    } else {
        const Vec2 end = arc.point_at(1.0);
        path += " A " + radius + " " + radius + flags + fmt(end.x) + " " + fmt(end.y);
    }
    return path;
}

std::string shape_path(const Shape& shape) {
    if (const auto* line = std::get_if<LineSegment>(&shape)) {
        return "M " + fmt(line->a.x) + " " + fmt(line->a.y) + " L " + fmt(line->b.x) + " " +
               fmt(line->b.y);
    }
    if (const auto* arc = std::get_if<ArcSegment>(&shape)) {
        return arc_path(*arc);
    }
    if (const auto* bezier = std::get_if<CubicBezier>(&shape)) {
        return "M " + fmt(bezier->p0.x) + " " + fmt(bezier->p0.y) + " C " + fmt(bezier->p1.x) +
               " " + fmt(bezier->p1.y) + " " + fmt(bezier->p2.x) + " " + fmt(bezier->p2.y) +
               " " + fmt(bezier->p3.x) + " " + fmt(bezier->p3.y);
    }

    // Módulos são compostos por segmentos locais já transformados; cada trecho
    // recebe seu próprio `M` para não criar ligações espúrias.
    const auto* module = std::get_if<ModuleInstance>(&shape);
    if (module == nullptr) return {};

    std::string path;
    for (const auto& [a, b] : module_local_segments(*module)) {
        const Vec2 start = module->transform.apply(a);
        const Vec2 end = module->transform.apply(b);
        path += "M " + fmt(start.x) + " " + fmt(start.y) + " L " + fmt(end.x) + " " +
                fmt(end.y) + " ";
    }
    if (!path.empty()) path.pop_back();
    return path;
}

std::string element_tag(const Element& element) {
    std::string color{"#0f172a"};
    std::string extra;
    double width = element.width_mm;

    switch (element.layer) {
        case LayerId::Track: color = "#0f172a"; break;
        case LayerId::Markers: color = "#b91c1c"; break;
        case LayerId::Annotations: color = "#475569"; break;
        case LayerId::Guides:
            color = "#7c3aed";
            extra = " stroke-dasharray=\"6 4\"";
            width = std::max(0.35, element.width_mm * 0.15);
            break;
    }

    return "<path d=\"" + shape_path(element.shape) + "\" fill=\"none\" stroke=\"" + color +
           "\" stroke-width=\"" + fmt(width) +
           "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" + extra + "/>\n";
}

std::string checker_band(const Marker& marker) {
    constexpr double kLength = 60.0;
    constexpr double kThickness = 12.0;
    constexpr double kCell = 6.0;
    constexpr int kColumns = 10;
    constexpr int kRows = 2;

    const double angle_deg = marker.angle * 180.0 / kPi;
    std::string out = "<g transform=\"translate(" + fmt(marker.position.x) + " " +
                      fmt(marker.position.y) + ") rotate(" + fmt(angle_deg) + ")\">\n";
    out += rect_tag(-kLength / 2.0, -kThickness / 2.0, kLength, kThickness, "#ffffff",
                    "#0f172a", 0.6);

    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            if ((row + column) % 2 != 0) continue;
            out += "<rect x=\"" + fmt(-kLength / 2.0 + column * kCell) + "\" y=\"" +
                   fmt(-kThickness / 2.0 + row * kCell) + "\" width=\"" + fmt(kCell) +
                   "\" height=\"" + fmt(kCell) + "\" fill=\"#0f172a\"/>\n";
        }
    }

    out += "</g>\n";
    return out;
}

std::string marker_tag(const Marker& marker) {
    switch (marker.type) {
        case MarkerType::Start:
        case MarkerType::Finish:
        case MarkerType::StartFinish: {
            std::string out = checker_band(marker);

            const std::string color = marker.type == MarkerType::Start   ? "#15803d"
                                      : marker.type == MarkerType::Finish ? "#b91c1c"
                                                                         : "#1d4ed8";
            const std::string label = marker.type == MarkerType::Start   ? "LARGADA"
                                      : marker.type == MarkerType::Finish ? "CHEGADA"
                                                                         : "LARGADA / CHEGADA";
            out += text_tag(marker.position.x + 8.0, marker.position.y - 9.0, label, 5.5, color,
                            "start", "bold");
            return out;
        }
        case MarkerType::DirectionArrow: {
            const double angle_deg = marker.angle * 180.0 / kPi;
            return "<g transform=\"translate(" + fmt(marker.position.x) + " " +
                   fmt(marker.position.y) + ") rotate(" + fmt(angle_deg) +
                   ")\"><polygon points=\"0,-5 13,0 0,5\" fill=\"#dc2626\"/></g>\n";
        }
        case MarkerType::Text:
            break;
    }

    if (marker.text.empty()) return {};
    return text_tag(marker.position.x + 4.0, marker.position.y, marker.text, 5.0, "#7f1d1d");
}

struct Tile {
    int row{0};
    int column{0};
    int rows{1};
    int columns{1};
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

std::vector<Tile> layout_tiles(const Arena& arena, const SvgOptions& options) {
    const double page_width = std::max(10.0, options.page_width_mm);
    const double page_height = std::max(10.0, options.page_height_mm);
    const double limit = std::min(page_width, page_height) * 0.5;
    const double overlap = std::clamp(options.overlap_mm, 0.0, limit);
    const double step_x = page_width - overlap;
    const double step_y = page_height - overlap;

    const auto count = [](double size, double page, double step) {
        if (size <= page + 1e-9) return 1;
        return static_cast<int>(std::ceil((size - (page - step)) / step));
    };
    const auto origin = [](double size, double page, double step, int index, int total) {
        if (total <= 1) return size >= page ? 0.0 : -(page - size) * 0.5;
        return std::min(index * step, std::max(0.0, size - page));
    };

    const int columns = count(arena.width_mm, page_width, step_x);
    const int rows = count(arena.height_mm, page_height, step_y);

    std::vector<Tile> tiles;
    tiles.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            tiles.push_back(Tile{
                .row = row,
                .column = column,
                .rows = rows,
                .columns = columns,
                .x = origin(arena.width_mm, page_width, step_x, column, columns),
                .y = origin(arena.height_mm, page_height, step_y, row, rows),
                .width = page_width,
                .height = page_height,
            });
        }
    }
    return tiles;
}

std::string page_content(const Scene& scene, const SvgOptions& options, const Tile& tile,
                         bool tiled) {
    const Arena& arena = scene.arena;

    std::string out;
    out += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + fmt(tile.width) +
           "mm\" height=\"" + fmt(tile.height) + "mm\" viewBox=\"" + fmt(tile.x) + " " +
           fmt(tile.y) + " " + fmt(tile.width) + " " + fmt(tile.height) + "\">\n";
    out += "<title>" + escape(arena.name) + "</title>\n";

    // Fundo da página.
    out += rect_tag(tile.x, tile.y, tile.width, tile.height, "#ffffff", "none", 0.0);

    // Arena e margem de segurança.
    out += rect_tag(0.0, 0.0, arena.width_mm, arena.height_mm, "#ffffff", "#0f172a", 1.2);
    if (arena.margin_mm > 0.0) {
        out += rect_tag(arena.margin_mm, arena.margin_mm,
                        arena.width_mm - arena.margin_mm * 2.0,
                        arena.height_mm - arena.margin_mm * 2.0, "none", "#94a3b8", 0.4,
                        " stroke-dasharray=\"8 5\"");
    }

    // Grade de referência a cada 100 mm (reforçada a cada 500 mm).
    if (options.grid) {
        for (double x = 100.0; x < arena.width_mm - 1e-9; x += 100.0) {
            const bool major = std::fmod(x, 500.0) < 1e-9;
            out += line_tag(x, 0.0, x, arena.height_mm, major ? "#cbd5e1" : "#e2e8f0",
                            major ? 0.3 : 0.18);
        }
        for (double y = 100.0; y < arena.height_mm - 1e-9; y += 100.0) {
            const bool major = std::fmod(y, 500.0) < 1e-9;
            out += line_tag(0.0, y, arena.width_mm, y, major ? "#cbd5e1" : "#e2e8f0",
                            major ? 0.3 : 0.18);
        }
    }

    // Elementos visíveis, respeitando as camadas do documento.
    for (const Element& element : scene.elements) {
        if (element.hidden) continue;
        if (element.layer == LayerId::Guides) {
            if (!(options.guides || scene.layers.guides)) continue;
        } else if (!layer_visible(scene, element.layer)) {
            continue;
        }
        out += element_tag(element);
    }

    // Marcadores.
    if (scene.layers.markers) {
        for (const Marker& marker : scene.markers) {
            out += marker_tag(marker);
        }
    }

    // Guias.
    if (scene.layers.guides || options.guides) {
        for (const Guide& guide : scene.guides) {
            out += line_tag(guide.a.x, guide.a.y, guide.b.x, guide.b.y, "#7c3aed", 0.35,
                            " stroke-dasharray=\"6 4\"");
        }
    }

    // Cotas da arena.
    if (arena.show_dimensions) {
        out += text_tag(arena.width_mm - 4.0, arena.height_mm - 4.0,
                        fmt(arena.width_mm) + " x " + fmt(arena.height_mm) + " mm", 5.0,
                        "#64748b", "end");
    }

    // Calibração e rótulo da página.
    if (options.calibration) {
        const double ruler_x = tile.x + 14.0;
        const double ruler_y = tile.y + tile.height - 12.0;
        out += line_tag(ruler_x, ruler_y, ruler_x + 100.0, ruler_y, "#0f172a", 0.35);
        for (int step = 0; step <= 100; step += 10) {
            const double tick = step % 50 == 0 ? 4.5 : 2.5;
            out += line_tag(ruler_x + step, ruler_y, ruler_x + step, ruler_y - tick, "#0f172a",
                            0.35);
        }
        out += text_tag(ruler_x, ruler_y - 7.0, "100 mm - escala 1:1 (mm)", 4.2, "#334155");

        std::string label = "PistaForge - " + arena.name;
        if (tiled) {
            label += " - pagina " + std::to_string(tile.column + 1) + "/" +
                     std::to_string(tile.columns) + " (coluna), " +
                     std::to_string(tile.row + 1) + "/" + std::to_string(tile.rows) +
                     " (linha)";
        }
        out += text_tag(tile.x + tile.width - 12.0, tile.y + tile.height - 6.0, label, 4.2,
                        "#334155", "end");

        if (tiled) {
            // Marcas de canto para alinhar a colagem.
            const double inset = 4.0;
            const double length = 7.0;
            const double x0 = tile.x;
            const double y0 = tile.y;
            const double x1 = tile.x + tile.width;
            const double y1 = tile.y + tile.height;
            out += line_tag(x0 + inset, y0 + inset, x0 + inset + length, y0 + inset, "#64748b",
                            0.2);
            out += line_tag(x0 + inset, y0 + inset, x0 + inset, y0 + inset + length, "#64748b",
                            0.2);
            out += line_tag(x1 - inset - length, y0 + inset, x1 - inset, y0 + inset, "#64748b",
                            0.2);
            out += line_tag(x1 - inset, y0 + inset, x1 - inset, y0 + inset + length, "#64748b",
                            0.2);
            out += line_tag(x0 + inset, y1 - inset, x0 + inset + length, y1 - inset, "#64748b",
                            0.2);
            out += line_tag(x0 + inset, y1 - inset - length, x0 + inset, y1 - inset, "#64748b",
                            0.2);
            out += line_tag(x1 - inset - length, y1 - inset, x1 - inset, y1 - inset, "#64748b",
                            0.2);
            out += line_tag(x1 - inset, y1 - inset - length, x1 - inset, y1 - inset, "#64748b",
                            0.2);

            out += rect_tag(tile.x, tile.y, tile.width, tile.height, "none", "#cbd5e1", 0.2);
        }
    }

    out += "</svg>\n";
    return out;
}

} // namespace

std::string to_svg(const Scene& scene, const SvgOptions& options) {
    const Tile tile{
        .row = 0,
        .column = 0,
        .rows = 1,
        .columns = 1,
        .x = 0.0,
        .y = 0.0,
        .width = scene.arena.width_mm,
        .height = scene.arena.height_mm,
    };
    return page_content(scene, options, tile, false);
}

std::vector<SvgPage> to_svg_pages(const Scene& scene, const SvgOptions& options) {
    std::vector<SvgPage> pages;
    for (const Tile& tile : layout_tiles(scene.arena, options)) {
        pages.push_back(SvgPage{
            .content = page_content(scene, options, tile, true),
            .row = tile.row,
            .column = tile.column,
            .rows = tile.rows,
            .columns = tile.columns,
            .origin_x_mm = tile.x,
            .origin_y_mm = tile.y,
            .width_mm = tile.width,
            .height_mm = tile.height,
        });
    }
    return pages;
}

} // namespace pf::io
