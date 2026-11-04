# PistaForge

Editor gráfico de pistas para robôs seguidores de linha, no estilo Inkscape
simplificado. A arena (ex.: 4 × 3 m) é definida pelo usuário e o traçado é desenhado
dentro dela com uma biblioteca de curvas/cruzamentos e Béziers livres.

- **Núcleo:** C++23 (MSVC 14.51 / Visual Studio 2026)
- **Interface:** FastWebUI (fork de WebUI) + HTML/Canvas
- **Impressão:** SVG 1:1 em mm, mosaico de páginas
- **Simulação:** exportação JSON versionada (simulador virá depois)

## Estado atual — M1

- [x] Repositório, CMake, presets MSVC
- [x] FastWebUI vendorizado e linkado como biblioteca estática
- [x] Ponte JSON `app_call` (dispatcher com erros estruturados)
- [x] Shell mínimo com janela, `index.html` e canvas da arena
- [x] Núcleo geométrico: `Vec2`, `BBox`, `CubicBezier`, `LineSegment`,
      `ArcSegment` e índice espacial em grid uniforme
- [x] Modelo de cena: arena, elementos (linha/Bézier/arco/módulo), portas,
      conexões, marcadores, guias e camadas
- [x] Validação: arena, schema, formas degeneradas, templates, portas,
      fechamento (0,01 mm), alinhamento (0,1°) e conexões duplicadas
- [x] Documento `.pftrack` (schema 1) com round-trip JSON completo
- [x] API de documento/cena (`doc.*`, `scene.*`)
- [x] Interface com faixa estilo Office (abas **Inserir**, **Documento** e
      **Exibir**), ícones SVG de curvas e inserção real de elementos
- [x] Biblioteca lateral, alternância de camadas, desfazer e diálogo de
      caminho para salvar/abrir
- [x] Testes com doctest e alvo `pf_tests` — 63 casos / 1287 asserções
- [x] Benchmark: round-trip de 5k elementos em menos de 1 s (Debug)
- [x] Spike de latência: p50 ≈ 9,7 ms / p95 ≈ 17,0 ms (100 chamadas)
- [ ] Editor MVP com canvas interativo (M2)

### API de documento e cena

| Método | Entrada | Saída |
|--------|---------|-------|
| `doc.new` | `{arena?}` | resumo do documento |
| `doc.state` | `{}` | documento `.pftrack` + `path` |
| `doc.load` | `{path}` | resumo do documento |
| `doc.save` | `{path?}` | `{path, rev}` |
| `doc.addLine` | `{a,b,width?,layer?}` | `{id,rev}` |
| `doc.addCubic` | `{p0,p1,p2,p3,width?,layer?}` | `{id,rev}` |
| `doc.addArc` | `{center,radius,start?,sweep?,width?,layer?}` | `{id,rev}` |
| `doc.addModule` | `{template,params?,transform?,width?,layer?}` | `{id,rev}` |
| `doc.removeElement` | `{id}` | `{rev,removed}` |
| `doc.connect` | `{a:[id,port],b:[id,port]}` | `{id,rev}` |
| `doc.disconnect` | `{id}` | `{rev,removed}` |
| `doc.addMarker` | `{type,x,y,angle?,text?}` | `{id,rev}` |
| `doc.setArena` | `{name?,width?,height?,margin?}` | `{rev,arena}` |
| `doc.setLayers` | `{track?,markers?,annotations?,guides?}` | `{layers,rev}` |
| `scene.ports` | `{}` | lista de portas |
| `scene.pick` | `{point,tolerance?}` | `{element,port,t,distance,point}` |
| `scene.validate` | `{}` | diagnósticos |
| `scene.bounds` | `{}` | bbox da cena (inclui largura da faixa) |

Ângulos são gravados em **graus** no JSON (`start`/`sweep` para arcos, `rot` para
módulos e `angle` para marcadores) e usados em **radianos** no núcleo. A API e o
leitor também aceitam os nomes antigos `start_deg`, `sweep_deg`, `rot_deg` e
`angle_deg`.

