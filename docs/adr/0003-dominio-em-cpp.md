# ADR 0003 — Domínio em C++ e interface só desenha

- **Status:** aceito
- **Contexto:** o PistaForge precisa da mesma geometria para editar, validar,
  imprimir, gerar 3D e alimentar o robô/simulador. Reimplementar regras em
  JavaScript criaria divergências.
- **Decisão:** toda a lógica de domínio (geometria, cena, validação, análise e
  exportação) fica no núcleo C++23. A interface HTML/Canvas apenas desenha e
  captura entrada, chamando a API `doc.*` / `scene.*` pela ponte JSON.
- **Consequências:**
  - Uma única fonte de verdade para medidas e formatos.
  - A UI pode ser substituída sem reescrever regras.
  - Testes de domínio rodam sem abrir janela (`pf_tests`).
