#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/geometry/line.hpp"
#include "core/io/scene_pdf.hpp"
#include "core/scene/scene.hpp"

using pf::LineSegment;
using pf::Scene;

namespace {

Scene make_pdf_scene() {
    Scene scene;
    scene.arena.name = "Arena PDF";
    scene.arena.width_mm = 400.0;
    scene.arena.height_mm = 300.0;
    scene.arena.margin_mm = 10.0;
    scene.add_element(LineSegment{{50.0, 50.0}, {350.0, 50.0}}, 19.0);
    return scene;
}

} // namespace

TEST_CASE("PDF: envelope válido e uma página para a arena inteira") {
    const Scene scene = make_pdf_scene();

    pf::io::PdfOptions options;
    options.page_width_mm = scene.arena.width_mm;
    options.page_height_mm = scene.arena.height_mm;
    options.margin_mm = 10.0;

    const std::string pdf = pf::io::to_pdf(scene, options);

    CHECK(pdf.starts_with("%PDF-1.4\n"));
    CHECK(pdf.ends_with("%%EOF\n"));
    CHECK(pdf.find("/Type /Catalog") != std::string::npos);
    CHECK(pdf.find("MediaBox [0 0 1190.551 907.087]") != std::string::npos);
    CHECK(pdf.find("/BaseFont /Helvetica") != std::string::npos);
    CHECK(pdf.find("/BaseFont /Helvetica-Bold") != std::string::npos);
    CHECK(pdf.find("startxref") != std::string::npos);
}

TEST_CASE("PDF: pontos numerados para desenhar no chão") {
    Scene scene = make_pdf_scene();
    scene.elements.front().shape = LineSegment{{50.0, 50.0}, {1050.0, 50.0}};
    scene.arena.width_mm = 1200.0;

    pf::io::PdfOptions options;
    options.mark_step_mm = 500.0;

    const std::string pdf = pf::io::to_pdf(scene, options);

    CHECK(pdf.find("(P1 INICIO) Tj") != std::string::npos);
    CHECK(pdf.find("(P2) Tj") != std::string::npos);
    CHECK(pdf.find("(P3 FIM) Tj") != std::string::npos);
    CHECK(pdf.find("(x=50 y=50) Tj") != std::string::npos);
}

TEST_CASE("PDF: marcas podem ser desligadas") {
    const Scene scene = make_pdf_scene();

    pf::io::PdfOptions options;
    options.floor_marks = false;

    const std::string pdf = pf::io::to_pdf(scene, options);
    CHECK(pdf.find("(P1") == std::string::npos);
}

TEST_CASE("PDF: modo linha central reduz a espessura da faixa") {
    const Scene scene = make_pdf_scene();

    pf::io::PdfOptions full;
    full.floor_marks = false;
    const std::string full_pdf = pf::io::to_pdf(scene, full);
    CHECK(full_pdf.find("53.858 w") != std::string::npos);

    pf::io::PdfOptions centerline;
    centerline.floor_marks = false;
    centerline.centerline = true;
    const std::string centerline_pdf = pf::io::to_pdf(scene, centerline);
    CHECK(centerline_pdf.find("53.858 w") == std::string::npos);
    CHECK(centerline_pdf.find("2.835 w") != std::string::npos);
}

TEST_CASE("PDF: mosaico calcula páginas e rótulos") {
    Scene scene;
    scene.arena.width_mm = 350.0;
    scene.arena.height_mm = 500.0;

    pf::io::PdfOptions options;
    options.tiled = true;

    const std::vector<pf::io::PdfPage> pages = pf::io::to_pdf_pages(scene, options);
    REQUIRE(pages.size() == 4);
    CHECK(pages.front().columns == 2);
    CHECK(pages.front().rows == 2);
    CHECK(pages.front().content.find("pagina 1/2") != std::string::npos);

    const std::string pdf = pf::io::to_pdf(scene, options);
    std::size_t count = 0;
    std::size_t cursor = 0;
    while ((cursor = pdf.find("/Type /Page ", cursor)) != std::string::npos) {
        ++count;
        cursor += 12;
    }
    CHECK(count == 4);
}
