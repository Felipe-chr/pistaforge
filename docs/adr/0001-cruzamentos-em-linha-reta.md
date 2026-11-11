# ADR 0001 — Cruzamentos sempre seguem em linha reta

- **Status:** aceito
- **Contexto:** pistas de seguidores de linha usam cruzamentos em que o robô
  deve continuar reto, sem virar. Qualquer outra regra exigiria decisão do
  robô no cruzamento e tornaria o percurso ambíguo.
- **Decisão:** todo cruzamento conecta as portas opostas: `w ↔ e` e `s ↔ n`.
  O percurso nunca vira dentro do cruzamento.
- **Consequências:**
  - A tabela de coordenadas e o robô seguem sempre em frente.
  - Não há estado de "escolha de caminho" no simulador.
  - Cruzamentos são representados como elementos com quatro portas.
