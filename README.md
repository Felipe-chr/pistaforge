# PistaForge

Editor gráfico de pistas para robôs seguidores de linha, no estilo Inkscape
simplificado. A arena (ex.: 4 × 3 m) é definida pelo usuário e o traçado é desenhado
dentro dela com uma biblioteca de curvas/cruzamentos e Béziers livres.

- **Núcleo:** C++23 (MSVC 14.51 / Visual Studio 2026)
- **Interface:** FastWebUI (fork de WebUI) + HTML/Canvas
- **Impressão:** SVG 1:1 em mm, mosaico de páginas
- **Simulação:** exportação JSON versionada (simulador virá depois)

## Estado atual — M1 + M2

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
- [x] Testes com doctest e alvo `pf_tests` — 85 casos / 1450 asserções
- [x] Benchmark: round-trip de 5k elementos em menos de 1 s (Debug)
- [x] Spike de latência: p50 ≈ 9,7 ms / p95 ≈ 17,0 ms (100 chamadas)
- [x] Editor MVP com canvas interativo (M2)
  - [x] Ferramentas **Selecionar**, **Reta** e **Bézier** (desenho por arrasto)
  - [x] Seleção por clique, arrasto para mover e exclusão (`Del`)
  - [x] Encaixe com prioridade **porta > elemento > grade** e indicador visual
  - [x] Desfazer/refazer com histórico de instantâneos e arrastos agrupados
  - [x] Zoom pela roda do mouse, pan com botão do meio ou `Espaço` + arrasto

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
| `doc.moveElement` | `{id,delta,merge?}` | `{id,delta,rev}` |
| `doc.setElement` | `{id,width?,layer?,hidden?,locked?,label?,merge?}` | `{id,rev}` |
| `doc.undo` | `{}` | `{rev,label,can_undo,can_redo,history}` |
| `doc.redo` | `{}` | `{rev,label,can_undo,can_redo,history}` |
| `doc.history` | `{}` | `{can_undo,can_redo,undo,redo}` |
| `doc.connect` | `{a:[id,port],b:[id,port]}` | `{id,rev}` |
| `doc.disconnect` | `{id}` | `{rev,removed}` |
| `doc.addMarker` | `{type,x,y,angle?,text?}` | `{id,rev}` |
| `doc.setArena` | `{name?,width?,height?,margin?}` | `{rev,arena}` |
| `doc.setLayers` | `{track?,markers?,annotations?,guides?}` | `{layers,rev}` |
| `scene.ports` | `{}` | lista de portas |
| `scene.pick` | `{point,tolerance?}` | `{element,port,t,distance,point}` |
| `scene.snap` | `{point,tolerance?,grid?,snap_grid?,ports?,elements?,ignore?}` | `{kind,point,distance,...}` |
| `scene.validate` | `{}` | diagnósticos |
| `scene.bounds` | `{}` | bbox da cena (inclui largura da faixa) |

`merge` agrupa alterações contínuas (arrastos) em um único passo de desfazer.
`scene.snap` devolve `kind` = `port`, `element`, `grid` ou `none`; o encaixe tenta
as portas primeiro, depois o corpo dos elementos e por último a grade. Em
`scene.snap`, `grid` é o passo em mm e `snap_grid` liga/desliga o encaixe na grade.

Ângulos são gravados em **graus** no JSON (`start`/`sweep` para arcos, `rot` para
módulos e `angle` para marcadores) e usados em **radianos** no núcleo. A API e o
leitor também aceitam os nomes antigos `start_deg`, `sweep_deg`, `rot_deg` e
`angle_deg`.

## Interface

A janela é dividida em quatro áreas:

- **Faixa (ribbon)** no topo, com abas:
  - **Inserir** — ferramentas (selecionar, reta, Bézier), formas, curvas
    (arco 90°, curva 90°, 180°, curva S), cruzamento 90° e edição (desfazer,
    refazer, excluir, novo).
  - **Documento** — novo, abrir, salvar e presets de arena (4 × 3 m e 3 × 4 m).
  - **Exibir** — visibilidade das camadas Traçado, Marcadores, Anotações e Guias.
- **Biblioteca** à esquerda, com as mesmas ações de inserção da faixa.
- **Prancheta** ao centro, com grade, margem de segurança, cotas e cursor.
- **Painel** à direita, com versão, compilador, contadores do documento,
  seleção atual, arena e estado da ponte.

Cada clique de inserção coloca o elemento na posição do cursor e avança o cursor
para o fim do elemento (sem conexão automática nesta etapa). Os elementos são
criados no núcleo C++ via `doc.*`; o JavaScript apenas desenha e captura
entrada.

### Edição no canvas (M2)

| Ação | Como fazer |
|------|------------|
| Selecionar | ferramenta **Selecionar** (`V`) e clique no elemento |
| Mover | arrastar o elemento selecionado (com encaixe) |
| Excluir | `Del` ou `Backspace` |
| Desenhar reta | ferramenta **Reta** (`L`) e arrastar |
| Desenhar Bézier | ferramenta **Bézier** (`B`) e arrastar (com `Shift` para inverter a curvatura) |
| Desfazer / refazer | `Ctrl+Z` / `Ctrl+Y` |
| Zoom | roda do mouse (ancorado no ponteiro) ou botões **Aproximar/Afastar/Ajustar** |
| Pan | botão do meio arrastando ou `Espaço` + arrasto |
| Cancelar | `Esc` |

O arrasto contínuo gera uma única entrada de desfazer (mesma chave `merge`).

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

Executar (a janela do app abre sozinha; a mesma UI também responde no navegador):

```powershell
.\build\windows-msvc-debug\Debug\PistaForge.exe
```

Para fixar a porta e abrir no navegador:

```powershell
.\build\windows-msvc-debug\Debug\PistaForge.exe --port 8099
# abra http://localhost:8099/
```

Mantenha a aba aberta enquanto testa. O processo encerra quando a última janela
conectada é fechada (a janela nativa do app e a aba do navegador contam como
clientes).

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
