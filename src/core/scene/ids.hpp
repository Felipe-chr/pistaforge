#pragma once

#include <cstdint>

namespace pf {

/// Identificadores monotônicos, persistidos e nunca reutilizados.
using ElementId = std::uint64_t;
using ConnectionId = std::uint64_t;
using MarkerId = std::uint64_t;
using GuideId = std::uint64_t;

inline constexpr std::uint64_t kInvalidId = 0;

} // namespace pf
