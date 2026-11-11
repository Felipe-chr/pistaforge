# ADR 0002 — Formatos JSON versionados e compatibilidade

- **Status:** aceito
- **Contexto:** projetos salvos precisam continuar abrindo em versões futuras,
  e o simulador precisa de um contrato estável.
- **Decisão:**
  - Todo documento tem um campo inteiro `schema`.
  - `.pftrack` usa `schema: 1`; `.pfsim.json` usa `format: "pistaforge.sim"` e
    `schema: 1`.
  - A leitura ignora campos desconhecidos e aplica padrões documentados para
    campos ausentes.
  - Mudanças incompatíveis incrementam o schema; mudanças aditivas não.
- **Consequências:**
  - Arquivos antigos continuam abrindo.
  - O simulador pode validar `format`/`schema` antes de consumir o documento.
  - Os formatos ficam documentados em `docs/`.
