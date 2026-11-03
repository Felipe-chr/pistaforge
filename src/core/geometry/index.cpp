#include "core/geometry/index.hpp"

#include <algorithm>
#include <cmath>

#include "core/scene/scene.hpp"

namespace pf {
namespace {

constexpr std::int32_t kMaxCellCoordinate = 1'000'000;
constexpr std::int64_t kMaxCellsPerElement = 4096;

std::int32_t clamp_coordinate(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    const double clamped = std::clamp(value, -static_cast<double>(kMaxCellCoordinate),
                                      static_cast<double>(kMaxCellCoordinate));
    return static_cast<std::int32_t>(clamped);
}

} // namespace

SpatialIndex::SpatialIndex(double cell_size_mm)
    : cell_size_mm_(std::max(cell_size_mm, 1.0)) {}

void SpatialIndex::clear() {
    cells_.clear();
    large_.clear();
}

SpatialIndex::Cell SpatialIndex::cell_of(Vec2 point) const noexcept {
    return Cell{
        clamp_coordinate(std::floor(point.x / cell_size_mm_)),
        clamp_coordinate(std::floor(point.y / cell_size_mm_)),
    };
}

void SpatialIndex::insert(ElementId id, const BBox& box) {
    if (!box.is_valid()) return;

    const Cell min_cell = cell_of(box.min);
    const Cell max_cell = cell_of(box.max);

    const std::int64_t columns =
        static_cast<std::int64_t>(max_cell.x) - static_cast<std::int64_t>(min_cell.x) + 1;
    const std::int64_t rows =
        static_cast<std::int64_t>(max_cell.y) - static_cast<std::int64_t>(min_cell.y) + 1;
    if (columns <= 0 || rows <= 0) return;

    if (columns * rows > kMaxCellsPerElement) {
        large_.push_back(id);
        return;
    }

    for (std::int32_t x = min_cell.x;; ++x) {
        for (std::int32_t y = min_cell.y;; ++y) {
            cells_[Cell{x, y}].push_back(id);
            if (y == max_cell.y) break;
        }
        if (x == max_cell.x) break;
    }
}

void SpatialIndex::rebuild(const Scene& scene) {
    clear();
    for (const Element& element : scene.elements) {
        BBox box = shape_bbox(element.shape);
        if (!box.is_valid()) continue;
        const double half_width = element.width_mm * 0.5;
        box.min.x -= half_width;
        box.min.y -= half_width;
        box.max.x += half_width;
        box.max.y += half_width;
        insert(element.id, box);
    }
}

std::vector<ElementId> SpatialIndex::query(const BBox& box) const {
    std::vector<ElementId> result = large_;
    if (!box.is_valid()) {
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    const Cell min_cell = cell_of(box.min);
    const Cell max_cell = cell_of(box.max);

    for (std::int32_t x = min_cell.x;; ++x) {
        for (std::int32_t y = min_cell.y;; ++y) {
            const auto it = cells_.find(Cell{x, y});
            if (it != cells_.end()) {
                result.insert(result.end(), it->second.begin(), it->second.end());
            }
            if (y == max_cell.y) break;
        }
        if (x == max_cell.x) break;
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<ElementId> SpatialIndex::query_point(Vec2 point, double radius) const {
    BBox box;
    box.expand(Vec2{point.x - radius, point.y - radius});
    box.expand(Vec2{point.x + radius, point.y + radius});
    return query(box);
}

} // namespace pf
