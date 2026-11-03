# PistaForge

Editor gráfico de pistas para robôs seguidores de linha, no estilo Inkscape
simplificado. A arena (ex.: 4 × 3 m) é definida pelo usuário e o traçado é desenhado
dentro dela com uma biblioteca de curvas/cruzamentos e Béziers livres.

- **Núcleo:** C++23 (MSVC 14.51 / Visual Studio 2026)
- **Interface:** FastWebUI (fork de WebUI) + HTML/Canvas
- **Impressão:** SVG 1:1 em mm, mosaico de páginas
- **Simulação:** exportação JSON versionada (simulador virá depois)

## Estado atual — M0

- [x] Repositório, CMake, presets MSVC
- [x] FastWebUI vendorizado e linkado como biblioteca estática
- [x] Ponte JSON `app_call` (dispatcher com erros estruturados)
- [x] Shell mínimo com janela, `index.html` e canvas da arena
- [x] Núcleo geométrico: `Vec2`, `CubicBezier` (eval, derivadas, curvatura, split,
      bbox, comprimento, flatten, ponto mais próximo)
- [x] Testes com doctest e alvo `pf_tests`
- [x] Spike de latência: p50 ≈ 9,7 ms / p95 ≈ 17,0 ms (100 chamadas)
- [ ] Arena, elementos e conexões (M1)

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
src/core/geometry/   Vec2, BBox, CubicBezier
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
