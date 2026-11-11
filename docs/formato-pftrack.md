# Formato `.pftrack` — schema 1

Documento JSON que guarda a arena, os elementos do traçado, as conexões, os
marcadores, as guias e a visibilidade das camadas. É o formato de **Salvar** e
**Abrir** do PistaForge.

## Estrutura

```json
{
  "schema": 1,
  "arena": {
    "name": "Arena",
    "width": 4000.0,
    "height": 3000.0,
    "margin": 100.0,
    "show_dimensions": true
  },
  "revision": 12,
  "next_id": 8,
  "layers": {
    "track": true,
    "markers": true,
    "annotations": true,
    "guides": false
  },
  "elements": [],
  "connections": [],
  "markers": [],
  "guides": []
}
```

| Campo | Tipo | Padrão | Descrição |
|-------|------|--------|-----------|
| `schema` | inteiro | `1` | Versão do formato |
| `arena.name` | texto | `"Arena"` | Nome exibido |
| `arena.width` | número | `4000` | Largura da arena em mm |
| `arena.height` | número | `3000` | Altura da arena em mm |
| `arena.margin` | número | `100` | Margem de segurança em mm |
| `arena.show_dimensions` | booleano | `true` | Desenha as cotas |
| `revision` | inteiro | `0` | Contador de alterações (informativo) |
| `next_id` | inteiro | `1` | Próximo id livre de elemento |
| `layers.*` | booleano | ver exemplo | Visibilidade das camadas |
| `elements` | lista | `[]` | Elementos do desenho |
| `connections` | lista | `[]` | Ligações entre portas |
| `markers` | lista | `[]` | Largada, chegada e marcadores |
| `guides` | lista | `[]` | Linhas de guia |

## Elementos

Todo elemento tem `id`, `type`, `width` (mm), `layer` (`track`, `markers`,
`annotations` ou `guides`) e, opcionalmente, `hidden`, `locked`, `label` e
`difficulty` (1–5; ausente = automática).

### `line`

```json
{ "id": 1, "type": "line", "layer": "track", "width": 19.0,
  "a": [0, 0], "b": [1000, 0] }
```

### `arc`

```json
{ "id": 2, "type": "arc", "layer": "track", "width": 19.0,
  "center": [1000, 500], "radius": 500, "start": 0, "sweep": 90 }
```

`start` e `sweep` em graus, no sentido horário da tela. A leitura também aceita
`start_deg` e `sweep_deg`.

### `cubic`

```json
{ "id": 3, "type": "cubic", "layer": "track", "width": 19.0,
  "p0": [0, 0], "p1": [300, 0], "p2": [700, 400], "p3": [1000, 400] }
```

### `module`

```json
{ "id": 4, "type": "module", "layer": "track", "width": 19.0,
  "template": "curve90", "version": 1,
  "params": { "radius": 300 },
  "transform": { "tx": 500, "ty": 300, "rot": 90, "mirrored": false } }
```

`rot` em graus. A leitura também aceita `rot_deg`.

## Conexões

```json
{ "id": 1, "a": [1, "end"], "b": [2, "start"] }
```

Cada lado é `[id_do_elemento, nome_da_porta]`. Portas de primitivas:
`start`/`end`. Módulos usam as portas definidas pelo template (`in`/`out` são
migradas para `start`/`end` ao converter o módulo em primitiva).

## Marcadores

```json
{ "id": 1, "type": "start", "x": 200, "y": 300, "angle": 0, "text": "Largada" }
```

`type`: `start`, `finish`, `start_finish`, `direction_arrow` ou `text`.
`angle` em graus. A leitura também aceita `angle_deg`.

## Guias

```json
{ "id": 1, "a": [0, 0], "b": [4000, 3000] }
```

## Compatibilidade

- O leitor ignora campos desconhecidos.
- Campos ausentes usam os padrões da tabela acima.
- `schema` maior que 1 deve ser recusado por versões antigas; hoje o leitor
  aceita `1` e trata ausência como `1`.
- O round-trip `abrir → salvar` preserva ids, conexões e ordem dos elementos.
