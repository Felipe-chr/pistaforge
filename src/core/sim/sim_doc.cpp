#include "core/sim/sim_doc.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/analysis/track_analysis.hpp"
#include "core/sim/path_table.hpp"

namespace pf::sim {
namespace {

using nlohmann::json;

json arena_json(const Arena& arena) {
    return json{
        {"name", arena.name},
        {"width_mm", arena.width_mm},
        {"height_mm", arena.height_mm},
        {"margin_mm", arena.margin_mm},
    };
}

json point_json(const PathSample& sample, std::size_t index) {
    return json{
        {"i", index},
        {"s_mm", sample.s_mm},
        {"x_mm", sample.position.x},
        {"y_mm", sample.position.y},
        {"heading_deg", sample.heading_deg},
        {"curvature_1_mm", sample.curvature},
        {"width_mm", sample.width_mm},
        {"element", sample.element},
    };
}

json segment_json(const PathSegmentInfo& segment) {
    return json{
        {"element", segment.element},
        {"start_s_mm", segment.start_s_mm},
        {"length_mm", segment.length_mm},
        {"reversed", segment.reversed},
    };
}

json critical_json(const CriticalPoint& point) {
    return json{
        {"kind", std::string{to_string(point.kind)}},
        {"x_mm", point.position.x},
        {"y_mm", point.position.y},
        {"element", point.element},
        {"severity", point.severity},
        {"t", point.t},
        {"angle_deg", point.angle_deg},
        {"radius_mm", point.radius_mm},
    };
}

json metrics_json(const TrackMetrics& metrics) {
    return json{
        {"total_length_mm", metrics.total_length_mm},
        {"straight_length_mm", metrics.straight_length_mm},
        {"curve_length_mm", metrics.curve_length_mm},
        {"straight_count", metrics.straight_count},
        {"curve_count", metrics.curve_count},
        {"crossing_count", metrics.crossing_count},
        {"special_count", metrics.special_count},
        {"element_count", metrics.element_count},
        {"connection_count", metrics.connection_count},
        {"sharp_turn_count", metrics.sharp_turn_count},
        {"tight_curve_count", metrics.tight_curve_count},
        {"difficulty", metrics.difficulty},
        {"difficulty_label", metrics.difficulty_label},
        {"estimated_time_s", metrics.estimated_time_s},
        {"recommended_speed_mps", metrics.recommended_speed_mps},
    };
}

} // namespace

std::string to_sim_json(const Scene& scene, const SimDocOptions& options) {
    return build_sim_document(scene, options).json;
}

SimDocument build_sim_document(const Scene& scene, const SimDocOptions& options) {
    PathTableOptions table_options;
    table_options.step_mm = options.step_mm;
    table_options.tolerance_mm = options.tolerance_mm;

    PathTable table = build_path_table(scene, table_options);
    const TrackAnalysis analysis = analyze_track(scene);

    json points = json::array();
    for (std::size_t i = 0; i < table.samples.size(); ++i) {
        points.push_back(point_json(table.samples[i], i));
    }

    json segments = json::array();
    for (const PathSegmentInfo& segment : table.segments) {
        segments.push_back(segment_json(segment));
    }

    json critical_points = json::array();
    for (const CriticalPoint& point : analysis.critical_points) {
        critical_points.push_back(critical_json(point));
    }

    json generator{{"name", "PistaForge"}};
    if (!options.generator_version.empty()) {
        generator["version"] = options.generator_version;
    }

    json document{
        {"format", std::string{kSimFormat}},
        {"schema", kSimSchema},
        {"generator", std::move(generator)},
        {"units",
         {
             {"length", "mm"},
             {"angle", "deg"},
             {"curvature", "1/mm"},
         }},
        {"coordinate_system",
         {
             {"origin", "top-left"},
             {"x", "right"},
             {"y", "down"},
             {"heading_zero", "+x"},
             {"heading_positive", "clockwise"},
         }},
        {"arena", arena_json(scene.arena)},
        {"path",
         {
             {"closed", table.closed},
             {"ordered", table.ordered},
             {"step_mm", table.step_mm},
             {"total_length_mm", table.total_length_mm},
             {"point_count", table.samples.size()},
             {"start_element", table.start_element},
             {"points", std::move(points)},
             {"segments", std::move(segments)},
         }},
        {"metrics", metrics_json(analysis.metrics)},
        {"critical_points", std::move(critical_points)},
    };

    return SimDocument{std::move(table), document.dump(2)};
}

} // namespace pf::sim
