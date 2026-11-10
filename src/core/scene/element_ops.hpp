#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/scene/element.hpp"

namespace pf {

/// Tipos de forma oferecidos na faixa contextual de conversão.
enum class ShapeType { Line, Cubic, Arc90, Arc180, SCurve };

[[nodiscard]] std::string_view to_string(ShapeType type) noexcept;
[[nodiscard]] std::optional<ShapeType> shape_type_from_string(std::string_view text) noexcept;

/// Pontas inicial e final de uma forma (módulos curve.90 usam in/out).
[[nodiscard]] std::optional<std::pair<Vec2, Vec2>> shape_endpoints(const Shape& shape);

/// Nomes das portas correspondentes às pontas, quando existirem.
[[nodiscard]] std::optional<std::pair<std::string, std::string>> endpoint_port_names(
    const Shape& shape);

/// Tipo inferido a partir da geometria (usado para destacar a opção atual).
/// Módulos e arcos fora de 90°/180° não têm tipo inferido.
[[nodiscard]] std::optional<ShapeType> infer_shape_type(const Shape& shape);

/// Raio característico (raio do arco, raio mínimo aproximado de uma Bézier ou
/// parâmetro `radius` de um módulo). `nullopt` para retas e cruzamentos.
[[nodiscard]] std::optional<double> shape_radius(const Shape& shape);

/// Ângulo característico em graus: varredura de arcos/módulos e ângulo total de
/// virada de uma Bézier. `nullopt` para retas e cruzamentos.
[[nodiscard]] std::optional<double> shape_angle_deg(const Shape& shape);

/// Inverte o lado da forma em torno da corda entre as pontas, mantendo-as fixas.
/// Retorna false para formas sem lado (reta, cruzamento) ou degeneradas.
bool flip_shape(Shape& shape);

/// Espelha a forma em torno da reta vertical `x = axis_x`.
bool mirror_shape(Shape& shape, double axis_x);

/// Converte a forma para `type`, preservando as pontas e o lado de curvatura.
[[nodiscard]] std::optional<Shape> convert_shape(const Shape& shape, ShapeType type);

/// Converte para uma Bézier com tangentes suaves nas pontas.
[[nodiscard]] std::optional<Shape> smooth_shape(const Shape& shape);

/// Converte para um laço fechado (arco de 360°).
[[nodiscard]] std::optional<Shape> closed_shape(const Shape& shape);

struct SplitShapes {
    Shape first;
    Shape second;
};

/// Divide a forma no parâmetro `t` (limitado a [0,05, 0,95]).
[[nodiscard]] std::optional<SplitShapes> split_shape(const Shape& shape, double t = 0.5);

/// Ajusta o raio preservando as pontas (arcos) ou a corda (Béziers).
/// `factor > 1` deixa a curva mais suave; `factor < 1` mais fechada.
bool adjust_radius(Shape& shape, double factor);

/// Altera o ângulo/varredura em graus, preservando as pontas quando possível.
bool adjust_angle(Shape& shape, double delta_deg);

/// Ajusta o comprimento de uma reta mantendo o ponto inicial e a direção.
bool adjust_length(Shape& shape, double length_mm);

} // namespace pf
