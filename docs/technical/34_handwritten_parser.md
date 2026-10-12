# The Hand-Written Front End

This note describes the lexer, the parser and the concrete syntax tree that replace the ANTLR4
grammar: what they are built from, the two layout rules they add to the language, how errors are
found and reported, and how the replacement was checked against the parser it replaces. The
language reference ([02](../language/02_lexical.md), [08](../language/08_top_level_and_grammar.md))
states the same rules for a user; this note states them for someone who changes the code.

The goals, in the language author's words: determinism (how a text is read follows from written
rules, not from a prediction algorithm), efficiency, and line breaks that take part in
delimiting statements. The rulings are in `docs/design-principles.cn.md` 2.10, "语法".

```
text ──Lexer──► TokenBuffer ──Parser──► Tree (CST) ──CstBuilder──► AST ──► GCT ──► GIR
                 12 bytes/token          24 bytes/node              (unchanged from here on)
```

| Part | Files |
|---|---|
| Token kinds, keyword table | `include/camel/parse/syntax/token_kinds.def`, `token.h` |
| Lexer | `syntax/lexer.h`, `src/parse/syntax/lexer.cpp` |
| Node kinds, tree, access | `syntax/node_kinds.def`, `syntax/cst.h` |
| Parser | `syntax/parser.h`, `src/parse/syntax/parser*.cpp`, `parser_impl.h` |
| Errors to diagnostics | `syntax/report.h`, `src/parse/syntax/report.cpp` |
| AST builder | `include/camel/parse/ast/cst_builder.h`, `src/parse/ast/cst_builder.cpp` |
| Driver | `include/camel/parse/parse.h` (`CamelParser`) |
| Differential harness | `tools/parsediff/`, `test/tools/parsediff/` |

## 1. Lexer

One pass over the bytes, a 256-entry class table for the first byte of a token, no allocation
per token, no backtracking beyond the token in hand. A token is

```cpp
struct Token { uint32_t begin, end; TokenKind kind; uint8_t flags; };   // 12 bytes
```

`flags` records what stood between the token and the one before it: `SpaceBefore` (white space
or a comment) and `NewlineBefore` (a line break, also one inside a comment). Both layout rules
are decided on these two bits and nothing else.

Comments (`#…`, `//…`, `/*…*/`) are tokens of the buffer, skipped by the parser. White space is
the text between two tokens. The buffer and the source text are therefore lossless, and token
indices are the same as they were under ANTLR (which numbered hidden-channel tokens too), so a
token range stored in the AST means the same token.

`token_kinds.def` is the only place a keyword or an operator is spelled. The enum, the display
text, the keyword hash table of the lexer, the keyword table of the reference (2.4) and the check
of the editor grammar (`scripts/check-keywords.mjs`) are all generated from or checked against
it.

What needed care, all of it kept as ANTLR read it (checked token for token, section 7):

- **Numbers** carry no sign. `REAL` is `digits . digits exponent?` and is the longest match
  whenever it applies (`007.5`); `1.` and `1..2` leave the dot. `INTEGER` is decimal, `0` and
  octal digits, `0x…`, `0b…`, then an exponent; a digit run that is none of these is cut where
  the longest of them ends (`089` is `0`, `89`). `0xFFe-3` is one token: the final `e` goes to a
  signed exponent.
- **Strings** `'…'`, `"…"`, `` `…` `` end at the first unescaped quote; `\\` and an escaped
  quote do not close; a string may span lines. When no quote closes it, it ends at its last
  escaped quote if there is one (`'C:\dir\'`), else it is unclosed and takes the rest of the
  file. `'''…'''` and `"""…"""` run to the first closing triple, without escapes; without one,
  the first two quotes are an empty string.
- **Identifiers** are ASCII: `[A-Za-z_][A-Za-z_0-9]*`. A lone `_` is the wildcard token.
- **Operators** are matched longest first (`===`, `!==`, `?->`, `?..`, `...`, then two
  characters, then one). `>=` is always one token; there is no `>>`.

Lexical errors are recorded in the buffer and their text kept as `Error` tokens, which the
parser skips: a run of characters that start no token (one error per run), an unclosed string,
an unterminated block comment (ANTLR read its `/*` as `/` and `*`; the only place the token
streams differ).

## 2. Rule 1: angle brackets touch

> The `<` of `with` arguments and of type arguments touches the token before it. `f<a>` is `f`
> with an argument; `f < a` is a comparison.

