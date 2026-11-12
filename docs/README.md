# Documentação do PistaForge

Este diretório reúne o formato dos arquivos e as decisões de arquitetura
(ADRs) do projeto.

## Formatos

| Documento | Descrição |
|-----------|-----------|
| [`formato-pftrack.md`](formato-pftrack.md) | Projeto editável (`.pftrack`, schema 1) |
| [`formato-pfsim.md`](formato-pfsim.md) | Documento do simulador (`.pfsim.json`, schema 1) |
| [`formato-settings.md`](formato-settings.md) | Preferências locais (`settings.json`, schema 1) |

## Decisões de arquitetura

| ADR | Tema |
|-----|------|
| [`adr/0001-cruzamentos-em-linha-reta.md`](adr/0001-cruzamentos-em-linha-reta.md) | Cruzamentos sempre seguem em linha reta |
| [`adr/0002-formatos-json-versionados.md`](adr/0002-formatos-json-versionados.md) | Formatos JSON versionados e compatibilidade |
| [`adr/0003-dominio-em-cpp.md`](adr/0003-dominio-em-cpp.md) | Domínio em C++ e interface só desenha |

## Convenções gerais

- Unidades de comprimento em **milímetros**; ângulos em **graus** no JSON.
- Origem no **canto superior esquerdo** da arena, **X para a direita** e
  **Y para baixo**.
- Rumo: `0°` aponta para `+X` e ângulos positivos giram no sentido **horário**
  na tela.
- Curvatura assinada em `1/mm` (positiva = curva para a direita).
- Todo documento traz um campo `schema` inteiro. Campos desconhecidos são
  ignorados na leitura; campos ausentes usam os padrões documentados.
