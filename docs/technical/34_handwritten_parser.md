# The Hand-Written Front End

This note describes the lexer, the parser and the concrete syntax tree that replace the ANTLR4
grammar: what they are built from, the two layout rules they add to the language, how errors are
found and reported, and how the replacement was checked against the parser it replaces. The
language reference ([02](../language/02_lexical.md), [08](../language/08_top_level_and_grammar.md))
states the same rules for a user; this note states them for someone who changes the code.

The goals, in the language author's words: determinism (how a text is read follows from written
rules, not from a prediction algorithm), efficiency, and line breaks that take part in
delimiting statements. The rulings are in `docs/design-principles.cn.md` 4, "语法".

```
text ──Lexer──► TokenBuffer ──Parser──► Tree (CST) ──ast::Builder──► AST ──► GCT ──► GIR
                 12 bytes/token          24 bytes/node              (unchanged from here on)
```

| Part | Files |
|---|---|
| Token kinds, keyword table | `include/camel/parse/syntax/token_kinds.def`, `token.h` |
| Lexer | `syntax/lexer.h`, `src/parse/syntax/lexer.cpp` |
| Node kinds, tree, access | `syntax/node_kinds.def`, `syntax/cst.h` |
| Parser | `syntax/parser.h`, `src/parse/syntax/parser*.cpp`, `parser_impl.h` |
| Errors to diagnostics | `syntax/report.h`, `src/parse/syntax/report.cpp` |
| AST builder | `include/camel/parse/ast/builder.h`, `src/parse/ast/builder.cpp` |
| Driver | `include/camel/parse/parse.h` (`CamelParser`) |
| Corpus checks (once the differential harness) | `tools/parsecheck/`, `test/tools/parsecheck/` |

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
of the editor grammar (`editors/vscode/scripts/check-grammar.mjs`) are all generated from or checked against
it.

What needed care, all of it kept as ANTLR read it (checked token for token, section 7):

- **Numbers** carry no sign. `REAL` is `digits . digits exponent?` and is the longest match
  whenever it applies (`007.5`); `1.` and `1..2` leave the dot. `INTEGER` is decimal, `0x…`,
  `0b…`, `0o…`, then an exponent on a decimal number; `0xFFe-3` is one token: the final `e` goes
  to a signed exponent. A decimal digit run that starts with `0` and has another digit (`09`,
  `077`) used to be cut into two tokens; it is now refused as one run, with a located
  `LexicalDiag::InvalidNumberFormat`, and a prefixed form with no digit of its base (`0o8`,
  `0x`) is refused the same way. Octal is written `0o77` (H15i).
- **Strings** `'…'`, `"…"`, `` `…` `` end at the first unescaped quote; `\\` and an escaped
  quote do not close; a string may span lines. When no quote closes it, it ends at its last
  escaped quote if there is one (`'C:\dir\'`), else it is unclosed and takes the rest of the
  file. `'''…'''` and `"""…"""` run to the first closing triple, without escapes; without one,
  the first two quotes are an empty string.
- **Identifiers** are ASCII: `[A-Za-z_][A-Za-z_0-9]*`. A lone `_` is the wildcard token.
- **Operators** are matched longest first (`===`, `!==`, `?->`, `?..`, `...`, then two
  characters, then one). `>=` is always one token; there is no `>>`.

Lexical errors are recorded in the buffer and their text kept as `Error` tokens, which the
parser skips: a run of characters that start no token (one error per run), a number run that is
not a number token (`09`, `0o8`), an unclosed string, an unterminated block comment (ANTLR read
its `/*` as `/` and `*`; the only place the token streams differ).

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

- `@` is both the matrix product and the mark of an annotation. A line starting with `@` is an
  annotation (ruled on October 13): a symbol of two roles (`+`, `-`, `@`) that starts a line has
  the role that starts something new. A matrix product continued on the next line leaves the
  `@` at the end of the line before.
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
`node_kinds.def` lists them once more with the node kind each one builds, and section 8.5 of
the language reference is generated from the comments (`scripts/check-grammar.mjs`): there is one
grammar, written where it is implemented.

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
| modifiers (`sync`) at a statement | the modifier + 1 | `func` after it: a declaration; `{` after a lone `sync`: a block; else a lambda |
| `export` | ≤ 3 tokens | `export let/var/type/{`: an export; else a function |
| `import x …` | 2 tokens | `x from`: the imported name; else a module path |
| arguments: positional or named | 2 tokens | `name :` starts the named ones; a `...` before that is positional |
| `[e for …]` | after the first element | `for` makes it a comprehension |
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
4. `src/parse/ast/builder.cpp`: `visitWhileStmt` and one `case` in `visitStmt`.
5. The formatter and the syntax index each get one `case`.
6. The production in `docs/language/08` (`scripts/check-grammar.mjs` fails until it is there).

