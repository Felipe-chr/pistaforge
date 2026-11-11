# Formato `.pfsim.json` — schema 1

Documento gerado por **Documento → Simulador**. Ele entrega ao simulador a
arena, a linha central amostrada na ordem de percurso, os trechos por elemento,
as métricas do traçado e os pontos críticos. É somente leitura: o PistaForge
não abre esse formato.

Identificador: `"format": "pistaforge.sim"` e `"schema": 1`.

## Estrutura

```json
{
  "format": "pistaforge.sim",
  "schema": 1,
  "generator": { "name": "PistaForge", "version": "0.1.0" },
  "units": { "length": "mm", "angle": "deg", "curvature": "1/mm" },
  "coordinate_system": {
    "origin": "top-left",
    "x": "right",
    "y": "down",
    "heading_zero": "+x",
    "heading_positive": "clockwise"
  },
  "arena": { "name": "Arena", "width_mm": 4000.0, "height_mm": 3000.0, "margin_mm": 100.0 },
  "path": {
    "closed": false,
    "ordered": true,
    "step_mm": 50.0,
    "total_length_mm": 1234.5,
    "point_count": 26,
    "start_element": 1,
    "points": [],
    "segments": []
  },
  "metrics": {},
  "critical_points": []
}
```

## `path.points`

Cada ponto é uma amostra da linha central:

```json
{
  "i": 0,
  "s_mm": 0.0,
  "x_mm": 200.0,
  "y_mm": 300.0,
  "heading_deg": 0.0,
  "curvature_1_mm": 0.0,
  "width_mm": 19.0,
  "element": 1
}
```

| Campo | Significado |
|-------|-------------|
| `i` | índice sequencial (0..n-1) |
| `s_mm` | distância percorrida desde o início, em mm |
| `x_mm`, `y_mm` | posição na arena, em mm |
| `heading_deg` | direção do percurso em graus |
| `curvature_1_mm` | curvatura assinada em 1/mm |
| `width_mm` | largura da faixa no ponto |
| `element` | id do elemento de origem |

O último ponto sempre cai no fim do traçado. Com `step_mm = 50`, o passo entre
amostras é de 50 mm, exceto no último intervalo.

## `path.segments`

Trechos contínuos percorridos de cada elemento:

```json
{ "element": 1, "start_s_mm": 0.0, "length_mm": 600.0, "reversed": false }
```

`reversed` indica que o elemento foi percorrido no sentido oposto ao da sua
geometria.

## `metrics`

| Campo | Significado |
|-------|-------------|
| `total_length_mm` | comprimento total do percurso |
| `straight_length_mm`, `curve_length_mm` | comprimento em retas e curvas |
| `straight_count`, `curve_count` | contagem de retas e curvas |
| `crossing_count`, `special_count` | cruzamentos e elementos especiais |
| `element_count`, `connection_count` | totais do documento |
| `sharp_turn_count`, `tight_curve_count` | viradas bruscas e curvas apertadas |
| `difficulty` | pontuação de 0 a 100 |
| `difficulty_label` | nota textual (Muito Fácil → Extremo) |
| `estimated_time_s` | tempo estimado de percurso |
| `recommended_speed_mps` | velocidade média recomendada |

## `critical_points`

```json
{
  "kind": "tight_curve",
  "x_mm": 900.0,
  "y_mm": 400.0,
  "element": 3,
  "severity": 0.72,
  "t": 0.45,
  "angle_deg": 0.0,
  "radius_mm": 120.0
}
```

`kind`: `tight_curve`, `abrupt_turn`, `line_loss`, `sensor_critical` ou
`high_difficulty`. `severity` vai de 0 a 1; `t` é o parâmetro no elemento
(0..1) quando aplicável; `angle_deg` e `radius_mm` complementam viradas e
curvas.

## Compatibilidade

- O documento é autossuficiente: o simulador não precisa abrir o `.pftrack`.
- Campos novos podem ser adicionados sem quebrar leitores antigos; a versão do
  schema só muda em alterações incompatíveis.
- A matriz CSV (`-tabela.csv`) e este JSON usam a mesma amostragem e as mesmas
  convenções de coordenadas.
