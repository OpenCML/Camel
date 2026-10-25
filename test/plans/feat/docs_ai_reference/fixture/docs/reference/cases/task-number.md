# Camel for AI (proof case)

A reduced copy of the document the generator reads, with the four regions of the real page. The
plan runs the check on this root to pin what a stale region, a missing marker, an unknown
diagnostic name and a task number are reported as; the sources beside it are reduced copies of the
four real ones, so the case does not move when the language does.

A `**target**` note names its design under `docs/design/`, and a diagnostic is written in full, as
`SemanticDiag::UnresolvedReference`.

## Keywords

<!-- camel-for-ai region: lexical (generated from include/camel/parse/syntax/token_kinds.def; node scripts/camel-for-ai.mjs --write) -->
| Group | Words |
|---|---|
| Declaration | `let` `func` |
| Control | `if` |
| Type and conversion | `as` |
| Literal | `null` |
| Reserved | `is` |

**Operators and separators** (3): `=` `->` `..`
<!-- camel-for-ai end: lexical -->

## Precedence

<!-- camel-for-ai region: precedence (generated from src/parse/syntax/parser_expr.cpp; node scripts/camel-for-ai.mjs --write) -->
```text
 1  dataExpr            -                                  -
 2  waitExpr            wait                               prefix, optional
 3  ctrlExpr            if then else                       construct
 4  assignExpr          =                                  one
 5  logicalOrExpr       ||                                 repeat, left
 6  logicalAndExpr      &&                                 repeat, left
 7  equalityExpr        == !=                              repeat, left
 8  relationalExpr      < >                                repeat, left
 9  additiveExpr        + -                                repeat, left
10  multiplicativeExpr  * /                                repeat, left
11  powerExpr           ^                                  right, one
12  nullableExpr        ?? !!                              right (a whole expression on the right)
13  unaryExpr           as is                              one
14  linkExpr            -> ?->                             repeat, left
15  negExpr             - !                                prefix
16  negPower            - ! ^                              prefix, right
17  compExpr            ..                                 repeat, left
18  annoExpr            !                                  repeat, left
```

The production of each level, as `src/parse/syntax/parser_expr.cpp` writes it above the
function that parses it:

```text
dataExpr           : waitExpr | ctrlExpr
waitExpr           : 'wait'? assignExpr
ctrlExpr           : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
assignExpr         : logicalOrExpr (('=') logicalOrExpr)?
logicalOrExpr      : logicalAndExpr ('||' logicalAndExpr)*
logicalAndExpr     : equalityExpr ('&&' equalityExpr)*
equalityExpr       : relationalExpr (('==' | '!=') relationalExpr)*
relationalExpr     : additiveExpr (('<' | '>') additiveExpr)*
additiveExpr       : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
multiplicativeExpr : powerExpr (('*' | '/') powerExpr)*
powerExpr          : nullableExpr ('^' powerExpr)?
nullableExpr       : unaryExpr (('??' | '!!') dataExpr)?
unaryExpr          : linkExpr (('as' | 'is') typeExpr)?
linkExpr           : negExpr (('->' | '?->') compExpr)*
negExpr            : ('-' | '!') negPower | compExpr
negPower           : ('-' | '!') negPower | compExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | '!')*
```
<!-- camel-for-ai end: precedence -->

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

## Signatures

<!-- camel-for-ai region: stdlib-signatures (generated from docs/stdlib-reference.md; node scripts/camel-for-ai.mjs --write) -->
**`math`** — import { ... } from math — 2 name(s)

```text
pi: float
abs: (x: float) => float
```

**`str`** — import { ... } from str — 2 name(s)

```text
len: (x: string) => int
join: (xs: string[], by: string) => string
```
<!-- camel-for-ai end: stdlib-signatures -->
This paragraph still names the work item H123.