Nothing else changes: no tables to regenerate, no prediction to re-check. For an expression form
(`loop … while … { }` as an operand) step 3 is a `case` in `parseExpr` or `parsePrimary`
instead.

### The minus, and `&` `|` beside `&&` `||` (rulings of October 13)

The minus is always an operator; the parser no longer takes a `-` that touches a number into
the literal. Its level is its own (`negExpr`, the first operand of `linkExpr`): tighter than
`->` and every other binary operator, looser than `^`, which under a minus is taken by
`negPower`. So `-2 ^ 2` and `-x ^ 2` are `-(… ^ 2)`, `-2->f` and `-x->f` are `(-…)->f`. Without
a minus nothing changed: `x->f ^ 2` is still `(x->f) ^ 2` through `powerExpr`, whose right
operand reaches `negExpr` (`2 ^ -1`). The AST builder turns a minus applied directly to a
number literal into the negative literal, which keeps `-9223372036854775808`, typed literals
(`let b: i32 = -2147483648`) and every snapshot of `-1` as they were; `-(2)` and `-2 ^ 2` are
negations. `!` and `~` keep their place (they apply to the `->` chain after them). In a pattern
(`case -1 =>`) the sign is still part of the literal.

`orExpr` (`|`) and `andExpr` (`&`) sit between `&&` and the comparisons, `&` the tighter. In
the AST they are `BinaryDataOp::Or` and `And`, the ordinary operators that become calls of
`__or__` and `__and__`; `||` and `&&` are `LogicalOr` and `LogicalAnd`, for the stage that will
turn them into branches (the GCT builder compiles them as `|` and `&` until then). In a type
`|` and `&` are parsed by the type functions and never meet these.

### The `loop` expression (provisional notation)

```
loopExpr  : 'loop' loopState '=' logicalOrExpr (loopFor loopWhile? | loopWhile) stmtBlock
loopState : identDef | parentIdents
loopFor   : 'for' identDef 'in' logicalOrExpr
loopWhile : 'while' logicalOrExpr
```

`loop` and `while` are keywords. The CST node `LoopExpr` has the children `loop`, the state
(`IdentDef` or `ParentIdents`), `=`, the first value, `LoopFor`?, `LoopWhile`?, `StmtBlock`. The AST
node is `AST::LoopExprLoad` (`include/camel/parse/ast/data.h`), which the next stage is to turn
into the `LOOP` node of the graph:

| | |
|---|---|
| `state()` | the names of the state: one for `loop acc = …`, several for `loop (i, acc) = …` |
| `isTuple()` | whether the state was written as a tuple pattern |
| `binder()`, `hasFor()` | the name `for` binds each round; empty without `for` |
| child 0, Data | the first value of the state; evaluated once, outside the scope of the names |
| child 1, Optional Data | the sequence of `for`; evaluated once, outside that scope; empty without `for` |
| child 2, Optional Data | the condition of `while`; state names and binder in scope; empty without `while` |
| child 3, StmtBlock | the body; same scope; its value is the state of the next round |

The value of the expression is the last state. With both clauses a round runs while an element
is left and the condition holds. The GCT builder reports `ConstructNotSupported` at the loop
for now; the formatter, the syntax index (scopes and bindings) and the editor grammar know the
form.

What the notation leaves awkward or open (for the language author):

1. **The body block and struct literals.** The body is always a block, so `{ x: 1 }` after the
   head is a block whose first statement fails at the `:`; a struct state needs `{ { x: 1 } }`.
   Elsewhere (`if` arms, lambda bodies) `{ name: …` is read as a struct literal; here the block
   was preferred because the body is mandatory and most states are not structs.
