#include <string>

#include <doctest/doctest.h>

#include "core/geometry/line.hpp"
#include "core/io/scene_obj.hpp"
#include "core/scene/scene.hpp"

using pf::LineSegment;
using pf::Scene;

namespace {

Scene make_obj_scene() {
    Scene scene;
    scene.arena.name = "Arena 3D";
    scene.arena.width_mm = 1200.0;
    scene.arena.height_mm = 800.0;
    scene.add_element(LineSegment{{0.0, 0.0}, {1000.0, 0.0}}, 20.0);
    return scene;
}

} // namespace

TEST_CASE("OBJ: malha da faixa, piso e linha central") {
    const Scene scene = make_obj_scene();
    const pf::io::ObjBundle bundle = pf::io::to_obj(scene);

    CHECK(bundle.obj.starts_with("# PistaForge - Arena 3D\n"));
    CHECK(bundle.obj.find("mtllib pista.mtl") != std::string::npos);
    CHECK(bundle.obj.find("o Pista") != std::string::npos);
    CHECK(bundle.obj.find("o Piso") != std::string::npos);
    CHECK(bundle.obj.find("o LinhaCentral") != std::string::npos);
    CHECK(bundle.obj.find("usemtl Pista") != std::string::npos);
    CHECK(bundle.vertices == 259);
    CHECK(bundle.faces == 204);
}

TEST_CASE("OBJ: coordenadas convertem mm para metros e altura vira Y") {
    const Scene scene = make_obj_scene();
    const pf::io::ObjBundle bundle = pf::io::to_obj(scene);

    CHECK(bundle.obj.find("v 0 0 -0.01\n") != std::string::npos);
    CHECK(bundle.obj.find("v 0 0 0.01\n") != std::string::npos);
    CHECK(bundle.obj.find("v 0 0.002 -0.01\n") != std::string::npos);
    CHECK(bundle.obj.find("v 1 0 -0.01\n") != std::string::npos);
}

TEST_CASE("OBJ: escala e opções de piso/linha central") {
    Scene scene = make_obj_scene();

    pf::io::ObjOptions options;
    options.scale = 1.0;
    options.floor = false;
    options.centerline = false;

    const pf::io::ObjBundle bundle = pf::io::to_obj(scene, options);
    CHECK(bundle.obj.find("v 0 0 -10\n") != std::string::npos);
    CHECK(bundle.obj.find("o Piso") == std::string::npos);
    CHECK(bundle.obj.find("o LinhaCentral") == std::string::npos);
    CHECK(bundle.vertices == 204);
    CHECK(bundle.faces == 202);
}

TEST_CASE("OBJ: materiais do Blender são gerados") {
    const Scene scene = make_obj_scene();
    const pf::io::ObjBundle bundle = pf::io::to_obj(scene);

    CHECK(bundle.mtl.find("newmtl Pista") != std::string::npos);
    CHECK(bundle.mtl.find("newmtl Piso") != std::string::npos);
    CHECK(bundle.mtl.find("newmtl Linha") != std::string::npos);
    CHECK(bundle.mtl.find("newmtl Marcador") != std::string::npos);
}