**Opening.** A lone `<` is lexed as `LAngle` when nothing separates it from the previous token
and that token is one an operand can end with: a name, a number, a string, `true`, `false`,
`null`, `)`, `]`, `}`, `>`, `!`. Every other `<` is `Lt`. `<=` is always the comparison.

- `LAngle` is taken by the postfix loop of the operand it touches (`annoExpr`), by a type
  (`specType`) and by an annotation (`@name<args>`). It is never a comparison.
- `Lt` is the comparison. It also opens a *parameter* list, where no expression precedes it and
  nothing can be confused: `with <T: type>`, the lambda `<k: int>(x: int) => …`, `type Pair <T>`.

So **a comparison needs a space before `<`**: `i<n` opens an argument list, and the parse fails
a few tokens later. That failure is reported as `SyntaxDiag::ComparisonNeedsSpace`, at the `<`,
when it happens on the line of the `<`; a list that goes wrong further down is reported where it
goes wrong, with a note at its `<`.

**Closing.** Directly inside an angle-bracket list (not inside a bracket or a block nested in
it), a `>` after a complete expression

- closes the list when it touches the token before it, or is the first token of its line;
- is the comparison when it follows a space on the same line.

`xs->filter<(x: int): bool => x > 1>` therefore reads as written: the first `>` follows a space,
the second touches `1`. `a<b<c>>` closes twice. After a *type* or a parameter without default
value there is no expression to continue, and the `>` closes wherever it stands
(`with <T: type >`). `f<a, b >` leaves the list open; the failure that follows is reported at
that `>` as `SyntaxDiag::AngleCloseAfterSpace`. Outside angle-bracket lists `>` is the
comparison with or without a space (`v>-2`).

The parser keeps, per bracket level, the `<` of the list it is directly inside (`angle_`);
entering `(`, `[`, `{` or a block clears it, leaving restores it.

The other postfix forms follow the rule they always had in the grammar (the `isAdjacent()`
predicate), now enforced: a call `f(x)`, an index `xs[i]` and the postfix `!` touch the
expression before them.

## 3. Rule 2: a line break ends a statement

> A statement ends at a line break unless the line is visibly unfinished.

A recursive-descent parser needs little for this: whatever the grammar *requires* next is taken
from wherever it stands, on this line or a later one. Only the places where the grammar offers a
choice between going on and stopping look at the line break, and they are the operator loops.

| Situation | Reading | How |
|---|---|---|
| inside `( )`, `[ ]`, `< >`, a struct literal or type `{ }`, a pattern | the line break means nothing | `nl_` is off inside (`Scope`) |
| the line ends with an operator, `,`, `=`, `=>`, `->`, `:`, `then`, `else`, `return`, … | continues | the operand, value or clause is required |
| the next line starts with an operator that can only be binary | continues | the loops below |
| the next line starts with `-`, `+`, `@`, `(`, `[`, `{`, `!`, `~`, a name, a literal, a keyword that starts a statement | new statement | the loops below; `(`, `[`, `!` already need to touch |
| the next line starts with `else`, `then`, `catch`, `finally`, `case`, `from`, `,`, `=>`, `)`… | continues | they cannot start a statement and are asked for by the construct |

Tokens at the start of a line, in full:

| Continue the expression | Start a statement |
|---|---|
| `->` `?->` `.` `..` `?..` `??` `!!` | `-` `+` (sign of a number, negation) |
| `*` `/` `%` `^` | `@` (annotation of a function declaration) |
| `&&` `\|\|` `==` `!=` `===` `!==` `<` `>` `<=` `>=` | `!` `~` |
| `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | `(` `[` `{` |
| `as` `is` | names, literals |
| in a type: `\|` `&` `\` | in a type: `+` `-` `[` `?` `!` |

Two entries need a word.

- `@` is both the matrix product and the mark of an annotation. A line starting with `@` is read
  as an annotation, by the principle of the rule (a token that can start a statement does). The
  author's table listed `@` among the binary operators; the corpus has no line that starts with
  a binary `@` and has annotations at the start of a line throughout. **Open for the author.**
- `<` at the start of a line continues as a comparison, as the table says, although a lambda
  with `with` parameters can start with `<`. A statement that is a bare lambda does nothing.

A line starting with `+` cannot be a statement (there is no unary plus before a name) and is
reported as `SyntaxDiag::LineStartsWithOperator`, which says how a continued expression is
written. A line starting with `-x` is a statement.

Two statements on one line need no `;` between them, as before (`stmtList : stmt (SEP? stmt)*`):
on one line the operator loops behave as they always did, so nothing new can be confused, and
the first statement ends where its expression cannot go on. `;` separates statements wherever
the author wants to be explicit.

`return`, `throw`, `let x =`, `import … from` and the like require what follows them, so a line
break after them continues. There is no `return` without a value in the grammar.

## 4. Grammar and decisions

One function per production, named after it (`parseFuncDecl`, `parseRelational`, …); the
production is the comment above the function. The productions are those of the ANTLR grammar.
`node_kinds.def` lists them once more with the node kind each one builds.

Expressions are parsed level by level, one loop per level of the precedence table (the table is
at the top of `parser_expr.cpp`). The levels are those of the grammar, including what is
peculiar to it: the right operand of `??` and `!!` is a whole `dataExpr`; a prefix `!`, `-`, `~`
takes a `linkExpr` and cannot be followed by `as`; a `-` that touches a number is the sign of
the literal when no value precedes it (`-2->f` is `(-2)->f`, `- 2->f` is `-(2->f)`).

No function backtracks. Where one token does not decide, a bounded number of tokens after it is
read (`peek`), never a whole bracket:

| Decision | Looks at | Rule |
|---|---|---|
| `(`: lambda, or tuple / parenthesized expression | ≤ 4 tokens | lambda iff `(name:`, `(var`, `()` or `(,)` followed by `=>` or `:` |
| `(` in a type: function type, parenthesized type, tuple type | ≤ 4 tokens | function type iff `(name:`, `(var`, `()`/`(,)` followed by `=>`; then `(T)` is parenthesized, anything else a tuple |
| `{`: struct literal or block | 3 tokens | struct iff `{ name :` or `{ ...`; `{}` is an empty block as a branch arm or a lambda body and an empty struct as a statement (the order of the alternatives in `blockExpr` and `stmt`) |
| `{` in a braced pattern | 3 tokens | pairs iff `{ name :` or `{ ...` |
| `<` where an operand starts | 1 token | the `with` parameters of a lambda |
| modifiers (`sync`, `macro`, …) at a statement | the modifiers + 1 | `func` after them: a declaration; `{` after a lone `sync`: a block; else a lambda |
| `export`, `inner`, `outer` | ≤ 3 tokens | `export let/var/type/{`, `export inner type`: an export; else a function |
| `import x …` | 2 tokens | `x from`: the imported name; else a module path |
| arguments: positional or named | 2 tokens | `name :` starts the named ones; a `...` before that is positional |
| `[e for …]` | after the first element | `for` makes it a comprehension |
| `T ?` | the token after `?` | `T ? U` (reserved) iff a type starts there on the same line, and not `{` before a block |
| `T + U`, `T - U`, `T ^ U` after a type in an expression | 1 token | the type keeps the operator iff a type can start after it |

The worst case of any decision is the number of modifiers in a row plus a constant.

**Depth.** Expressions, types and blocks count their nesting; beyond 400 levels the parser
reports an error instead of exhausting the stack.

**Adding a form.** Suppose `while cond { body }` became a statement:

1. `token_kinds.def`: `CAMEL_KEYWORD(KwWhile, "while", Control)`. The lexer, the dumps, the
   keyword check of the editor grammar and the reference table follow.
2. `node_kinds.def`: `CAMEL_NODE(WhileStmt, Other) // 'while' logicalOrExpr stmtBlock`.
3. `parser.cpp`: a `parseWhile()` of six lines (`bump`, `parseLogicalOr`, `parseStmtBlock`,
   `close`), one `case` in `parseStmt`, one in `startsStmt`.
4. `cst_builder.cpp`: `visitWhileStmt` and one `case` in `visitStmt`.
5. The formatter and the syntax index each get one `case`.
6. The production in `docs/language/08` (`scripts/check-grammar.mjs` fails until it is there).

Nothing else changes: no tables to regenerate, no prediction to re-check. For an expression form
(`loop … while … { }` as an operand) step 3 is a `case` in `parseExpr` or `parsePrimary`
instead.

## 5. The tree

```cpp
struct NodeData { uint32_t firstChild, childCount, firstToken, lastToken, parent;
                  NodeKind kind; uint8_t flags; };                            // 24 bytes
```

A `Tree` owns the token buffer and three vectors: nodes, children (a 32-bit word each: a node
index, or a token index with the top bit set) and errors. Nodes are appended when a production
closes, children are copied from a scratch stack at the same moment, so the children of a node
are contiguous and a subtree is a contiguous range of both vectors. There is no object per node,
no virtual call, no pointer to fix up; a tree is freed by freeing three vectors.

`Node` is a handle (tree, index). Children are reached by kind (`decl.get<cst::IdentRef>()`,
`params.all<cst::KeyParamPair>()`), by category (`pair.expr()`, `pair.type()`), by token kind
(`decl.tok(TK::KwExport)`) or by position. The typed handles `cst::X` are generated from
`node_kinds.def`; they add nothing but the kind in the signature of the function that takes
them. Lists (`all<>`, `exprs()`) are views that walk the parent's children; nothing is
allocated.

**Pass-through levels leave no node.** `a + b` is an `AdditiveExpr` whose operands are two
`RefData` nodes; the fourteen levels between `dataExpr` and `primaryData` exist only where an
operator of the level is present. The same holds for types and for the rules that only choose
(`decl`, `stmt`, `blockExpr`, `carrier`). A consumer dispatches on the kind of the child
(`visitExpr`, `visitType`, `visitStmt`, `visitDecl`). The tree of a typical file has about a
third of the nodes of the ANTLR parse tree.

**Why a concrete tree, and not the AST directly.** The formatter and the language server need
every token and every comment in place, also in a file with errors; the AST drops both and is
only built from a correct file. One tree for all four consumers (AST builder, formatter, syntax
index, `inspect --cst`) also means one place where the grammar is implemented. The cost was
measured (section 8): building the tree is a small part of the front end, whose time is
dominated by the AST builder and its `shared_ptr` nodes. A direct AST path would save the tree's
memory, not noticeable time, so there is none.

## 6. Errors

The parser always returns a tree. At an error it records a `SyntaxError` (the token, what would
have fitted, and what the parser was doing where that says more), sets `panic_`, and unwinds:
every production checks nothing further, loops stop, nodes close with what they have. `panic_`
suppresses further errors until the nearest statement loop **resynchronizes**: it skips to a
`;`, to the `}` of its block, to a line that starts with a token a statement (at top level: a
declaration) can start with, or to a declaration keyword, stepping over bracketed groups whole.
What is skipped becomes an `Error` node, so the tree still covers every token. Rule 2 is what
makes the line a reliable place to resume.

A file therefore gets **one diagnostic per independent mistake**, not only the first as under
ANTLR, whose recovery produced follow-up errors. A file with a lexical error is still not given
syntax errors: the parser saw the tokens without the offending text.

`report.cpp` turns each error into a diagnostic. The codes are those of
[30_diagnostics_reporting.md](30_diagnostics_reporting.md), with the same names and numbers:

| Code | Raised when |
|---|---|
| `ExtraSemicolon` | a `;` right after a `;` |
| `UnmatchedClosingDelimiter` | a closing bracket closes nothing (bracket scan of the tokens) |
| `MissingClosingDelimiter` | the parser needed `)`, `]`, `}`; or a bracket is open where a statement starts or the file ends |
| `MissingThen` | the condition of an `if` is not followed by `then` |
| `AssignmentInCondition` | … but by `=` |
| `KeywordAsIdentifier` | a reserved word where a name is required (now also after `.` and in a path) |
| `MissingComma` | a list item is followed by neither `,` nor the closing bracket |
| `MissingInitializer` | `let x` without `=` |
| `MissingToken` | the parser needed exactly one token |
| `UnexpectedToken`, `UnexpectedEndOfFile` | anything else, with what would have fitted |
| `ComparisonNeedsSpace`, `AngleCloseAfterSpace` (new) | rule 1, section 2 |
| `LineStartsWithOperator` (new) | rule 2, section 3 |
| `LexicalDiag::UnterminatedComment` (now raised) | `/*` without `*/` |

## 7. Verification against the ANTLR parser

See [section 9](#9-differential-results) for the numbers. The method:

- **Lexer** (`camel-parsediff lexdiff`, `lexfuzz`): both lexers on every file and on random
  character edits, compared as (kind, first byte, last byte) per token.
- **Parser** (`camel-parsediff diff`): both front ends on every file; the *abstract* trees are
  compared, since the concrete ones differ by design. The comparison is of a normalized dump
  (`tools/parsediff/ast_dump.h`): every node's load, its token range, its origin (byte span,
  label, parent) and its semantic bundle, then the diagnostics.
- **Fuzzing** (`camel-parsediff fuzz`): token-level mutations of the corpus (delete, duplicate,
  swap, insert, truncate, splice two files, change the white space before a token). A difference
  is attributed automatically: to rule 2 if the new parser with the rule switched off agrees
  with ANTLR, to rule 1 if an angle bracket is involved, otherwise it is written out for
  inspection.

## 8. Measurements

See the end of this note.