2. **Where the first value, the sequence and the condition end.** They end at `for`, `while` or
   `{`. A `{` never continues an operator expression, so there is no ambiguity as long as they
   are operator expressions; `if`, `match`, a lambda with an expression body, and `??` (whose
   right side is a whole expression) would swallow the `{` or the keyword, so the three are
   restricted to `logicalOrExpr` and anything else goes in parentheses. `loop a = x ?? 0 while …`
   therefore reads the `??` operand as `0`, then expects the clause: it works, but only because
   the right side of `??` stops at `while`; `loop a = x ?? if c then 1 else 2 while …` does not.
3. **`loop` and `while` as keywords** take two common names away. 18 files of the repository (57 places)
   named a function `loop` (the usual name of a tail-recursive helper); they were renamed to `spin`.
4. **A loop without a state** (`loop for x in xs { println(x) }`) is not in the notation: the
   ruling says effects are iterated with `foreach`.
5. **A tuple state of one name**, `loop (a,) = …`, is accepted and distinguished from
   `loop a = …` (`isTuple()`); whether it should be is not said.
6. `loop … for x in xs while c`: the order `for` then `while` is fixed; `while … for …` is a
   syntax error.

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
(`visitExpr`, `visitType`, `visitStmt`, `visitDecl`).

**Why a concrete tree, and not the AST directly.** The formatter and the language server need
every token and every comment in place, also in a file with errors; the AST drops both and is
only built from a correct file. One tree for all four consumers (AST builder, formatter, syntax
index, `inspect --cst`) also means one place where the grammar is implemented. The cost was
measured (section 9): building the tree is a small part of the front end, whose time is
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

The ANTLR grammar, the generated parser and its runtime were removed once every consumer had
moved. Before that the two front ends lived side by side and were compared by a harness (it
is in the history as `tools/parsediff`; what needs no second parser lives on as
`tools/parsecheck`):

- **Lexer**: both lexers on every file and on random character edits, compared as (kind, first
  byte, last byte) per token.
- **Parser**: both front ends on every file; the *abstract* trees are compared, since the
  concrete ones differ by design. The comparison is of a normalized dump
  (`tools/parsecheck/ast_dump.h`): every node's load, its token range, its origin (byte span,
  label, parent) and its semantic bundle, then the diagnostics.
- **Fuzzing**: token-level mutations of the corpus (delete, duplicate, swap, insert, truncate,
  splice two files, change the white space before a token). A difference was attributed
  automatically: to rule 2 if the new parser with that rule switched off agreed with ANTLR, to
  rule 1 if an angle bracket was involved, to the two rulings of 2026-10-12 (`raise`, the minus
  of a literal) if the mutant used them; anything else was written out and read.

### Results (the last run, on the commit before the removal)

Corpus: 3679 files. 998 `.cml` files of the repository, 481 `cml` blocks of the Markdown
documents, 2000 programs of the random generator `test/tools/progen` and 200 at `--size 4`.

| Check | Result |
|---|---|
| Lexers, 3 533 142 tokens | identical, except the two files that use `raise` (a keyword there, a name here) |
| Lexers, 200 000 random character edits | identical; 6 265 mutants had an unterminated comment (reported here, read as `/` `*` there) |
| ASTs, 3679 files | 3420 identical; 248 rejected by both; 11 different (below) |
| Probes of the grammar's decisions, 220 | 206 identical; 8 rejected by both; 6 different (below) |
| Fuzzing, 80 000 mutants (ANTLR accepted 12 991) | rule 1: 261, rule 2: 49, minus: 24+, `raise`: 17+, other: 8 in two classes (below); the ANTLR side crashed on 11 |

The 11 corpus files that differ, all by a rule or a ruling:

| Files | Cause |
|---|---|
| `test/cases/compile/surface/angle_then_name_line.cml`, `angle_then_string_line.cml`, and five document blocks that quote them or `f<a>(x), g<b>(y)` | rules 1 and 2: the angle-bracket ruling of 2026-10-12, now read as written |
| `test/cases/consistency/surface/minus.cml`, `test/cases/format/minus.cml`, `test/plans/feat/parse/golden/numbers_format.cml` | `- 1` is a negation, not the literal `-1` (same value) |
| `test/cases/compile/semantic/raise_is_a_name.cml` | `raise` is a name |

