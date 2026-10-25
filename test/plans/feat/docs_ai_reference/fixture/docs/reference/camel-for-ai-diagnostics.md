# Camel for AI — diagnostics (proof case)

The reduced Appendix A: the diagnostics region, in a document of its own as the real one is, so that
the catalogue does not count against the page.

## Diagnostics

<!-- camel-for-ai region: diagnostics (generated from diagnostics/registry.tsv; node scripts/camel-for-ai.mjs --write) -->
| Name | What the compiler says |
|---|---|

**Syntax — errors** (`SyntaxDiag`) — 1

| `KeywordAsIdentifier` | '{0}' is a keyword and cannot be a name here |

**Semantic — errors** (`SemanticDiag`) — 2

| `UnresolvedReference` | '{0}' is not defined |
| `DuplicateCollectionKey` | The key '{0}' is written twice |

**Run time and passes — warnings** (`RuntimeDiag`) — 1

| `IgnoredSideEffect` | The call of '{{0}}' is dropped and its effect is lost |
<!-- camel-for-ai end: diagnostics -->