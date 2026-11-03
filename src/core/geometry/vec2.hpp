#pragma once

#include <cmath>

namespace pf {

inline constexpr double kGeometryEpsilon = 1e-9;

/// Ponto/vetor 2D em milímetros. X para a direita, Y para baixo.
struct Vec2 {
    double x{0.0};
    double y{0.0};

    constexpr Vec2() = default;
    constexpr Vec2(double x_value, double y_value) : x(x_value), y(y_value) {}

    [[nodiscard]] constexpr Vec2 operator+(Vec2 other) const noexcept {
        return {x + other.x, y + other.y};
    }

    [[nodiscard]] constexpr Vec2 operator-(Vec2 other) const noexcept {
        return {x - other.x, y - other.y};
    }

    [[nodiscard]] constexpr Vec2 operator-() const noexcept { return {-x, -y}; }

    [[nodiscard]] constexpr Vec2 operator*(double factor) const noexcept {
        return {x * factor, y * factor};
    }

    [[nodiscard]] constexpr Vec2 operator/(double divisor) const noexcept {
        return {x / divisor, y / divisor};
    }

    constexpr Vec2& operator+=(Vec2 other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vec2& operator-=(Vec2 other) noexcept {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr Vec2& operator*=(double factor) noexcept {
        x *= factor;
        y *= factor;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Vec2&) const noexcept = default;

    [[nodiscard]] constexpr double dot(Vec2 other) const noexcept {
        return x * other.x + y * other.y;
    }

    [[nodiscard]] constexpr double cross(Vec2 other) const noexcept {
        return x * other.y - y * other.x;
    }

    [[nodiscard]] constexpr double length_squared() const noexcept { return x * x + y * y; }

    [[nodiscard]] double length() const noexcept { return std::sqrt(length_squared()); }

    /// Vetor unitário; retorna zero se o comprimento for desprezível.
    [[nodiscard]] Vec2 normalized() const noexcept {
        const double len = length();
        return len > kGeometryEpsilon ? Vec2{x / len, y / len} : Vec2{};
    }

    /// Rotação de 90° no sentido horário na tela (Y para baixo).
    [[nodiscard]] constexpr Vec2 perpendicular() const noexcept { return {-y, x}; }

    /// Rotação de `radians` no sentido horário na tela.
    [[nodiscard]] Vec2 rotated(double radians) const noexcept {
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        return {x * c - y * s, x * s + y * c};
    }
};

[[nodiscard]] constexpr Vec2 operator*(double factor, Vec2 vector) noexcept {
    return vector * factor;
}

[[nodiscard]] constexpr Vec2 lerp(Vec2 a, Vec2 b, double t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

[[nodiscard]] inline double distance(Vec2 a, Vec2 b) noexcept { return (b - a).length(); }

[[nodiscard]] constexpr double distance_squared(Vec2 a, Vec2 b) noexcept {
    return (b - a).length_squared();
}

} // namespace pf