Two files of the repository had to be rewritten to mean the same under the rules (commit
`test: write two cases the way the layout rules of the new parser read them`): one comparison
`v<-2` and one expression continued on two lines that started with `+`.

How much existing code the two rules touch, measured on the ANTLR parse of the corpus before the
new parser existed (`camel-parsediff stats`, 3431 accepted files, 195 026 statements):

| | Count |
|---|---|
| Rule 1. `<` as a comparison, with a space before it | 2195 |
| `<` as a comparison touching its left operand | 8: one real (`v<-2`), seven that are finding 12 misread |
| Angle-bracket arguments touching the callee | 61 371 |
| Angle-bracket arguments after a space | 0 |
| Closing `>` touching the last item | 62 022 |
| Closing `>` first on its line | 3 (accepted) |
| Closing `>` after a space on the same line | 0 |
| `>` as a comparison inside an angle list, after a space | 547 (e.g. `x > 1>` in a lambda) |
| `>` as a comparison inside an angle list, touching | 7, all finding 12 misread |
| Rule 2. Statements continued over a line break | 501 |
| ... inside brackets | 990 line breaks |
| ... next line starts with `else` / `then` / `->` | 36 / 6 / 32 |
| ... line ends with `then` / `else` | 3 / 1 |
| ... in a function header (`@anno`, `with <...>`, `export`, modifiers, then `func` on the next line) | 270 |
| ... next line starts with `+` or `-` (now a new statement) | 2, in one file |
| Two statements on one line without `;` | 1 (a formatter test input) |

### Where the new parser does not read what ANTLR read, beyond the rules

ANTLR decided several places by prediction over the rest of the input. The new parser decides
each by a written rule (section 4). The rules were chosen to agree with ANTLR wherever the
probes and the fuzzer could tell; what is left:

1. **`xs[():1]`**, a slice whose first bound is the empty tuple. ANTLR found the slice; here
   `():` is the head of a lambda with a return type, and the text is rejected.
2. **Statements run together that only split one way.** `acc = acc + 1.0 = acc relu(acc)` on one
   line was accepted by ANTLR as `acc = acc`, `+1.0 = acc`, `relu(acc)`: the only split that
   parses. Here the first statement takes `acc + 1.0` and the `=` after it is an error.
3. **`typeof a = b`** as a type: the operand of `typeof` now stops before an assignment, which
   is what ANTLR chose in `let v: typeof a = b` and not where an assignment was the only reading.

Kept as ANTLR read them, though a reader may expect otherwise (for the language author):

- `x as T + U`, `x as T - U`, `x as T ^ U`: the operator belongs to the type when a type can
  follow it.
- `x as T? y`: the `?` is the nullable mark and `y` the next statement.
- `{}` as a statement is an empty struct literal, as a branch arm or lambda body an empty block.
- `xs[]` is `xs` and then the empty array `[]`, two statements.
- `f(a, ...b, k: 1, ...c)`: `...b` is a positional spread, `...c` a named one.

A defect of the old front end found on the way: `import {} from a` and `export {}` crashed the
ANTLR-based AST builder (a missing identifier list was dereferenced). They now build an import
without names.

## 8. Checks that remain

| Test (`test/plans/feat/parse/frontend.plan.toml`) | What it holds |
|---|---|
| `grammar.reference_in_step` | the productions above the parser's functions, the grammar blocks of the language reference, the token table and the node kinds agree (`scripts/check-grammar.mjs`) |
| `frontend.ast_snapshot` | every case of the parse corpus (`test/cases/parse/**`) and every probe gives the AST recorded at the last comparison with ANTLR (`golden/ast_hashes.txt`, a hash per file of the normalized dump). The snapshot is of the parser's tests, not of the repository (H188): other `.cml` files are checked by the plan that owns them |
| `frontend.format_keeps_ast` | formatting an accepted file gives a text with the same AST |
| `frontend.fuzz_lossless_tree` | for mutated programs the parser returns a tree whose leaves are exactly the tokens, and the AST builder accepts every tree without errors |

`python3 test/tools/parsecheck/check.py fuzz N SEED` runs more; 183 000 mutants over four seeds
and 100 000 over the corpus with the generated programs gave no failure.