## Interface

A janela é dividida em quatro áreas:

- **Faixa (ribbon)** no topo, com abas:
  - **Inserir** — formas (linha, Bézier), curvas (arco 90°, curva 90°, 180°,
    curva S), cruzamento 90° e edição (desfazer, limpar).
  - **Documento** — novo, abrir, salvar e presets de arena (4 × 3 m e 3 × 4 m).
  - **Exibir** — visibilidade das camadas Traçado, Marcadores, Anotações e Guias.
- **Biblioteca** à esquerda, com as mesmas ações de inserção da faixa.
- **Prancheta** ao centro, com grade, margem de segurança, cotas e cursor.
- **Painel** à direita, com versão, compilador, contadores do documento, arena e
  estado da ponte.

Cada clique de inserção coloca o elemento na posição do cursor e avança o cursor
para o fim do elemento (sem conexão automática nesta etapa). Os elementos são
criados no núcleo C++ via `doc.*`; o JavaScript apenas desenha e captura
entrada.

Salvar e abrir usam um diálogo próprio (`<dialog>`) porque o navegador embutido
não implementa `window.prompt`.

## Build

Requisitos: Visual Studio 2026 com "Desenvolvimento para desktop com C++",
CMake ≥ 3.28 e Git. O CMake e o Ninja do Visual Studio já bastam.

O FastWebUI é um **submódulo git** (`vendor/FastWebUI`) apontando para
`https://github.com/frankalcantara/FastWebUI`, fixado em um commit conhecido.
Clone o projeto com os submódulos:

```powershell
git clone --recurse-submodules <url-do-repositorio>
```

Em um clone já existente:

```powershell
git submodule update --init --recursive
```

Para atualizar o FastWebUI de propósito:

```powershell
git submodule update --remote vendor/FastWebUI
```

Build:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Se `cmake` não estiver no PATH, use o que vem com o Visual Studio:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-msvc-debug
& $cmake --build --preset windows-msvc-debug
```

Testes:

```powershell
ctest --preset windows-msvc-debug --output-on-failure
```

Executar:

```powershell
.\build\windows-msvc-debug\Debug\PistaForge.exe
```

Autoteste do núcleo e da ponte, sem abrir janela:

```powershell
.\build\windows-msvc-debug\Debug\PistaForge.exe --selftest
```

## Estrutura

```
src/core/geometry/   Vec2, BBox, CubicBezier, LineSegment, ArcSegment, SpatialIndex
src/core/scene/      Arena, Element, ModuleInstance, Scene, validação
src/core/io/         Documento `.pftrack` (JSON schema 1)
src/app/bridge/      Dispatcher JSON + métodos da API
src/app/shell/       IShell/WebUiShell (FastWebUI)
src/web/             index.html, styles.css, app.js
tests/               doctest
vendor/FastWebUI/    submódulo git (frankalcantara/FastWebUI, commit fixado)
vendor/doctest/      doctest 2.4.11 (header)
vendor/nlohmann/     nlohmann/json 3.11.3 (header)
docs/                ADRs e formato de arquivo
```

## Notas de runtime

- O WebUI/CivetWeb usa caminhos ANSI e falha ao abrir arquivos estáticos por
  caminhos com acentos ou nomes 8.3 (`~`). Por isso, ao iniciar, o app copia os
  assets de `src/web` (ou de `web/` ao lado do `.exe`) para
  `%LOCALAPPDATA%\PistaForge\web` e serve dessa pasta.
- A opção `multi_client` fica habilitada para evitar o bloqueio 403 de
  subrecursos pelo cookie `webui_auth`.
- `--port N` fixa a porta do servidor embutido.

## Decisões

- Windows-only na v1; MSVC.
- Uma arena com um traçado principal.
- Cruzamentos sempre "segue reto" (`w↔e`, `s↔n`).
- Simulador vem depois; o formato `.pfsim.json` é versionado e documentado.
- Todo o domínio em C++; o frontend só desenha e captura entrada.
