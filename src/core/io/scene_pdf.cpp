#include "core/io/scene_pdf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "core/scene/module.hpp"
#include "core/sim/path_table.hpp"

namespace pf::io {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kMmToPt = 72.0 / 25.4;

struct Rgb {
    double r{0.0};
    double g{0.0};
    double b{0.0};
};

constexpr Rgb kInk{15.0 / 255.0, 23.0 / 255.0, 42.0 / 255.0};
constexpr Rgb kWhite{1.0, 1.0, 1.0};
constexpr Rgb kMarkerRed{185.0 / 255.0, 28.0 / 255.0, 28.0 / 255.0};
constexpr Rgb kAnnotation{71.0 / 255.0, 85.0 / 255.0, 105.0 / 255.0};
constexpr Rgb kGuide{124.0 / 255.0, 58.0 / 255.0, 237.0 / 255.0};
constexpr Rgb kGridMinor{226.0 / 255.0, 232.0 / 255.0, 240.0 / 255.0};
constexpr Rgb kGridMajor{203.0 / 255.0, 213.0 / 255.0, 225.0 / 255.0};
constexpr Rgb kMargin{148.0 / 255.0, 163.0 / 255.0, 184.0 / 255.0};
constexpr Rgb kBlue{29.0 / 255.0, 78.0 / 255.0, 216.0 / 255.0};
constexpr Rgb kGreen{21.0 / 255.0, 128.0 / 255.0, 61.0 / 255.0};
constexpr Rgb kRed{220.0 / 255.0, 38.0 / 255.0, 38.0 / 255.0};

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

/// Converte UTF-8 para os bytes aceitos pelo Helvetica com WinAnsiEncoding.
std::string pdf_text(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    std::size_t index = 0;
    while (index < utf8.size()) {
        const unsigned char first = static_cast<unsigned char>(utf8[index]);
        std::uint32_t code = '?';
        std::size_t length = 1;
        if (first < 0x80) {
            code = first;
        } else if ((first >> 5) == 0x6 && index + 1 < utf8.size()) {
            code = ((first & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[index + 1]) & 0x3Fu);
            length = 2;
        } else if ((first >> 4) == 0xE && index + 2 < utf8.size()) {
            code = ((first & 0x0Fu) << 12) |
                   ((static_cast<unsigned char>(utf8[index + 1]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(utf8[index + 2]) & 0x3Fu);
            length = 3;
        } else if ((first >> 3) == 0x1E && index + 3 < utf8.size()) {
            code = ((first & 0x07u) << 18) |
                   ((static_cast<unsigned char>(utf8[index + 1]) & 0x3Fu) << 12) |
                   ((static_cast<unsigned char>(utf8[index + 2]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(utf8[index + 3]) & 0x3Fu);
            length = 4;
        }
        index += length;

        if (code == '(' || code == ')' || code == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(code));
        } else if (code >= 0x20 && code <= 0x7E) {
            out.push_back(static_cast<char>(code));
        } else if (code >= 0xA0 && code <= 0xFF) {
            out.push_back(static_cast<char>(code));
        } else if (code == 0x2013 || code == 0x2014) {
            out.push_back('-');
        } else if (code == 0x2018 || code == 0x2019) {
            out.push_back('\'');
        } else if (code == 0x201C || code == 0x201D) {
            out.push_back('"');
        } else {
            out.push_back('?');
        }
    }
    return out;
}

double text_width_mm(std::string_view encoded, double size_mm, bool bold) {
    const double factor = bold ? 0.56 : 0.52;
    return static_cast<double>(encoded.size()) * size_mm * factor;
}

class Content {
public:
    void raw(std::string_view text) { data.append(text); }

    void line_width(double points) { data += fmt(points) + " w\n"; }
    void stroke_color(Rgb color) {
        data += fmt(color.r) + " " + fmt(color.g) + " " + fmt(color.b) + " RG\n";
    }
    void fill_color(Rgb color) {
        data += fmt(color.r) + " " + fmt(color.g) + " " + fmt(color.b) + " rg\n";
    }
    void move_to(double x, double y) { data += fmt(x) + " " + fmt(y) + " m\n"; }
    void line_to(double x, double y) { data += fmt(x) + " " + fmt(y) + " l\n"; }
    void close_path() { data += "h\n"; }
    void stroke() { data += "S\n"; }
    void fill() { data += "f\n"; }
    void rect(double x, double y, double width, double height) {
        data += fmt(x) + " " + fmt(y) + " " + fmt(width) + " " + fmt(height) + " re\n";
    }
    void save() { data += "q\n"; }
    void restore() { data += "Q\n"; }
    void set_dash(double on, double off) {
        data += "[" + fmt(on) + " " + fmt(off) + "] 0 d\n";
    }
    void solid() { data += "[] 0 d\n"; }
    void text(double x, double y, double size_pt, Rgb color, std::string_view encoded,
              bool bold) {
        data += "BT\n";
        data += std::string{"/"} + (bold ? "F2" : "F1") + " " + fmt(size_pt) + " Tf\n";
        data += fmt(color.r) + " " + fmt(color.g) + " " + fmt(color.b) + " rg\n";
        data += fmt(x) + " " + fmt(y) + " Td\n";
        data += "(" + std::string{encoded} + ") Tj\nET\n";
    }

    std::string data;
};

struct Mapper {
    double origin_x_mm{0.0};
    double origin_y_mm{0.0};
    double page_height_mm{0.0};
    double margin_mm{0.0};

    [[nodiscard]] double x(double scene_x) const {
        return (margin_mm + scene_x - origin_x_mm) * kMmToPt;
    }
    [[nodiscard]] double y(double scene_y) const {
        return (page_height_mm - (margin_mm + scene_y - origin_y_mm)) * kMmToPt;
    }
    [[nodiscard]] static double mm(double value) { return value * kMmToPt; }
};

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

std::vector<Tile> layout_tiles(const Arena& arena, const PdfOptions& options) {
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

std::vector<std::vector<Vec2>> element_polylines(const Element& element, double tolerance) {
    if (const auto* line = std::get_if<LineSegment>(&element.shape)) {
        return {{line->a, line->b}};
    }
    if (const auto* module = std::get_if<ModuleInstance>(&element.shape)) {
        std::vector<std::vector<Vec2>> result;
        for (const auto& [local_a, local_b] : module_local_segments(*module)) {
            const Vec2 a = module->transform.apply(local_a);
            const Vec2 b = module->transform.apply(local_b);
            if (!result.empty() && distance(result.back().back(), a) < 1e-6) {
                result.back().push_back(b);
            } else {
                result.push_back({a, b});
            }
        }
        return result;
    }
    return {shape_flatten(element.shape, tolerance)};
}

void stroke_polyline(Content& content, const Mapper& mapper, const std::vector<Vec2>& points,
                     double width_mm, Rgb color, bool dashed = false) {
    if (points.size() < 2) return;
    content.save();
    content.line_width(Mapper::mm(std::max(0.01, width_mm)));
    content.stroke_color(color);
    content.raw("1 J\n1 j\n");
    if (dashed) {
        content.set_dash(Mapper::mm(4.0), Mapper::mm(3.0));
    } else {
        content.solid();
    }
    content.move_to(mapper.x(points.front().x), mapper.y(points.front().y));
    for (std::size_t i = 1; i < points.size(); ++i) {
        content.line_to(mapper.x(points[i].x), mapper.y(points[i].y));
    }
    content.stroke();
    content.restore();
}

void fill_polygon(Content& content, const Mapper& mapper, const std::vector<Vec2>& points,
                  Rgb fill, Rgb stroke_color, double stroke_width_mm) {
    if (points.size() < 3) return;
    content.save();
    content.fill_color(fill);
    content.stroke_color(stroke_color);
    content.line_width(Mapper::mm(std::max(0.0, stroke_width_mm)));
    content.move_to(mapper.x(points.front().x), mapper.y(points.front().y));
    for (std::size_t i = 1; i < points.size(); ++i) {
        content.line_to(mapper.x(points[i].x), mapper.y(points[i].y));
    }
    content.close_path();
    content.raw(stroke_width_mm > 0.0 ? "B\n" : "f\n");
    content.restore();
}

void draw_circle(Content& content, const Mapper& mapper, Vec2 center, double radius_mm,
                 double width_mm, Rgb color) {
    constexpr int kSteps = 20;
    std::vector<Vec2> points;
    points.reserve(kSteps + 1);
    for (int step = 0; step <= kSteps; ++step) {
        const double angle = 2.0 * kPi * static_cast<double>(step) / static_cast<double>(kSteps);
        points.push_back(center + Vec2{std::cos(angle), std::sin(angle)} * radius_mm);
    }
    stroke_polyline(content, mapper, points, width_mm, color);
}

void draw_element(Content& content, const Mapper& mapper, const Element& element,
                  const PdfOptions& options) {
    Rgb color = kInk;
    double width = element.width_mm;
    bool dashed = false;

    switch (element.layer) {
        case LayerId::Track:
            color = kInk;
            width = options.centerline ? options.path_width_mm : element.width_mm;
            break;
        case LayerId::Markers: color = kMarkerRed; break;
        case LayerId::Annotations: color = kAnnotation; break;
        case LayerId::Guides:
            color = kGuide;
            dashed = true;
            width = std::max(0.35, element.width_mm * 0.15);
            break;
    }

    for (const std::vector<Vec2>& polyline : element_polylines(element, 0.15)) {
        stroke_polyline(content, mapper, polyline, width, color, dashed);
    }
}

std::vector<Vec2> rotated_box(Vec2 center, double angle, double half_width, double half_height) {
    const Vec2 corners[] = {
        {-half_width, -half_height},
        {half_width, -half_height},
        {half_width, half_height},
        {-half_width, half_height},
    };
    std::vector<Vec2> points;
    points.reserve(4);
    for (const Vec2 corner : corners) {
        points.push_back(center + corner.rotated(angle));
    }
    return points;
}

void draw_marker(Content& content, const Mapper& mapper, const Marker& marker) {
    switch (marker.type) {
        case MarkerType::Start:
        case MarkerType::Finish:
        case MarkerType::StartFinish: {
            constexpr double kLength = 60.0;
            constexpr double kThickness = 12.0;
            constexpr double kCell = 6.0;
            constexpr int kColumns = 10;
            constexpr int kRows = 2;

            fill_polygon(content, mapper,
                         rotated_box(marker.position, marker.angle, kLength / 2.0,
                                     kThickness / 2.0),
                         kWhite, kInk, 0.6);

            for (int row = 0; row < kRows; ++row) {
                for (int column = 0; column < kColumns; ++column) {
                    if ((row + column) % 2 != 0) continue;
                    const Vec2 local{-kLength / 2.0 + (column + 0.5) * kCell,
                                     -kThickness / 2.0 + (row + 0.5) * kCell};
                    fill_polygon(content, mapper,
                                 rotated_box(marker.position + local.rotated(marker.angle),
                                             marker.angle, kCell / 2.0, kCell / 2.0),
                                 kInk, kInk, 0.0);
                }
            }

            const Rgb color = marker.type == MarkerType::Start     ? kGreen
                              : marker.type == MarkerType::Finish  ? kRed
                                                                   : kBlue;
            const std::string label = marker.type == MarkerType::Start     ? "LARGADA"
                                      : marker.type == MarkerType::Finish  ? "CHEGADA"
                                                                           : "LARGADA / CHEGADA";
            const std::string encoded = pdf_text(label);
            content.text(mapper.x(marker.position.x + 8.0),
                         mapper.y(marker.position.y - 9.0), Mapper::mm(5.5), color, encoded,
                         true);
            return;
        }
        case MarkerType::DirectionArrow: {
            const Vec2 local[] = {{0.0, -5.0}, {13.0, 0.0}, {0.0, 5.0}};
            std::vector<Vec2> points;
            points.reserve(3);
            for (const Vec2 point : local) {
                points.push_back(marker.position + point.rotated(marker.angle));
            }
            fill_polygon(content, mapper, points, kRed, kRed, 0.0);
            return;
        }
        case MarkerType::Text:
            break;
    }

    if (marker.text.empty()) return;
    content.text(mapper.x(marker.position.x + 4.0), mapper.y(marker.position.y),
                 Mapper::mm(5.0), kMarkerRed, pdf_text(marker.text), false);
}

void draw_floor_marks(Content& content, const Mapper& mapper, const PathTable& table,
                      const PdfPage& page) {
    for (std::size_t index = 0; index < table.samples.size(); ++index) {
        const PathSample& sample = table.samples[index];
        const double pad = 8.0;
        if (sample.position.x < page.origin_x_mm - pad ||
            sample.position.x > page.origin_x_mm + page.width_mm + pad ||
            sample.position.y < page.origin_y_mm - pad ||
            sample.position.y > page.origin_y_mm + page.height_mm + pad) {
            continue;
        }

        const double heading = sample.heading_deg * kPi / 180.0;
        const Vec2 direction{std::cos(heading), std::sin(heading)};

        stroke_polyline(content, mapper,
                        {sample.position + Vec2{-4.0, 0.0}, sample.position + Vec2{4.0, 0.0}},
                        0.35, kBlue);
        stroke_polyline(content, mapper,
                        {sample.position + Vec2{0.0, -4.0}, sample.position + Vec2{0.0, 4.0}},
                        0.35, kBlue);
        draw_circle(content, mapper, sample.position, 2.0, 0.3, kBlue);
        stroke_polyline(content, mapper,
                        {sample.position, sample.position + direction * 8.0}, 0.35, kBlue);

        const bool first = index == 0;
        const bool last = index + 1 == table.samples.size();
        const Rgb accent = first ? kGreen : last ? kRed : kBlue;

        std::string label = "P" + std::to_string(index + 1);
        if (first) label += " INICIO";
        if (last && !first) label += " FIM";
        const std::string encoded_label = pdf_text(label);

        const double label_size = 3.8;
        const double label_width = text_width_mm(encoded_label, label_size, true);
        double label_x = sample.position.x + 6.0;
        if (label_x + label_width > page.origin_x_mm + page.width_mm - 2.0) {
            label_x = sample.position.x - 6.0 - label_width;
        }
        double label_y = sample.position.y - 6.0;
        if (label_y < page.origin_y_mm + 5.0) {
            label_y = sample.position.y + 10.0;
        }

        content.text(mapper.x(label_x), mapper.y(label_y), Mapper::mm(label_size), accent,
                     encoded_label, true);

        const std::string coords = "x=" + fmt(std::round(sample.position.x)) +
                                   " y=" + fmt(std::round(sample.position.y));
        content.text(mapper.x(label_x), mapper.y(label_y + 4.0), Mapper::mm(3.2), accent,
                     pdf_text(coords), false);
    }
}

void draw_calibration(Content& content, const Mapper& mapper, const Scene& scene,
                      const PdfOptions& options, const PdfPage& page) {
    const double ruler_x = page.origin_x_mm + 14.0;
    const double ruler_y = page.origin_y_mm + page.height_mm - 12.0;

    stroke_polyline(content, mapper,
                    {{ruler_x, ruler_y}, {ruler_x + 100.0, ruler_y}}, 0.35, kInk);
    for (int step = 0; step <= 100; step += 10) {
        const double tick = step % 50 == 0 ? 4.5 : 2.5;
        stroke_polyline(content, mapper,
                        {{ruler_x + step, ruler_y}, {ruler_x + step, ruler_y - tick}}, 0.35,
                        kInk);
    }
    content.text(mapper.x(ruler_x), mapper.y(ruler_y - 7.0), Mapper::mm(4.2), kAnnotation,
                 pdf_text("100 mm - escala 1:1 (mm)"), false);

    std::string label = "PistaForge - " + scene.arena.name;
    if (options.tiled) {
        label += " - pagina " + std::to_string(page.column + 1) + "/" +
                 std::to_string(page.columns) + " (coluna), " +
                 std::to_string(page.row + 1) + "/" + std::to_string(page.rows) + " (linha)";
    }
    const std::string encoded = pdf_text(label);
    const double width = text_width_mm(encoded, 4.2, false);
    content.text(mapper.x(page.origin_x_mm + page.width_mm - 12.0 - width),
                 mapper.y(page.origin_y_mm + page.height_mm - 6.0), Mapper::mm(4.2),
                 kAnnotation, encoded, false);

    if (!options.tiled) return;

    const double inset = 4.0;
    const double length = 7.0;
    const double x0 = page.origin_x_mm;
    const double y0 = page.origin_y_mm;
    const double x1 = page.origin_x_mm + page.width_mm;
    const double y1 = page.origin_y_mm + page.height_mm;

    const std::vector<std::pair<Vec2, Vec2>> marks{
        {{x0 + inset, y0 + inset}, {x0 + inset + length, y0 + inset}},
        {{x0 + inset, y0 + inset}, {x0 + inset, y0 + inset + length}},
        {{x1 - inset - length, y0 + inset}, {x1 - inset, y0 + inset}},
        {{x1 - inset, y0 + inset}, {x1 - inset, y0 + inset + length}},
        {{x0 + inset, y1 - inset}, {x0 + inset + length, y1 - inset}},
        {{x0 + inset, y1 - inset - length}, {x0 + inset, y1 - inset}},
        {{x1 - inset - length, y1 - inset}, {x1 - inset, y1 - inset}},
        {{x1 - inset, y1 - inset - length}, {x1 - inset, y1 - inset}},
    };
    for (const auto& [a, b] : marks) {
        stroke_polyline(content, mapper, {a, b}, 0.2, kMargin);
    }
    stroke_polyline(content, mapper,
                    {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}, 0.2, kGridMajor);
}

std::string page_content(const Scene& scene, const PdfOptions& options, const Tile& tile,
                         const PathTable& table) {
    const Arena& arena = scene.arena;
    const double page_height_mm = tile.height + 2.0 * options.margin_mm;

    PdfPage page;
    page.row = tile.row;
    page.column = tile.column;
    page.rows = tile.rows;
    page.columns = tile.columns;
    page.origin_x_mm = tile.x;
    page.origin_y_mm = tile.y;
    page.width_mm = tile.width;
    page.height_mm = tile.height;

    const Mapper mapper{
        .origin_x_mm = tile.x,
        .origin_y_mm = tile.y,
        .page_height_mm = page_height_mm,
        .margin_mm = options.margin_mm,
    };

    Content content;

    // Fundo da página.
    content.save();
    content.fill_color(kWhite);
    content.rect(mapper.x(tile.x), mapper.y(tile.y + tile.height),
                 Mapper::mm(tile.width), Mapper::mm(tile.height));
    content.fill();
    content.restore();

    // Arena e margem de segurança.
    content.save();
    content.fill_color(kWhite);
    content.stroke_color(kInk);
    content.line_width(Mapper::mm(1.2));
    content.rect(mapper.x(0.0), mapper.y(arena.height_mm), Mapper::mm(arena.width_mm),
                 Mapper::mm(arena.height_mm));
    content.raw("B\n");
    content.restore();

    if (arena.margin_mm > 0.0) {
        content.save();
        content.stroke_color(kMargin);
        content.line_width(Mapper::mm(0.4));
        content.set_dash(Mapper::mm(4.0), Mapper::mm(3.0));
        content.rect(mapper.x(arena.margin_mm), mapper.y(arena.height_mm - arena.margin_mm),
                     Mapper::mm(arena.width_mm - arena.margin_mm * 2.0),
                     Mapper::mm(arena.height_mm - arena.margin_mm * 2.0));
        content.stroke();
        content.restore();
    }

    // Grade de referência.
    if (options.grid) {
        for (double x = 100.0; x < arena.width_mm - 1e-9; x += 100.0) {
            const bool major = std::fmod(x, 500.0) < 1e-9;
            stroke_polyline(content, mapper, {{x, 0.0}, {x, arena.height_mm}},
                            major ? 0.3 : 0.18, major ? kGridMajor : kGridMinor);
        }
        for (double y = 100.0; y < arena.height_mm - 1e-9; y += 100.0) {
            const bool major = std::fmod(y, 500.0) < 1e-9;
            stroke_polyline(content, mapper, {{0.0, y}, {arena.width_mm, y}},
                            major ? 0.3 : 0.18, major ? kGridMajor : kGridMinor);
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
        draw_element(content, mapper, element, options);
    }

    // Marcadores.
    if (scene.layers.markers) {
        for (const Marker& marker : scene.markers) draw_marker(content, mapper, marker);
    }

    // Guias.
    if (scene.layers.guides || options.guides) {
        for (const Guide& guide : scene.guides) {
            stroke_polyline(content, mapper, {guide.a, guide.b}, 0.35, kGuide, true);
        }
    }

    // Pontos numerados para desenhar no chão.
    if (options.floor_marks && !table.samples.empty()) {
        draw_floor_marks(content, mapper, table, page);
    }

    // Cotas da arena.
    if (arena.show_dimensions) {
        const std::string dimensions = fmt(arena.width_mm) + " x " + fmt(arena.height_mm) + " mm";
        const std::string encoded = pdf_text(dimensions);
        const double width = text_width_mm(encoded, 5.0, false);
        content.text(mapper.x(arena.width_mm - 4.0 - width),
                     mapper.y(arena.height_mm - 4.0), Mapper::mm(5.0), kAnnotation, encoded,
                     false);
    }

    // Calibração e rótulo da página.
    if (options.calibration) {
        draw_calibration(content, mapper, scene, options, page);
    }

    return content.data;
}

std::string assemble_pdf(const std::vector<PdfPage>& pages, const PdfOptions& options) {
    const std::size_t page_count = pages.size();
    const std::size_t font_regular = 3 + page_count * 2;
    const std::size_t font_bold = font_regular + 1;
    const std::size_t object_count = font_bold;

    std::vector<std::string> objects(object_count);
    objects[0] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::string kids;
    for (std::size_t i = 0; i < page_count; ++i) {
        if (i > 0) kids += " ";
        kids += std::to_string(3 + i * 2) + " 0 R";
    }
    objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(page_count) +
                 " >>";

    for (std::size_t i = 0; i < page_count; ++i) {
        const PdfPage& page = pages[i];
        const double media_width = page.width_mm + 2.0 * options.margin_mm;
        const double media_height = page.height_mm + 2.0 * options.margin_mm;
        const std::size_t page_object = 3 + i * 2;
        const std::size_t content_object = page_object + 1;

        objects[page_object - 1] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
                                   fmt(media_width * kMmToPt) + " " +
                                   fmt(media_height * kMmToPt) + "] /Resources << /Font << /F1 " +
                                   std::to_string(font_regular) + " 0 R /F2 " +
                                   std::to_string(font_bold) +
                                   " 0 R >> >> /Contents " + std::to_string(content_object) +
                                   " 0 R >>";

        objects[content_object - 1] = "<< /Length " + std::to_string(page.content.size()) +
                                      " >>\nstream\n" + page.content + "\nendstream";
    }

    objects[font_regular - 1] =
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>";
    objects[font_bold - 1] =
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>";

    std::string out;
    out.reserve(4096 + page_count * 4096);
    out += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";

    std::vector<std::size_t> offsets;
    offsets.reserve(object_count);
    for (std::size_t i = 0; i < object_count; ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const std::size_t xref_offset = out.size();
    out += "xref\n0 " + std::to_string(object_count + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%010zu 00000 n \n", offset);
        out += buffer;
    }
    out += "trailer\n<< /Size " + std::to_string(object_count + 1) + " /Root 1 0 R >>\n";
    out += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
    return out;
}

} // namespace

std::vector<PdfPage> to_pdf_pages(const Scene& scene, const PdfOptions& options) {
    PathTable table;
    if (options.floor_marks) {
        PathTableOptions path_options;
        path_options.step_mm = std::max(1.0, options.mark_step_mm);
        path_options.tolerance_mm = 0.25;
        table = build_path_table(scene, path_options);
    }

    std::vector<PdfPage> pages;
    for (const Tile& tile : layout_tiles(scene.arena, options)) {
        PdfPage page;
        page.row = tile.row;
        page.column = tile.column;
        page.rows = tile.rows;
        page.columns = tile.columns;
        page.origin_x_mm = tile.x;
        page.origin_y_mm = tile.y;
        page.width_mm = tile.width;
        page.height_mm = tile.height;
        page.content = page_content(scene, options, tile, table);
        pages.push_back(std::move(page));
    }
    return pages;
}

std::string to_pdf(const Scene& scene, const PdfOptions& options) {
    return assemble_pdf(to_pdf_pages(scene, options), options);
}

} // namespace pf::io
