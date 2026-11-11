#include "core/io/scene_obj.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "core/sim/path_table.hpp"

namespace pf::io {
namespace {

constexpr double kPi = std::numbers::pi;

std::string fmt(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    std::string text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text;
}

struct MeshWriter {
    std::string obj;
    int vertex_count{0};
    int face_count{0};

    int add_vertex(Vec2 point, double height_mm, double scale) {
        // Cena (x, y, altura) -> OBJ (x, altura, y). O Blender importa com Y up.
        obj += "v " + fmt(point.x * scale) + " " + fmt(height_mm * scale) + " " +
               fmt(point.y * scale) + "\n";
        return ++vertex_count;
    }

    void add_face(int a, int b, int c, int d) {
        obj += "f " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) +
               " " + std::to_string(d) + "\n";
        ++face_count;
    }

    void add_triangle(int a, int b, int c) {
        obj += "f " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) +
               "\n";
        ++face_count;
    }
};

void append_marker_box(MeshWriter& mesh, const Marker& marker, const ObjOptions& options) {
    constexpr double kHalfLength = 30.0;
    constexpr double kHalfThickness = 6.0;
    constexpr double kMarkerHeight = 1.5;

    const Vec2 corners[] = {
        {-kHalfLength, -kHalfThickness},
        {kHalfLength, -kHalfThickness},
        {kHalfLength, kHalfThickness},
        {-kHalfLength, kHalfThickness},
    };

    int bottom[4]{};
    int top[4]{};
    for (int i = 0; i < 4; ++i) {
        const Vec2 point = marker.position + corners[i].rotated(marker.angle);
        bottom[i] = mesh.add_vertex(point, 0.0, options.scale);
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 point = marker.position + corners[i].rotated(marker.angle);
        top[i] = mesh.add_vertex(point, kMarkerHeight, options.scale);
    }

    mesh.add_face(top[0], top[1], top[2], top[3]);
    mesh.add_face(bottom[3], bottom[2], bottom[1], bottom[0]);
    mesh.add_face(bottom[0], bottom[1], top[1], top[0]);
    mesh.add_face(bottom[1], bottom[2], top[2], top[1]);
    mesh.add_face(bottom[2], bottom[3], top[3], top[2]);
    mesh.add_face(bottom[3], bottom[0], top[0], top[3]);
}

} // namespace

ObjBundle to_obj(const Scene& scene, const ObjOptions& options) {
    ObjBundle bundle;

    PathTableOptions path_options;
    path_options.step_mm = std::max(2.0, options.step_mm);
    path_options.tolerance_mm = 0.1;
    const PathTable table = build_path_table(scene, path_options);

    const double scale = std::max(1e-6, options.scale);
    const double height = std::max(0.05, options.height_mm);

    MeshWriter mesh;
    mesh.obj += "# PistaForge - " + scene.arena.name + "\n";
    mesh.obj += "# Arena: " + fmt(scene.arena.width_mm) + " x " +
                fmt(scene.arena.height_mm) + " mm\n";
    mesh.obj += "# 1 unidade = " + fmt(1.0 / scale) + " mm\n";
    mesh.obj += "mtllib " + options.mtl_name + "\n";
    mesh.obj += "o Pista\nusemtl Pista\n";

    if (table.samples.size() >= 2) {
        const std::size_t count = table.samples.size();
        std::vector<int> bottom_left(count);
        std::vector<int> bottom_right(count);
        std::vector<int> top_left(count);
        std::vector<int> top_right(count);

        for (std::size_t i = 0; i < count; ++i) {
            const PathSample& sample = table.samples[i];
            const double heading = sample.heading_deg * kPi / 180.0;
            const Vec2 normal{-std::sin(heading), std::cos(heading)};
            const Vec2 left = sample.position - normal * (sample.width_mm * 0.5);
            const Vec2 right = sample.position + normal * (sample.width_mm * 0.5);

            bottom_left[i] = mesh.add_vertex(left, 0.0, scale);
            bottom_right[i] = mesh.add_vertex(right, 0.0, scale);
            top_left[i] = mesh.add_vertex(left, height, scale);
            top_right[i] = mesh.add_vertex(right, height, scale);
        }

        for (std::size_t i = 0; i + 1 < count; ++i) {
            mesh.add_face(top_left[i], top_right[i], top_right[i + 1], top_left[i + 1]);
            mesh.add_face(bottom_left[i + 1], bottom_right[i + 1], bottom_right[i],
                          bottom_left[i]);
            mesh.add_face(bottom_left[i], top_left[i], top_left[i + 1], bottom_left[i + 1]);
            mesh.add_face(bottom_right[i + 1], top_right[i + 1], top_right[i], bottom_right[i]);
        }

        if (!table.closed) {
            mesh.add_face(bottom_left.front(), bottom_right.front(), top_right.front(),
                          top_left.front());
            mesh.add_face(top_left.back(), top_right.back(), bottom_right.back(),
                          bottom_left.back());
        }

        if (options.centerline) {
            mesh.obj += "o LinhaCentral\nusemtl Linha\n";
            std::vector<int> center(count);
            for (std::size_t i = 0; i < count; ++i) {
                center[i] = mesh.add_vertex(table.samples[i].position, height + 0.2, scale);
            }
            mesh.obj += "l";
            for (const int index : center) mesh.obj += " " + std::to_string(index);
            mesh.obj += "\n";
        }
    }

    if (options.floor) {
        mesh.obj += "o Piso\nusemtl Piso\n";
        const double floor_height = -0.1;
        const int a = mesh.add_vertex({0.0, 0.0}, floor_height, scale);
        const int b = mesh.add_vertex({scene.arena.width_mm, 0.0}, floor_height, scale);
        const int c = mesh.add_vertex({scene.arena.width_mm, scene.arena.height_mm},
                                      floor_height, scale);
        const int d = mesh.add_vertex({0.0, scene.arena.height_mm}, floor_height, scale);
        mesh.add_triangle(a, b, c);
        mesh.add_triangle(a, c, d);
    }

    bool has_marker = false;
    for (const Marker& marker : scene.markers) {
        if (marker.type != MarkerType::Start && marker.type != MarkerType::Finish &&
            marker.type != MarkerType::StartFinish) {
            continue;
        }
        if (!has_marker) {
            mesh.obj += "o Marcadores\nusemtl Marcador\n";
            has_marker = true;
        }
        append_marker_box(mesh, marker, options);
    }

    bundle.obj = std::move(mesh.obj);
    bundle.vertices = mesh.vertex_count;
    bundle.faces = mesh.face_count;

    bundle.mtl += "# PistaForge - materiais\n";
    bundle.mtl += "newmtl Pista\nKd 0.06 0.09 0.16\nKa 0 0 0\nKs 0.15 0.15 0.15\nNs 30\n"
                  "d 1\nillum 2\n\n";
    bundle.mtl += "newmtl Piso\nKd 0.90 0.92 0.95\nKa 0 0 0\nKs 0 0 0\nNs 1\n"
                  "d 1\nillum 1\n\n";
    bundle.mtl += "newmtl Linha\nKd 0.12 0.35 0.85\nKa 0 0 0\nKs 0 0 0\nNs 1\n"
                  "d 1\nillum 1\n\n";
    bundle.mtl += "newmtl Marcador\nKd 0.85 0.15 0.15\nKa 0 0 0\nKs 0 0 0\nNs 1\n"
                  "d 1\nillum 1\n";

    return bundle;
}

} // namespace pf::io
