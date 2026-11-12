# Preferências — `settings.json` (schema 1)

Arquivo de configuração do aplicativo, gravado em
`%LOCALAPPDATA%\PistaForge\settings.json`. Não faz parte do projeto
(`.pftrack`): guarda apenas o estado da interface entre execuções.

## Estrutura

```json
{
  "schema": 1,
  "last_dir": "C:\\Users\\maria\\Pistas",
  "recent_files": [
    "C:\\Users\\maria\\Pistas\\pista-a.pftrack",
    "C:\\Users\\maria\\Pistas\\pista-b.pftrack"
  ]
}
```

| Campo | Tipo | Padrão | Descrição |
|-------|------|--------|-----------|
| `schema` | inteiro | `1` | Versão do formato |
| `last_dir` | texto | — | Última pasta usada nos diálogos de arquivo |
| `recent_files` | lista | `[]` | Documentos recentes, do mais novo para o mais antigo |

## Regras

- No máximo **10** arquivos recentes; os mais antigos são descartados.
- Caminhos são gravados em **UTF-8** e comparados sem diferença de caixa
  (Windows), sem duplicar entradas.
- `doc.load` e `doc.save` atualizam a lista e a última pasta automaticamente.
- Salvar uma exportação (SVG, PDF, 3D, robô, tabela ou simulador) também
  atualiza `last_dir`, mas não entra em `recent_files`.
- Arquivo ausente, JSON inválido ou campos estranhos resultam nos padrões
  (lista vazia), sem impedir a inicialização.
- Remover o arquivo restaura o comportamento inicial.

## API

| Método | Descrição |
|--------|-----------|
| `app.preferences` | Devolve `{path,last_dir,recent_files,max_recent}` |
| `app.clearRecent` | Esvazia `recent_files` e regrava o arquivo |
