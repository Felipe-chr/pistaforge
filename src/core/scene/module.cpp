#include "core/scene/module.hpp"

#include <array>
#include <cmath>
#include <numbers>

#include "core/geometry/arc.hpp"

namespace pf {
namespace {

constexpr double kDefaultArm = 80.0;
constexpr double kDefaultRadius = 100.0;

const std::array<ModuleTemplate, 2> kTemplates{{
    ModuleTemplate{
        .id = "cross.90",
        .version = 1,
        .port_names = {"w", "e", "s", "n"},
        .pairs = {{"w", "e"}, {"s", "n"}},
        .defaults = {{"arm", kDefaultArm}, {"width", 19.0}},
    },
    ModuleTemplate{
        .id = "curve.90",
        .version = 1,
        .port_names = {"in", "out"},
        .pairs = {},
        .defaults = {{"radius", kDefaultRadius}, {"width", 19.0}},
    },
}};

} // namespace

const ModuleTemplate* find_module_template(std::string_view id) {
    for (const ModuleTemplate& candidate : kTemplates) {
        if (candidate.id == id) return &candidate;
    }
    return nullptr;
}

std::vector<ModulePortDef> module_local_ports(const ModuleInstance& instance) {
    if (instance.template_id == "cross.90") {
        const double arm = instance.param("arm", kDefaultArm);
        return {
            {"w", {-arm, 0.0}, {1.0, 0.0}},
            {"e", {arm, 0.0}, {1.0, 0.0}},
            {"s", {0.0, arm}, {0.0, 1.0}},
            {"n", {0.0, -arm}, {0.0, 1.0}},
        };
    }

    if (instance.template_id == "curve.90") {
        const double radius = instance.param("radius", kDefaultRadius);
        return {
            {"in", {radius, 0.0}, {0.0, 1.0}},
            {"out", {0.0, radius}, {-1.0, 0.0}},
        };
    }

    return {};
}

std::vector<std::pair<Vec2, Vec2>> module_local_segments(const ModuleInstance& instance) {
    if (instance.template_id == "cross.90") {
        const double arm = instance.param("arm", kDefaultArm);
        return {
            {{-arm, 0.0}, {arm, 0.0}},
            {{0.0, -arm}, {0.0, arm}},
        };
    }

    if (instance.template_id == "curve.90") {
        const double radius = instance.param("radius", kDefaultRadius);
        const ArcSegment arc{
            .center = {0.0, 0.0},
            .radius = radius,
            .start_angle = 0.0,
            .sweep_angle = std::numbers::pi / 2.0,
        };
        const std::vector<Vec2> points = arc.flatten(radius * 0.001);
        std::vector<std::pair<Vec2, Vec2>> segments;
        segments.reserve(points.size() > 1 ? points.size() - 1 : 0);
        for (std::size_t i = 1; i < points.size(); ++i) {
            segments.emplace_back(points[i - 1], points[i]);
        }
        return segments;
    }

    return {};
}

} // namespace pf