## 9. Measurements

Conditions: Linux x86-64 in a container on a laptop (Intel Core Ultra 5 125H, 18 logical CPUs
visible, which of them are performance cores is not visible from the container), Clang, Release
build with LTO, the process pinned to one logical CPU with `taskset`. **The machine was busy
with other builds throughout (load average 15 to 25)**; the figures are minima over the rounds,
and a second run of the same measurement gave minima up to 1.5 times higher or lower for both
front ends. They show orders of magnitude, not percentages. Each round ran the steps of both
front ends in turn.

Time per step, microseconds (minimum of 41 rounds; of 5 for the last file):

| File | Lines | Tokens | CST nodes | Lexer | Lexer + parser | To the AST | ANTLR to its tree | ANTLR to the AST |
|---|---|---|---|---|---|---|---|---|
| `stdlib/array.cml` | 171 | 1 068 | 972 | 11 | 55 | 658 | 43 033 | 44 940 |
| generated program, size 4 | 377 | 4 155 | 3 868 | 58 | 193 | 4 172 | 20 654 | 25 067 |
| generated program, size 30 | 2 999 | 34 260 | 31 333 | 597 | 1 631 | 35 010 | 193 536 | 305 220 |
| 16 000 lines of the corpus in one file | 16 322 | 95 946 | 94 273 | 1 867 | 8 649 | 281 881 | 11 428 450 | 11 760 012 |

- The lexer takes about 17 to 20 ns per token; lexer and parser together 50 to 90 ns per token.
- **The AST builder is where the front end's time goes now** (95% and more of "to the AST"):
  about 1 µs per CST node, spent in what the AST is made of, a `shared_ptr` node and load per
  node, an origin and a span record per node, semantic bundles with strings. That code was
  ported, not redesigned; the AST's shape was not to change in this work.
- A first pass over the builder (October 13) removed two thirds of its allocations without
  changing the tree (the AST hash snapshot is unchanged): the nodes and loads of a tree come
  from an arena that the tree keeps alive (`NodeArena`), a builder function returns a `Value`
  with room for a node pointer where `std::any` allocated, and the span and origin tables are
  reserved once per file. On a 3 300-line file (39 985 tokens, 35 041 CST nodes) one front-end
  round went from 158 531 calls of `malloc` to 52 119, and "to the AST" from a minimum of
  13.7 ms to 10.8 to 11.9 ms (lexer and parser: 0.9 ms), on the same busy machine; on files of
  a few hundred lines the difference is within the noise. **The aim, the same order of
  magnitude as lexer and parser, is not reached.** Sampling shows no single place any more:
  the origin and span records, the semantic bundles with their strings and hash table, the
  construction and the destruction of some 39 000 nodes with their loads and child vectors
  each take a tenth to a quarter. Going further means recording less per node (origins made
  on demand, labels that are not strings), which changes what the later stages read.
- ANTLR's time on the small file is dominated by building its prediction tables on first use;
  on the large file by the full-LL fallback. Its time grows faster than the input.

Peak memory of the process that parses the file and prints the AST dump (maximum resident set):
11.4 MB against 13.9 MB, 11.5 against 19.1, 30.5 against 71.8, 71.6 against 180.6 for the four
files.

Whole process, minimum wall time of 30 runs: `camel hello.cml` 6.8 ms against 30.8 ms;
`camel check -s hello.cml` 4.8 ms against 14.2 ms.

Size: `libcamel.so` 10 419 568 bytes before the removal, 8 188 384 after (2.2 MB less); the
`camel` executable is unchanged (832 256 bytes).

Build: `libcamel` has some 140 translation units fewer (136 of the ANTLR runtime and the
generated lexer, parser and visitors), no generation step, and needs neither Java nor the ANTLR jar. The build time was not measured: at
this load a full build took more than ten minutes before and after, and the difference would
have been noise.

Not verified: Windows (Clang with the MSVC standard library) and macOS (libc++). The new code is
plain C++23 without platform-specific parts; the removal of the ANTLR headers uncovered standard
headers that had been included only through them (`<deque>`, `<mutex>`, `<any>` were added where
the Linux build needed them), and other platforms may need more of the same.
