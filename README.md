# PistaForge

[![build](https://github.com/Felipe-chr/pistaforge/actions/workflows/build.yml/badge.svg)](https://github.com/Felipe-chr/pistaforge/actions/workflows/build.yml)

Editor gráfico de pistas para robôs seguidores de linha, no estilo Inkscape
simplificado. A arena (ex.: 4 × 3 m) é definida pelo usuário e o traçado é desenhado
dentro dela com uma biblioteca de curvas/cruzamentos e Béziers livres.

- **Núcleo:** C++23 (MSVC 14.51 / Visual Studio 2026)
- **Interface:** FastWebUI (fork de WebUI) + HTML/Canvas
- **Impressão:** SVG e PDF 1:1 em mm, mosaico de páginas com pontos de marcação
- **Produção:** malha 3D para Blender, pontos/instruções para robô e matriz de
  coordenadas para o simulador

## Estado atual — edição, análise, produção e proteção de dados

- [x] Repositório, CMake, presets MSVC
- [x] FastWebUI vendorizado e linkado como biblioteca estática
- [x] Ponte JSON `app_call` (dispatcher com erros estruturados)
- [x] Shell mínimo com janela, `index.html` e canvas da arena
- [x] Núcleo geométrico: `Vec2`, `BBox`, `CubicBezier`, `LineSegment`,
      `ArcSegment` e índice espacial em grid uniforme
- [x] Modelo de cena: arena, elementos (linha/Bézier/arco/módulo), portas,
      conexões, marcadores, guias e camadas
- [x] Validação: arena, schema, formas degeneradas, templates, portas,
      fechamento (0,01 mm), alinhamento (0,1°), conexões duplicadas e
      diagnósticos do traçado (curvas apertadas, viradas bruscas, sobreposição,
      cruzamento conflitante, pista aberta, dificuldade e velocidade)
- [x] Documento `.pftrack` (schema 1) com round-trip JSON completo
- [x] API de documento/cena (`doc.*`, `scene.*`)
- [x] Interface com faixa estilo Office (abas **Inserir**, **Documento** e
      **Exibir**), ícones SVG de curvas e inserção real de elementos
- [x] Biblioteca lateral, alternância de camadas, desfazer e diálogo nativo
      do Windows para abrir/salvar (com fallback em `<dialog>`)
- [x] Testes com doctest e alvo `pf_tests` — 155 casos / 1977 asserções
- [x] Benchmark: round-trip de 5k elementos em menos de 1 s (Debug)
- [x] Spike de latência: p50 ≈ 9,7 ms / p95 ≈ 17,0 ms (100 chamadas)
- [x] Editor MVP com canvas interativo (M2)
  - [x] Ferramentas **Selecionar**, **Reta** e **Bézier** (desenho por arrasto)
  - [x] Seleção por clique, arrasto para mover e exclusão (`Del`)
  - [x] Encaixe com prioridade **porta > elemento > grade** e indicador visual
  - [x] Desfazer/refazer com histórico de instantâneos e arrastos agrupados
  - [x] Zoom pela roda do mouse, pan com botão do meio ou `Espaço` + arrasto
  - [x] Arrastar em área vazia com o botão esquerdo também desloca a arena
- [x] Inserção por posicionamento com o mouse (M3)
  - [x] Primeiro clique ancora, movimento do mouse dimensiona/orienta e o
        segundo clique confirma; `Esc` cancela
  - [x] Prévia em tempo real com medida em mm
  - [x] Alças de edição para retas e Béziers (extremos e pontos de controle)
  - [x] Diálogos nativos de arquivo (`GetOpenFileNameW` / `GetSaveFileNameW`)
  - [x] **Salvar como** na faixa e abertura direta na última pasta usada
  - [x] Largada e chegada do circuito com faixa quadriculada perpendicular à pista
  - [x] Ímã de pontas: ao aproximar uma extremidade de uma porta existente, o
        encaixe trava e a conexão é criada automaticamente
- [x] Exportação e impressão em SVG 1:1 (M4)
  - [x] Unidades em mm com `viewBox` na mesma escala (imprime sem redução)
  - [x] Arquivo único da arena ou mosaico de páginas com sobreposição
  - [x] Página padrão de 190 × 277 mm (A4 retrato com margens de 10 mm)
  - [x] Régua de calibração de 100 mm, rótulo de página e marcas de canto
  - [x] Grade de referência de 100/500 mm e faixas de largada/chegada
- [x] Edição contextual e análise (M5)
  - [x] Mini-faixas contextuais para a curva/reta selecionada, posicionadas
        acima do elemento (nunca sobre ele) e fechadas ao perder a seleção
  - [x] Curva: inverter direção, suavizar, fechar, espelhar, raio ±, ângulo ±,
        duplicar, copiar/colar propriedades e excluir
  - [x] Reta: ajustar comprimento, dividir segmento, inserir curva, duplicar,
        largura, copiar/colar propriedades e excluir
  - [x] Painel **Análise da pista** recalculado a cada alteração: comprimento
        total, retas, curvas, cruzamentos, especiais, dificuldade, tempo
        estimado e velocidade média recomendada
  - [x] Pontuação de dificuldade (0–100) com notas Muito Fácil → Extremo e
        dificuldade manual por elemento (0 = automática)
  - [x] **Visão do Robô**: sobreposição de curvas apertadas, viradas bruscas,
        perda de linha, trechos críticos de sensor e áreas de alta dificuldade
  - [x] **Validar Pista** com lista clicável que navega até o trecho apontado
- [x] Produção e simulador (M6)
  - [x] **PDF 1:1** vetorial com mosaico, calibração e pontos numerados
        (`P1 INICIO`, `Pn`, `Pn FIM`) com coordenadas para desenhar no chão
  - [x] **3D para Blender**: malha OBJ + MTL da faixa extrudada, piso, linha
        central e marcadores, com escala padrão em metros e Y para cima
  - [x] **Robô**: lista de pontos (`-pontos.csv`) e instruções de pintura
        (`-instrucoes.txt`) com `INICIO`, `AVANCAR`, `VIRAR_DIR`, `VIRAR_ESQ`, `FIM`
  - [x] **Matriz de coordenadas** (CSV) ordenada por conexões, com `s`, posição,
        rumo, curvatura, largura e elemento — base do simulador
  - [x] **Simulador**: documento `.pfsim.json` (schema 1) com arena, linha
        central amostrada, trechos, métricas e pontos críticos
  - [x] `scene.pathTable` devolve a matriz como JSON e `scene.analysis` inclui
        contagem de pontos e passo
- [x] Proteção de dados e ajustes de interface (M7)
  - [x] Indicador **Arquivo** / **Alterações** no painel do documento
        (`salvo` / `não salvo`) baseado na assinatura do conteúdo — desfazer até
        o estado salvo volta a marcar como limpo
  - [x] Aviso de **alterações não salvas** ao criar/abrir documento, com
        **Salvar**, **Cancelar** e **Descartar**, além do aviso ao fechar a aba
  - [x] Diálogo de **opções de exportação** para PDF, 3D, robô e tabela, com
        validação de valores e preferências lembradas durante a sessão
  - [x] **Configurar arena** no painel: nome, largura, altura e margem, com
        validação (dimensões positivas e margem compatível)
  - [x] **Preferências persistidas** em `%LOCALAPPDATA%\PistaForge\settings.json`
        (última pasta usada e até 10 documentos recentes), com lista **Recentes**
        no painel para reabrir um documento com um clique
  - [x] Caminhos com acentos preservados em UTF-8 ao abrir, salvar e exportar
- [x] Documentação e formatos (M8)
  - [x] `docs/formato-pftrack.md` (schema 1) e `docs/formato-pfsim.md`
  - [x] ADRs: cruzamentos em linha reta, formatos versionados e domínio em C++

### API de documento e cena

| Método | Entrada | Saída |
|--------|---------|-------|
| `app.preferences` | `{}` | `{path,last_dir,recent_files,max_recent}` |
| `app.clearRecent` | `{}` | `{recent_files:[]}` |
| `doc.new` | `{arena?}` | resumo do documento |
| `doc.state` | `{}` | documento `.pftrack` + `path` |
| `doc.load` | `{path}` | resumo do documento |
| `doc.save` | `{path?}` | `{path, rev}` |
| `doc.openDialog` | `{}` | `{path}` ou `{cancelled:true}` |
| `doc.saveDialog` | `{}` | `{path}` ou `{cancelled:true}` |
| `doc.exportSvg` | `{path,tiled?,page_width?,page_height?,overlap?,grid?,calibration?,guides?}` | `{files,pages,tiled,width,height,rev}` |
| `doc.exportSvgDialog` | `{tiled?,page_width?,...}` | `{files,pages,tiled,...}` ou `{cancelled:true}` |
| `doc.exportPdf` | `{path,tiled?,page_width?,page_height?,overlap?,margin?,grid?,calibration?,guides?,centerline?,marks?,mark_step?,path_width?}` | `{files,pages,tiled,marks,centerline,width,height,rev}` |
| `doc.exportPdfDialog` | mesmos parâmetros | `{files,pages,...}` ou `{cancelled:true}` |
| `doc.exportObj` | `{path,scale?,height?,floor?,centerline?,step?}` | `{files,vertices,faces,scale,height_mm,rev}` |
| `doc.exportObjDialog` | mesmos parâmetros | `{files,vertices,faces,...}` ou `{cancelled:true}` |
| `doc.exportRobot` | `{path,step?,brush?,speed?}` | `{files,points,total_length_mm,step_mm,clockwise,closed,rev}` |
| `doc.exportRobotDialog` | mesmos parâmetros | `{files,points,...}` ou `{cancelled:true}` |
| `doc.exportTable` | `{path,step?}` | `{files,points,total_length_mm,step_mm,closed,ordered,rev}` |
| `doc.exportTableDialog` | mesmos parâmetros | `{files,points,...}` ou `{cancelled:true}` |
| `doc.exportSim` | `{path,step?,tolerance?}` | `{files,schema,points,total_length_mm,step_mm,closed,ordered,rev}` |
| `doc.exportSimDialog` | mesmos parâmetros | `{files,points,...}` ou `{cancelled:true}` |
| `doc.addLine` | `{a,b,width?,layer?}` | `{id,rev}` |
| `doc.addCubic` | `{p0,p1,p2,p3,width?,layer?}` | `{id,rev}` |
| `doc.addArc` | `{center,radius,start?,sweep?,width?,layer?}` | `{id,rev}` |
| `doc.addModule` | `{template,params?,transform?,width?,layer?}` | `{id,rev}` |
| `doc.removeElement` | `{id}` | `{rev,removed}` |
| `doc.moveElement` | `{id,delta,merge?}` | `{id,delta,rev}` |
| `doc.setElement` | `{id,width?,layer?,hidden?,locked?,label?,difficulty?,merge?}` | `{id,rev}` |
| `doc.setGeometry` | `{id,a?,b?,p0?,p1?,p2?,p3?,center?,radius?,start?,sweep?,merge?}` | `{id,rev}` |
| `doc.flipElement` | `{id}` | `{id,rev}` |
| `doc.mirrorElement` | `{id,axis?}` | `{id,rev}` |
| `doc.duplicateElement` | `{id,offset?}` | `{id,source,rev}` |
| `doc.adjustLength` | `{id,length?,delta?}` | `{id,length,rev}` |
| `doc.adjustRadius` | `{id,factor?,radius?,delta?}` | `{id,factor,radius_mm,rev}` |
| `doc.setRadius` | `{id,radius}` | `{id,factor,radius_mm,rev}` |
| `doc.adjustAngle` | `{id,delta?}` | `{id,delta,angle_deg,rev}` |
| `doc.setAngle` | `{id,angle}` | `{id,delta,angle_deg,rev}` |
| `doc.smoothElement` | `{id}` | `{id,rev}` |
| `doc.closeElement` | `{id}` | `{id,rev}` |
| `doc.convertElement` | `{id,shape}` | `{id,shape,rev}` |
| `doc.splitElement` | `{id,t?}` | `{id,second,connection,rev}` |
| `doc.undo` | `{}` | `{rev,label,can_undo,can_redo,history}` |
| `doc.redo` | `{}` | `{rev,label,can_undo,can_redo,history}` |
| `doc.history` | `{}` | `{can_undo,can_redo,undo,redo}` |
| `doc.connect` | `{a:[id,port],b:[id,port]}` | `{id,rev}` |
| `doc.disconnect` | `{id}` | `{rev,removed}` |
| `doc.addMarker` | `{type,x,y,angle?,text?}` | `{id,rev}` |
| `doc.addGuide` | `{a:[x,y],b:[x,y]}` | `{id,rev}` |
| `doc.clearGuides` | `{}` | `{removed,rev}` |
| `doc.clearAnnotations` | `{}` | `{markers,guides,rev}` |
| `doc.setStartFinish` | `{kind?:start\|finish\|start_finish,x,y,angle?}` | `{id,type,x,y,angle,rev}` |
| `doc.clearStartFinish` | `{kind?}` | `{removed,rev}` |
| `doc.setArena` | `{name?,width?,height?,margin?,show_dimensions?}` | `{rev,arena}` |
| `doc.setLayers` | `{track?,markers?,annotations?,guides?}` | `{layers,rev}` |
| `scene.ports` | `{}` | lista de portas |
| `scene.pick` | `{point,tolerance?}` | `{element,port,t,distance,point}` |
| `scene.snap` | `{point,tolerance?,grid?,snap_grid?,ports?,elements?,ignore?}` | `{kind,point,distance,...}` |
| `scene.validate` | `{}` | diagnósticos |
| `scene.bounds` | `{}` | bbox da cena (inclui largura da faixa) |
| `scene.analysis` | `{}` | métricas do traçado + pontos críticos |
| `scene.elementInfo` | `{id}` | resumo do elemento (tipo, raio, ângulo, dificuldade) |

`merge` agrupa alterações contínuas (arrastos) em um único passo de desfazer.
`scene.snap` devolve `kind` = `port`, `element`, `grid` ou `none`; o encaixe tenta
as portas primeiro, depois o corpo dos elementos e por último a grade. Para
`port` e `element`, `direction` traz a tangente unitária no ponto encaixado
(usada para orientar a faixa de largada/chegada). Em `scene.snap`, `grid` é o
passo em mm e `snap_grid` liga/desliga o encaixe na grade.

Ângulos são gravados em **graus** no JSON (`start`/`sweep` para arcos, `rot` para
módulos e `angle` para marcadores) e usados em **radianos** no núcleo. A API e o
leitor também aceitam os nomes antigos `start_deg`, `sweep_deg`, `rot_deg` e
`angle_deg`.

## Interface

A janela é dividida em quatro áreas:

- **Faixa (ribbon)** no topo, com abas:
  - **Inserir** — ferramentas (selecionar, reta, Bézier), formas, curvas
    (arco 90°, curva 90°, 180°, curva S), cruzamento 90°, **Circuito**
    (largada, chegada e remover marcas) e edição (desfazer, refazer, excluir,
    novo).
  - **Documento** — novo, abrir, salvar, salvar como, presets de arena
    (4 × 3 m e 3 × 4 m) e exportação (**Exportar SVG**, **Mosaico A4**,
    **PDF 1:1**, **3D Blender**, **Robô**, **Tabela** e **Simulador**). Cada
    exportação abre um diálogo de opções antes de pedir o arquivo.
  - **Exibir** — visibilidade das camadas Traçado, Marcadores, Anotações e Guias,
    além de **Visão do Robô** e **Validar Pista**.
- **Biblioteca** à esquerda, com as mesmas ações de inserção da faixa.
- **Prancheta** ao centro, com grade, margem de segurança, cotas e cursor.
- **Painel** à direita, com arquivo atual, estado de alterações, versão,
  compilador, contadores do documento, **Recentes**, seleção atual, arena (com
  **Configurar arena**), estado da ponte e **Análise da pista**.

As formas da faixa **Inserir** (e da biblioteca) entram em **modo de
posicionamento**: o primeiro clique na arena ancora a forma, o movimento do
mouse define tamanho e direção (com prévia e medida em mm) e o segundo clique
confirma. `Esc` cancela sem criar nada. O JavaScript só desenha e captura
entrada; a geometria é criada no núcleo C++ via `doc.*`.

### Edição no canvas (M2)

| Ação | Como fazer |
|------|------------|
| Inserir forma | escolha a forma na faixa/biblioteca, clique para ancorar e clique de novo para confirmar |
| Selecionar | ferramenta **Selecionar** (`V`) e clique no elemento |
| Mover | arrastar o elemento selecionado (com encaixe) |
| Editar geometria | com reta/Bézier selecionada, arraste as alças (quadradas = extremos, redondas = controles) |
| Excluir | `Del` ou `Backspace` |
| Desenhar reta | ferramenta **Reta** (`L`) e arrastar |
| Desenhar Bézier | ferramenta **Bézier** (`B`) e arrastar (com `Shift` para inverter a curvatura) |
| Desfazer / refazer | `Ctrl+Z` / `Ctrl+Y` |
| Zoom | roda do mouse (ancorado no ponteiro) ou botões **Aproximar/Afastar/Ajustar** |
| Pan | botão do meio arrastando, `Espaço` + arrasto ou arrastar em área vazia com o botão esquerdo |
| Largada / chegada | **Inserir → Circuito → Largada/Chegada** e clique sobre a pista |
| Remover largada/chegada | **Inserir → Circuito → Remover** |
| Inserir seta | **Inserir → Anotações → Seta** e clique na arena |
| Inserir texto | **Inserir → Anotações → Texto**, clique na arena e informe o texto |
| Inserir guia | **Inserir → Anotações → Guia**, clique no início e no fim da linha |
| Limpar anotações | **Inserir → Anotações → Limpar** remove setas, textos e guias, preservando largada e chegada |
| Ímã de pontas | ao posicionar ou desenhar, aproxime a ponta de uma porta existente: ela trava e conecta sozinha |
| Cancelar | `Esc` (posicionamento, arrasto ou seleção) |

O arrasto contínuo gera uma única entrada de desfazer (mesma chave `merge`).
Setas, textos e guias são anotações: ao inserir uma guia, a camada **Guias** é
ativada automaticamente, e **Limpar anotações** remove apenas esses itens,
preservando largada e chegada.

Abrir e salvar usam os diálogos nativos do Windows (`GetOpenFileNameW` /
`GetSaveFileNameW`), começando na última pasta usada. Se o diálogo nativo não
estiver disponível, a interface cai no `<dialog>` da página.

### Alterações não salvas, opções de exportação e arena

O painel **Documento** mostra o nome do arquivo e se há **alterações não
salvas**. A marcação compara o conteúdo editável (arena, elementos, conexões,
marcadores, guias e camadas) com o último estado salvo, então desfazer até o
ponto salvo volta a indicar `salvo`. Criar um documento novo ou abrir outro com
alterações pendentes mostra **Salvar**, **Cancelar** e **Descartar**; fechar a
aba também pede confirmação.

Cada botão de exportação abre primeiro um diálogo de opções, lembrado durante a
sessão:

| Exportação | Opções |
|------------|--------|
| **PDF 1:1** | páginas A4 (ou página única do tamanho da arena), pontos no chão, somente linha central, grade, calibração, passo das marcas, largura da linha e sobreposição |
| **3D Blender** | piso, linha central, escala, espessura da faixa e passo da malha |
| **Robô** | passo dos pontos, largura do pincel (0 = da pista) e velocidade base |
| **Tabela** | passo da matriz |
| **Simulador** | passo da linha central e tolerância das curvas |

**Configurar arena** (painel direito) permite alterar nome, largura, altura e
margem em milímetros. Valores inválidos (dimensões não positivas ou margem maior
que metade da arena) mantêm o diálogo aberto com a mensagem de erro.

### Preferências e documentos recentes

O aplicativo guarda a última pasta usada nos diálogos e os 10 documentos mais
recentes em `%LOCALAPPDATA%\PistaForge\settings.json` (schema 1). O painel
**Recentes** lista os arquivos do mais novo para o mais antigo; clicar reabre o
documento (respeitando o aviso de alterações não salvas) e **Limpar lista**
esvazia as entradas. O formato do arquivo de configuração está em
[`docs/formato-settings.md`](docs/formato-settings.md) e o arquivo pode ser
removido para voltar aos padrões.

### Exportação e impressão (SVG)

**Documento → Exportar SVG** grava a arena inteira em um arquivo; **Mosaico A4**
divide o desenho em páginas de 190 × 277 mm (A4 retrato com margens de 10 mm) e
sobreposição de 10 mm. O desenho usa milímetros como unidade (`width="4000mm"` e
`viewBox="0 0 4000 3000"`), então a impressão sai em escala 1:1.

Cada página do mosaico traz régua de calibração de 100 mm, rótulo com a posição
da página e marcas de canto para alinhar a colagem. Os arquivos seguem o padrão
`<nome>-c<coluna>-r<linha>.svg`, com índices a partir de 1. Grade de referência
(100/500 mm), calibração e guias podem ser ligadas/desligadas pelos parâmetros de
`doc.exportSvg`.

### PDF 1:1, 3D, robô e matriz de coordenadas

Toda pista gera uma **tabela de coordenadas** (linha central amostrada e
ordenada pelas conexões). Ela é a base do simulador e de todas as saídas de
produção:

| Coluna | Significado |
|--------|-------------|
| `indice` | posição na sequência (1..n) |
| `s_mm` | distância percorrida desde o início, em mm |
| `x_mm`, `y_mm` | posição da linha central na arena |
| `rumo_deg` | direção do percurso (0° = +X, positivo = horário na tela) |
| `curvatura_1_mm` | curvatura assinada (1/mm) |
| `largura_mm` | largura da faixa no ponto |
| `elemento` | id do elemento de origem |

**Documento → PDF 1:1** grava um PDF vetorial 1:1 (mm convertidos para pontos).
Quando a arena é maior que a página, o desenho é dividido automaticamente em
páginas A4 de 190 × 277 mm com sobreposição de 10 mm, régua de calibração,
rótulo de página e marcas de canto. Cada página traz **pontos numerados**
(`P1 INICIO`, `P2`, …, `Pn FIM`) com as coordenadas `x`/`y` em mm para desenhar
a pista no chão; o passo padrão é 500 mm (`mark_step`) e pode ser alterado.
`centerline` desenha apenas a linha central com `path_width` mm.

**Documento → 3D Blender** gera `pista.obj` + `pista.mtl`: faixa extrudada com
topo, laterais e tampas, piso da arena, linha central e caixas dos marcadores.
A escala padrão é `0.001` (mm → m), o eixo vertical da cena vira +Y (up do
Blender) e o plano da pista fica em XZ. Basta importar o OBJ no Blender; os
materiais `Pista`, `Piso`, `Linha` e `Marcador` vêm no MTL.

**Documento → Robô** grava dois arquivos a partir do nome escolhido:
`<nome>-pontos.csv` (comando, posição, rumo, delta, distância, largura,
velocidade e curvatura por ponto) e `<nome>-instrucoes.txt` (resumo, tabela de
comandos e sequência passo a passo). Comandos: `INICIO`, `AVANCAR`,
`VIRAR_DIR`, `VIRAR_ESQ` e `FIM`. A velocidade sugerida cai nas curvas e a
largura do pincel pode substituir a largura dos elementos.

**Documento → Tabela** grava a matriz de coordenadas em CSV (separador `;`),
com passo padrão de 50 mm. A mesma matriz está disponível na API por
`scene.pathTable` (JSON, com colunas, pontos, trechos e metadados) e o painel
**Análise da pista** mostra a contagem de pontos e o passo usados.

**Documento → Simulador** grava `pista.pfsim.json` (schema 1), um documento
autossuficiente para o simulador: arena, linha central amostrada na ordem de
percurso, trechos por elemento, métricas (comprimento, dificuldade, velocidade
recomendada) e pontos críticos. As convenções de coordenadas e o dicionário de
campos estão em [`docs/formato-pfsim.md`](docs/formato-pfsim.md).

### Edição contextual, análise e validação

Ao selecionar uma curva ou uma reta, uma **mini-faixa** aparece logo acima do
elemento (nunca sobre ele) e fecha ao perder a seleção. As ações e os campos
rápidos são:

- **Curva** — inverter direção, suavizar (vira Bézier), fechar, espelhar,
  raio ±, ângulo ±, duplicar, copiar/colar propriedades e excluir; campos de
  raio (mm), ângulo (°), largura (mm) e dificuldade (Auto, 1–5).
- **Reta** — ajustar comprimento, dividir segmento, inserir curva, duplicar,
  largura, copiar/colar propriedades e excluir; campos de comprimento, largura
  e dificuldade.

O painel **Análise da pista** é recalculado a cada alteração do documento
(`scene.analysis`) e mostra comprimento total, retas, curvas, cruzamentos,
obstáculos especiais, índice de dificuldade (nota de Muito Fácil a Extremo),
tempo estimado e velocidade média recomendada. A dificuldade considera
quantidade e raio das curvas, curvas consecutivas, viradas bruscas, cruzamentos
e elementos especiais; cada elemento pode ter dificuldade manual (1–5) ou
automática (0).

**Visão do Robô** (aba **Exibir**) desenha apenas sobreposições, sem alterar o
traçado: curvas apertadas (< 150 mm), viradas bruscas (> 25°), possíveis perdas
de linha (< 60 mm), trechos críticos de sensor (< 100 mm) e áreas de alta
dificuldade.

**Validar Pista** lista os problemas encontrados e cada item é clicável: a
seleção e o pan vão direto ao trecho apontado. Códigos de validação do traçado:
`track.open`, `track.overlap`, `track.difficulty`, `track.slow`, `curve.tight`,
`turn.sharp` e `crossing.conflict`, além dos códigos de arena, forma, módulo,
porta e conexão.

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

A integração contínua (`.github/workflows/build.yml`) compila **Debug** e
**Release** em `windows-latest`, roda `ctest` nas duas configurações e, no job
Release, gera o ZIP portátil como artefato (`PistaForge-win64`). O submódulo
FastWebUI é baixado automaticamente pelo checkout.

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

## Pacote portátil

O executável carrega ícone e metadados de versão definidos em
`resources/pistaforge.rc` (produto, versão e licença). O ícone é gerado de
forma determinística por um script:

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
```

Para gerar o ZIP de distribuição (Release, com assets web e DLLs do runtime
MSVC):

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
cpack --preset windows-msvc-release
```

O arquivo sai em
`build/windows-msvc-release/PistaForge-0.1.0-win64.zip` e contém:

```
PistaForge.exe
web/                 index.html, styles.css, app.js
vcruntime140*.dll    runtime do Visual C++
msvcp140*.dll
LICENSE
README.md
```

Basta extrair e executar `PistaForge.exe`; não é preciso instalar o runtime do
Visual C++ à parte. A primeira execução copia `web/` para
`%LOCALAPPDATA%\PistaForge\web`, como descrito em "Notas de runtime".

## Estrutura

```
src/core/geometry/   Vec2, BBox, CubicBezier, LineSegment, ArcSegment, SpatialIndex
src/core/scene/      Arena, Element, ModuleInstance, Scene, validação
src/core/analysis/   Métricas, dificuldade e pontos críticos do traçado
src/core/sim/        Matriz de coordenadas (linha central amostrada), CSV e `.pfsim.json`
src/core/io/         `.pftrack` (JSON schema 1), SVG, PDF 1:1, OBJ/MTL e robô
src/app/bridge/      Dispatcher JSON + métodos da API
src/app/shell/       IShell/WebUiShell (FastWebUI)
src/web/             index.html, styles.css, app.js
resources/           ícone (.ico) e metadados do executável (.rc)
tools/               gerador do ícone
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
- O simulador consome `.pfsim.json` (schema 1), versionado e documentado em
  `docs/formato-pfsim.md`.
- Todo o domínio em C++; o frontend só desenha e captura entrada.

## Licença

Distribuído sob a licença **MIT**. Veja [LICENSE](LICENSE).
