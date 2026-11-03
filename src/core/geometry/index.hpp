#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/geometry/bbox.hpp"
#include "core/scene/ids.hpp"

namespace pf {

struct Scene;

/// Grid uniforme com bbox dos elementos. Usado para pick, snap e interseções.
class SpatialIndex {
public:
    explicit SpatialIndex(double cell_size_mm = 100.0);

    void clear();

    void insert(ElementId id, const BBox& box);

    /// Reconstrução completa a partir da cena.
    void rebuild(const Scene& scene);

    [[nodiscard]] std::vector<ElementId> query(const BBox& box) const;

    [[nodiscard]] std::vector<ElementId> query_point(Vec2 point, double radius) const;

    [[nodiscard]] bool empty() const noexcept { return cells_.empty() && large_.empty(); }

    [[nodiscard]] double cell_size() const noexcept { return cell_size_mm_; }

private:
    struct Cell {
        std::int32_t x{0};
        std::int32_t y{0};

        [[nodiscard]] bool operator==(const Cell&) const noexcept = default;
    };

    struct CellHash {
        [[nodiscard]] std::size_t operator()(const Cell& cell) const noexcept {
            const auto x = static_cast<std::uint32_t>(cell.x);
            const auto y = static_cast<std::uint32_t>(cell.y);
            return (static_cast<std::size_t>(x) << 32) ^ static_cast<std::size_t>(y);
        }
    };

    [[nodiscard]] Cell cell_of(Vec2 point) const noexcept;

    double cell_size_mm_{100.0};
    std::unordered_map<Cell, std::vector<ElementId>, CellHash> cells_;
    /// Elementos cuja bbox cobre células demais para indexar individualmente.
    std::vector<ElementId> large_;
};

} // namespace pf
