# Camel for AI

One file that says everything an AI needs to write Camel that compiles: the rules, the idioms, the
errors a first draft makes, and the exact name of every diagnostic it will meet. It is written to be
read whole — the page is under 59,000 characters, about 16,000 tokens, and the check below keeps it
there. Two catalogues it points at are documents of their own, so that the language growing does not
push this page past what one read costs: [Appendix A](camel-for-ai-diagnostics.md), every diagnostic
with its message, and [Appendix B](camel-for-ai-stdlib-signatures.md), every exported standard
library name with its signature.

The language described here is the development version (`develop`), and it is tied to that tree:
`scripts/camel-for-ai.mjs --check` regenerates the generated regions of all three documents from
their sources, checks every diagnostic this file names against the registry, and fails when they and
the tree disagree. Read it as the language you are compiling against, not as a snapshot of one.

## How to read this file

- **Answers first.** Sections 2–3 are what you need before writing a line. Sections 4–10 are the
  rules of each part of the language. Section 11 is how Camel is written when someone who knows it
  writes it. Section 12 is the most valuable one when a compile fails: it is organised as *what you
  wrote — what the compiler says — what to write instead*.
- **Diagnostics are named in full**: `SemanticDiag::UnresolvedReference`, never bare
  `UnresolvedReference`. [Appendix A](camel-for-ai-diagnostics.md) lists all of them with the exact
  message template; the compiler prints the section-qualified name, so the two can be matched
  mechanically.
- **Fenced blocks tagged `camel` compile.** Every one of them is compiled by the documentation check
  when the test suite runs, so any block on this page is valid Camel as of this tree. A block that
  is deliberately incomplete is marked `camel-snippet: fragment`, and one that is deliberately wrong
  is marked `camel-snippet: counter-example` and says what the compiler answers.
- **Where a rule comes from.** Hand-written sections name their source in the text — a section of
  `docs/design-principles.cn.md` (normative: when it disagrees with anything, it wins) or a page of
  `docs/language/`. Appendix C collects the map.

## What each part costs

Budgets, in characters: this page as a whole (59,000), the hand-written part of it (52,000) and the
two generated regions it holds (7,000 together). The two catalogues are documents of their own and
are not counted here — that is what moving them out bought — and each has a budget of its own:
`camel-for-ai-diagnostics.md` 30,500, `camel-for-ai-stdlib-signatures.md` 30,000.
`node scripts/camel-for-ai.mjs --report` prints the current numbers for all three documents, and
`--check` fails when one of them is exceeded — including the per-chapter budget in the last column
below, which is read out of this table and measured against the chapter it names.

| Part | Answers | Budget |
|---|---|---|
| 2. The shape of a Camel program | what a program is: values, the graph, effects, passes | 3,000 |
| 3. The smallest program that compiles | entry point, module, output, exit | 2,000 |
| 4. Lexical: names, comments, literals | what the lexer accepts, keywords, operators | 3,200 |
| 5. Expressions, operators, precedence | how an expression is read | 8,200 |
| 6. Statements and declarations | functions, parameters, loops, rules, generics, `when` | 6,850 |
| 7. Effects and `sync` | what runs in order, and what the compiler folds | 2,700 |
| 8. Streams | `T..`, `yield`, `tee` | 1,500 |
| 9. Modules and the standard library | imports, exports, namespaces, where the library is | 3,300 |
| 10. Passes, the command line, the graph | how to run a program and look at its graph | 3,000 |
| 11. Idioms | how Camel code is written when it is written by someone who knows | 3,600 |
| 12. Common errors | the error, the diagnostic, the fix | 11,800 |
| Appendix A. Diagnostics | a pointer at `camel-for-ai-diagnostics.md` | 700 |
| Appendix B. Standard library signatures | a pointer at `camel-for-ai-stdlib-signatures.md` | 700 |
| Appendix C. Sources | which normative page each rule comes from | 1,800 |
| — | generated, the two regions of this page | 7,000 |
| — | hand-written, this header and sections 2–12 with Appendices A–C | 52,000 |

## 2. The shape of a Camel program

Camel is not a sequence of statements. A program is a **graph**: an expression is a node, a value
flows along an edge, and a node runs when its result is needed — that is, when it reaches the return
of its function along a data or a control edge. Nothing else decides the order: two independent
expressions have no order between them, and the parallel scheduler runs them side by side (design
principles §§2–3). Four consequences surprise a first draft:

1. **A function is its own subgraph.** A call is one node in the caller's graph that points at the
   callee's subgraph. There is no call stack except for recursion depth (§7).
2. **An effect nothing needs does not run.** `println` is an effect: outside a `sync` block, a call
   whose result nothing uses is dropped, and the compiler warns `SemanticDiag::EffectNotExecuted`.
   `sync { ... }` and `func f(): T sync { ... }` put control edges between the statements written
   *directly* in that block and nothing else. A sync block does not reach into an arm written inside
   it — write `sync` there too (design principles 3.4).
3. **Values do not change.** `let` binds a value; `var` allows rebinding; the standard library never
   mutates its argument, it returns a new value. In-place update is an optimization the compiler
   applies when a variable is the only holder of the value, and only speed observes it (design
   principles 11.3).
4. **Two kinds of parameters.** Ordinary parameters are the data that flows through a function;
   `with` parameters are what configures it — weights, shapes, flags, the function to map with.
   They are written in angle brackets after the name: `xs->map<inc>`, `logits<w>(x)`
   (design principles 4.4).

Errors and faults are different things: a failure the program is expected to handle is a `T!` value,
opened with `!!`, `?->` or `match`, while a fault stops the program and `!!` does not catch it (§7).

What runs the graph is a **pass**: `camel prog.cml` uses the default pipeline, `camel prog.cml
std::fvm` chooses the bytecode VM, and `std::gir` prints the graph instead of running it (§10).

Sources: `docs/design-principles.cn.md` §§2–4, 11.3; `docs/language/04_execution_model.md`.

**Common errors.** A statement at the top level is `SyntaxDiag::UnexpectedToken` at the word the
grammar stopped reading; executed statements belong in a function (§3).

<!-- camel-snippet: counter-example: the top level holds declarations, not statements -->

```camel
let x = 1
println(x)

func main(): int sync {
    return 0
}
```

## 3. The smallest program that compiles

```camel
// hello.cml
// camel hello.cml
func main(): int sync {
    println('Hello, Camel')
    return 0
}
```

```text
Hello, Camel
```

- The file is a module. It needs no `module` declaration unless it wants to name itself or hold
  `export`ed names (§9).
- The entry point is `func main`. Only a `main` with a written return type has its value count as
  the program's result: `int` and `bool` are the exit code, any other type is printed (or written to
  the file named by `-o`), and `void` exits with 0. A `main` with no return type still has a body
  value, but the program ignores it and exits 0.
- `sync` after the return type makes the body run in the written order. Without it `println`'s
  result is unused, so that line would not run at all — the most common first surprise.
- `println(x)` returns `x`, so a function whose body ends with it returns that value, and a value can
  be piped on: `value->println`.
- The parameters of `main` are the program's inputs, given as `--arg name=value`. A missing one is
  the located error `RuntimeDiag::EntryArgumentMissing`, never a default value:

```camel
// greet.cml
// camel greet.cml --arg name=Camel --arg times=2
func main(name: string, times: int): int sync {
    println(`Hello, ${name}! x${times}`)
    return 0
}
```

```text
Hello, Camel! x2
```

**Common errors.** Printing without `sync` is the warning `SemanticDiag::EffectNotExecuted`, and the
line does not run (§7, §12.1).

<!-- camel-snippet: counter-example: an effect nothing uses is dropped -->

```camel
func greet(): int {
    println('Hello, Camel')
    return 1
}
```

## 4. Lexical: names, comments, literals, keywords

- **Comments.** `//` to the end of the line, `/* ... */` across lines, and `#` to the end of the line
  — which is the command form (`#macro` on a declaration, `#leaf` in a pattern, `#impl` behind a
  kernel) rather than a comment.
- **Names.** A name is `[A-Za-z_][A-Za-z_0-9]*`; a qualified name uses `::` (`Shape::Circle`,
  `str::len`). A name with two leading and two trailing underscores (`__add__`) belongs to the
  compiler: using one is `SemanticDiag::ReservedIdentifier`.
- **Numbers.** Decimal `42`, hexadecimal `0xff`, octal `0o17`, binary `0b1010`, reals with a
  fraction or an exponent (`1.5`, `1e-3`), and imaginary literals with a trailing `i` (`4i`,
  `0.5-0.25i`), which are complex values.
- **Text.** `'...'` and `"..."` are the same string type; `'''...'''` and `"""..."""` span lines. A
  backtick string interpolates: <code>`Hello, ${name}!`</code> is the formatted string of
  `'Hello, ${0}!'` and `name`. A string is UTF-8 and immutable, and its length and subscripts count
  code points.
- **Keywords are reserved**: using one as a name is `SyntaxDiag::KeywordAsIdentifier`. The table is
  generated from the token table the lexer reads, grouped as that file groups the words.

### Keywords and operators (generated)

<!-- camel-for-ai region: lexical (generated from docs/language/02_lexical.md §2.4 and include/camel/parse/syntax/token_kinds.def; node scripts/camel-for-ai.mjs --write) -->
| Group | Words |
|---|---|
| Declaration | `let` `var` `func` `type` `with` `module` `import` `export` `from` `namespace` `using` |
| Control | `if` `then` `else` `match` `case` `return` `throw` `sync` `loop` `while` `do` `for` `in` `static` |
| Type and conversion | `as` `const` `constas` `enum` `typeof` |
| Literal | `null` `true` `false` |

**Operators and separators** (55): `;` `,` `:` `::` `.` `..` `...` `(` `)` `[` `]` `{` `}` `<` `<` `>` `<=` `<=>` `>=` `==` `!=` `===` `!==` `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `|=` `+` `-` `*` `/` `%` `^` `@` `#` `&` `|` `&&` `||` `!` `!!` `~` `?` `??` `->` `?->` `=>` `\\` `_`
<!-- camel-for-ai end: lexical -->

**Common errors.** `let if = 1` is `SyntaxDiag::KeywordAsIdentifier`; the name must be outside the
reserved spellings (§12.8).

<!-- camel-snippet: counter-example: a keyword is not an identifier -->

```camel
func main(): int sync {
    let if = 1
    return if
}
```

## 5. Expressions, operators, precedence

**The pipeline is the idiom.** `x->f` is `f(x)`, `x->f<w>` is `f<w>(x)`, and `->` is left
associative, so a computation reads top to bottom as a chain. `x->f(a, b)` is `f(x, a, b)`: the
receiver enters the first call of the name after `->`, so `x->g(1)(2)` is `g(x, 1)(2)`.

```camel
// pipeline.cml
// camel pipeline.cml
func main(): int sync {
    let total = 1..6
        ->map<(x: int): int => x * x>
        ->filter<(x: int): bool => x % 2 == 1>
        ->reduce<(acc: int, x: int): int => acc + x, 0>
    println(`total = ${total}`)
    return 0
}
```

```text
total = 35
```

- `f<w>` on its own is a partial application, not a call: `let g = f<w>` binds the context and
  leaves the data parameters open.
- Type arguments and `with` arguments are different spellings of different things:
  `f::<T><w>(x)`.
- **One call that has `with` arguments per statement.** `f<a>(x), g<b>(y)` in one argument list is
  read as two comparisons, not as two configured calls.

**Access.** `p.x` is a field, `t.0` and `t.1` are tuple elements, `xs[i]` is an element. `xs[idx]`
with an `int[]` takes several positions (the same as `take`), and `xs[a:b]` is a **slice**: a view
sharing the data block with `xs`, left-closed and right-open, along the first dimension only —
`xs[a:b][j]` is `xs[a + j]`. An index outside the array is the fault
`RuntimeDiag::IndexOutOfRange`, an out-of-range slice end is `RuntimeDiag::SliceOutOfRange`. A
postfix `.`, `[...]`, `(...)` or `<...>` must touch what it follows.

**Operators.** Arithmetic `+ - * / %`, power `^` (right-associative), matrix product `@`; `&&` and
`||` are branches — the right side runs only if it decides the result — while `&` and `|` read both
sides and may be overloaded; `==` and `!=` compare by content, so two arrays or two structs with
equal contents are equal. On an array `==` is the whole comparison — the shapes are the same and the
elements are equal everywhere — and gives a `bool`, tensors included, however the element type is
written; `===` and `!==` are element-wise and give a `bool` array of the same shape (on a scalar
`===` is `==`). Numeric types never convert implicitly: `i32` with `int`, or `int` with
`float`, is `SemanticDiag::NoMatchingFunction`, and `let y: float = n` (`n` an `int`) is
`SemanticDiag::DeclaredTypeMismatch`. Only **literals** take the type their context asks for
(`let x: f32 = 1`); variables do not. Convert with `float(n)`, `int(x)`, `string(v)`.

`a..b` is the integer interval [a, b) — left-closed, right-open, an `int[]`, integers only; with two
functions `f..g` is the composition `(x) => g(f(x))`, which can be called, passed on, or used directly
in a higher-order call; `..` binds tighter than `->`, so `x->f..g` is `x->(f..g)`.

```camel
// compose.cml
// camel compose.cml
func inc(x: int): int {
    return x + 1
}

func dbl(x: int): int {
    return x * 2
}

func main(): int sync {
    let g = inc..dbl
    println(g(3), 3->inc..dbl, [1, 2, 3]->map<inc..dbl>)
    return 0
}
```

```text
8 8 [4, 6, 8]
```

The left operand is a function of one parameter, because the composition *is* the one-parameter
lambda: `add2..inc` for a two-parameter `add2` has no reading (`SemanticDiag::ArgumentsMismatch`).

**Opening a value that may be absent or may fail.** `v ?? d`, `v !! d`, `v?->f`, or `match`. `T?` and
`T!` are not `T`: arithmetic, comparison and functions that take a `T` all refuse them until they are
opened, and a `T!` that is evaluated and used by nothing is the warning
`SemanticDiag::UnusedFallibleValue`. `null` may be compared with a `T?`; `if` and `match` narrow the
value in the arm that excludes it.

```camel
func positive(x: int): int? {
    return if x > 0 then x else null
}

func describe(v: int?): string {
    return match v {
        case null => 'nothing'
        case n => `value ${n}`
    }
}

func main(): int sync {
    println(positive(4) ?? 0, describe(positive(-4)), describe(positive(7)))
    return 0
}
```

```text
4 nothing value 7
```

**`if` and `match` are expressions**, so a branch is a value and needs no temporary: `if c then a
else b`, and `match x { case p => e ... }`. A `match` over an `enum` is checked for exhaustiveness —
list every variant and no `case _` is needed; miss one and the compiler answers
`SemanticDiag::EnumMatchNotExhaustive` with the missing names (§12).

**Lambdas.** `(x: int): int => x + 1`, `() => 0`, `(x: int) => sync { ... }`. A lambda used as a
`with` argument sits inside the angle brackets with its own parentheses.

**Higher-order operators**: `xs->map<f>` and `xs->filter<p>` (elements unordered, may run in
parallel, result in position order), `xs->reduce<f, init>` and `xs->foreach<f>` (in element order).
`attempt(f)` isolates a fault at a subgraph boundary and gives back a `T!`.

### Precedence (generated)

<!-- camel-for-ai region: precedence (generated from src/parse/syntax/parser_expr.cpp; node scripts/camel-for-ai.mjs --write) -->
```text
 1  dataExpr            -                                  -
 2  ctrlExpr            if then else                       construct
 3  assignExpr          = += -= *= /= %= ^= @= &= |=       one
 4  logicalOrExpr       ||                                 repeat, left
 5  logicalAndExpr      &&                                 repeat, left
 6  orExpr              |                                  repeat, left
 7  andExpr             &                                  repeat, left
 8  equalityExpr        === !== == !=                      repeat, left
 9  relationalExpr      < > <= >=                          repeat, left
10  additiveExpr        + -                                repeat, left
11  multiplicativeExpr  * / @ %                            repeat, left
12  powerExpr           ^                                  right, one
13  nullableExpr        ?? !!                              right (a whole expression on the right)
14  unaryExpr           as                                 one
15  linkExpr            -> ?->                             repeat, left
16  negExpr             - ! ~                              prefix
17  negPower            - ! ~ ^                            prefix, right
18  compExpr            ..                                 repeat, left
19  annoExpr            -                                  repeat, left
```

The production of each level, as `src/parse/syntax/parser_expr.cpp` writes it above the
function that parses it:

```text
dataExpr           : ctrlExpr | loopExpr
ctrlExpr           : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
assignExpr         : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
logicalOrExpr      : logicalAndExpr ('||' logicalAndExpr)*
logicalAndExpr     : orExpr ('&&' orExpr)*
orExpr             : andExpr ('|' andExpr)*
andExpr            : equalityExpr ('&' equalityExpr)*
equalityExpr       : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
relationalExpr     : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
additiveExpr       : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
multiplicativeExpr : powerExpr (('*' | '/' | '@' | '%') powerExpr)*
powerExpr          : nullableExpr ('^' powerExpr)?
nullableExpr       : unaryExpr (('??' | '!!') dataExpr)?
unaryExpr          : linkExpr ('as' typeExpr)?
linkExpr           : negExpr (('->' | '?->') compExpr)*
negExpr            : ('-' | '!' | '~') negPower | compExpr
negPower           : ('-' | '!' | '~') negPower | compExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | structData | angledValues | typeArguments | memberAccess)*
```
<!-- camel-for-ai end: precedence -->

**Common errors.** `as` restricts a value, it does not convert one: `s as int` on a string is
`SemanticDiag::DynamicCastForbidden`; convert with `int(s)` (§12.5).

<!-- camel-snippet: counter-example: as does no numeric conversion -->

```camel
func n(s: string): int {
    return s as int
}
```

## 6. Statements and declarations

A declaration is: optional annotations, optional `with <...>`, optional `export`, optional modifiers,
`func` name `(parameters)`, optional `: result`, optional `sync`, optional `when <condition>`, then
the body. **`with` goes before `func`; `when` goes between the result and the body** — a `when`
written after the closing brace is `SyntaxDiag::WhenAfterBody`, the one placement the language
removed. `sync` after the result makes the body a sync block (§7); `sync` before `func` is a
modifier and orders nothing.

`with` parameters are the function's configuration; ordinary parameters are its data. Both may have
defaults and both may be given by name:

```camel
with <lo: int, hi: int>
func clamp(x: int): int {
    return if x < lo then lo else if x > hi then hi else x
}

func main(): int sync {
    println(clamp<0, 10>(42))
    println([-5, 3, 99]->map<(x: int): int => x->clamp<0, 10>>)
    return 0
}
```

```text
10
[0, 3, 10]
```

- A default is a compile-time constant expression evaluated at the declaration, so it cannot read
  another `with` parameter (`WithDefaultNotConstant`). A parameter after one with a default needs a
  default too (`WithParameterAfterDefault`), and named arguments come after positional ones
  (`f<rounds: 16, strict: true>`).
- A **function value does not carry defaults**: after `let g = repeat`, every `with` parameter of
  `g` must be given.
- The result type may be left out and is inferred from the body; only a recursive function must
  write it. A body with no `return` has the value of its last statement — and `println(x)` has the
  value `x`, so `func main() sync { println('a') }` returns `'a'`, printed twice.
- **Overloads** are several declarations of one name with different parameter types; each is a
  subgraph and the one that accepts the arguments runs. Two candidates that both accept is
  `SemanticDiag::AmbiguousCall`; two identical parameter lists is `SemanticDiag::Redeclaration`
  unless their `when` conditions cannot both hold.
- `when <condition>` is a compile-time predicate over the type parameters (`is_numeric::<T>()`,
  `is_ordered::<T>()`, or a constant such as `is_zero(y)`). A declaration whose condition is false is
  not a candidate, which is how a generic function gets one body per type. `is_numeric`, `is_ordered`
  and the other predicates are names of the `types` module, so a file that writes one imports it
  (`import { is_numeric } from types`); the condition resolves names where the declaration is written,
  so a predicate the module does not import is `SemanticDiag::WhenPredicateNotImported`.
- `let` binds once; `var` can be rebound, also through a path: `p.x = 5`, `xs[0] = 5`,
  `xs[i].f = e`. A path assignment rebinds the whole variable, as if written
  `xs = set(xs, i, set(xs[i], f, e))`; the target must be a variable, and `f().x = 5` is
  `SemanticDiag::AssignmentTargetNotVariable`. Passing a variable to a `var` parameter is how a callee
  writes back into the caller's variable (§7).
- **Generics** are declared after the name: `func first::<T>(xs: T[]): T? { ... }`, called as
  `first::<int>(xs)` or `first(xs)` when the arguments fix `T`.
- **`loop`** is the language's own iteration, and it yields its last state:
  `loop acc = 0 for i in 0..5 do acc + i` adds 0 to 4 and gives 10. The states before `for` are
  written `name = start`, comma-separated; `for <name> in <range>` and `while <condition>` may each
  be left out, and the body's value is the next state. The name after `for` is bare — never
  `for i in 0..5 { ... }`, which is `SyntaxDiag::UnexpectedToken`.
- **`static loop`** folds a compile-time sequence, and its head is written like `loop`'s, without
  parentheses: `static loop acc = 0 for i in 0..3 do acc + t.[i]`. What stands after `in` is one
  criterion, not a list of spellings: what compile-time constants build through freely schedulable
  operations is unrolled — a constant interval `a..b`, constant-argument `range(a, b, step)`, an
  array literal or constant array, `indices ts`, `fields T`, or a compile-time tuple (each round
  binds the element there). Otherwise `SemanticDiag::StaticLoopSequenceNotKnown`.
- **`rule`** declares a rewrite an optimizer pass may apply:
  `rule name(<patterns>) when <condition> { <pattern> => <replacement> }` (`<=>` for a rewrite that
  reads both ways). The condition goes between the parameters and the body, the one position a `when`
  is read in; after the body it is `SyntaxDiag::WhenAfterBody`. A rule is a node of the graph, not a
  call: declaring one runs nothing, so `camel check` accepts it and a run warns
  `SemanticDiag::EffectNotExecuted` for it.
- **Types**: `type Point = { x: int, y: int }`, `type Pair<T> = (T, T)`, and nominal enums
  `type Shape = enum { Circle(float), Rect { w: float, h: float }, Empty }`. A variant is built with
  `Shape::Circle(1.0)` / `Shape::Rect { w: 2.0, h: 3.0 }` / `Shape::Empty`, holds its data privately
  (`s.w` is `EnumFieldAccess`), and is read with `match`. Structural types are written literally in a
  type position: `{ x: int, y: int }`, `(int, string)`, `int[]`, `int[4]`, `f32[2, 3]`.
- An `enum` over a type parameter needs an inferred argument: `let x: Option<int> = Option::None`
  works, `let x = Option::None` does not (`EnumTypeArgumentNotInferred`).
- Operator overloading is a declaration whose name is an operator: `func operator +(a: V, b: V): V`.
  Annotations before a declaration are `#` names (`#macro`, `#impl`) or `@` names; a `#macro`
  function may be evaluated early and its result folded into the graph as a constant.

```camel
type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}

rule add_zero(x: int) {
    x + 0 => x
}

func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
        case Shape::Empty => 0.0
    }
}

func first::<T>(xs: T[]): T? {
    return if len(xs) == 0 then null else xs[0]
}

func sum3(t: (int, int, int)): int {
    return static loop acc = 0 for i in 0..3 do acc + t.[i]
}

func main(): int sync {
    println(area(Shape::Rect { w: 2.0, h: 3.0 }), first([7, 8]) ?? 0, first::<string>([]) ?? 'none')
    println(loop acc = 0 for i in 0..5 do acc + i, loop n = 3 while n > 0 do n - 1, sum3((1, 2, 3)))
    return 0
}
```

**Common errors.** A declaration must agree with its initializer: `let n: int = '42'` is
`SemanticDiag::DeclaredTypeMismatch` (§12.5).

<!-- camel-snippet: counter-example: a declared type and its initializer must agree -->

```camel
func main(): int sync {
    let n: int = '42'
    return n
}
```

## 7. Effects and `sync`

A **sync block** orders the statements written directly in it, and only those: `func f(): T sync
{ ... }`, `if c then sync { ... }`, `(x: int) => sync { ... }`, or a bare `sync { ... }`. Inside it,
calls run in the written order whatever they return. Outside it, an effect whose result nothing uses
is dropped with the warning `SemanticDiag::EffectNotExecuted` — legal, and the reason a program can
look busy and print nothing. The rule does not propagate inward: an arm or loop body written inside
a sync block is an ordinary subgraph, and needs its own `sync`.

```camel
func f(c: bool): int sync {
    if c then { println(1); println(2) }   // the arm is not a sync block: only println(2) runs
    return 0
}
```

- **`var` parameters and write order.** A `var` parameter is a mutable reference to the caller's variable,
  so the callee's write is a write the caller sees. Writes to such a variable are ordered against the
  operations that read or write it, so a write the callee makes is the value the caller reads after the
  call returns (design principles 4.3).
- **Errors and faults.** A fault (index out of range, division by zero, a value that does not fit a
  conversion, recursion past the limit) stops the program with a located diagnostic and exit code 1;
  `!!` and `sync` do not catch it, and a `T!` returned from `main` and never handled is printed to
  standard error as `RuntimeDiag::UnhandledError`. An error value is caught: `!!` gives the fallback,
  `?->` passes it on, `match` with `case error e` reads `e.kind`, `e.message` and `e.location`.
- **Recursion.** A call in tail position does not use a stack frame; other recursion is limited (1000
  open calls by default, `--max-recursion-depth` or `CAMEL_MAX_RECURSION_DEPTH`), and passing it is
  the fault `RuntimeDiag::MaxRecursionDepthExceeded`.
- `exit(n)` ends the program immediately with exit code `n`, after the output written so far.

```camel
func main(): int sync {
    let xs = [1, 2, 3]
    println(xs[5])
    return 0
}
```

```text
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
```

**Common errors.** A fault is not an error value: `!!` does not catch it, and `xs[10]` stops the
program with `RuntimeDiag::IndexOutOfRange` — look at the length first (§12.18). A function that
writes must be `sync`, or its call is `SemanticDiag::EffectNotExecuted` (§12.1).

## 8. Streams

`T..` (a stream of `T`), `yield`, and `tee` are how a graph consumes data that does not fit in
memory: the push/pull boundary is written as a function the stream calls, and the design is
`docs/design/io-concurrency-streams/` (design principles 4.2 names the ruling). A pipeline can also
be expressed with `1..n` and the `map`/`filter`/`reduce` operators, and a file read with
`fs::read_text` or the `io` module. A function whose body writes `yield` produces a stream, and its
return type is written as a stream type in the head:

```camel
func pairs(n: int): (int, int).. {
    var i = 0
    loop while i < n do
        yield (i, i + 1)
}
```

Sources: `docs/design-principles.cn.md` §4.2; `docs/design/io-concurrency-streams/`.

**Common errors.** A spelling that only looks like a stream is refused instead of run: with the
function it names declared, `xs..map(inc)` is `SemanticDiag::NoMatchingFunction` — `..` composes two
functions and nothing else. The fix is an array with `map` and a filter (§12.14).

<!-- camel-snippet: counter-example: a stream spelling is written wrong here: `..` composes functions -->

```camel
func inc(x: int): int {
    return x + 1
}

func doubled(xs: int[]): int[] {
    return xs..map(inc)
}
```

## 9. Modules and the standard library

Every source file is a module. `module <name>` at the top names it; without one the name comes from
the file name. A name is visible only inside its module until it is exported, and `export` goes
before `func`, `type` or `let`, or is written once as `export { a, b }`.

```camel
// shapes.cml
module shapes

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

```camel
// main.cml, in the same directory as shapes.cml
import { hypot, Vec } from shapes
import shapes

func main(): int sync {
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(shapes::area(2.0, 5.0))
    return 0
}
```

```text
5.0
10.0
```

- The import forms are `import { a, b as c } from m`, `import a from m`, `import m` (qualified
  access only) and `import m as n`. A module may import a module that imports it back; only the
  top-level values must not form a cycle. A module that cannot be found is
  `SemanticDiag::ModuleNotFound` at that `import`, and importing a module that has errors is
  `SemanticDiag::ImportedModuleHasErrors`.
- An import names a **module**, never a path: `import ../lib` is a syntax error. The search order is
  the module's own directory, then `-I` directories, `CAMEL_PACKAGES`, `CAMEL_MODULE_PATH`, and
  finally the standard library shipped with Camel.
- **Namespaces.** `namespace ns { ... }` groups declarations; inside a module a name may also be
  reached qualified (`math::sqrt`) or pulled in with `using namespace math`. `using` imports names,
  not modules.
- **The standard library.** These names need no import: `print`, `println`, `input`, `format`,
  `join`, `len`, `range`, `zip`, `head`, `tail`, `last`, `slice`, `concat`, `append`, `extend`,
  `contains`, `map`, `filter`, `reduce`, `foreach`, `exit`, `sleep`, `error`, `parse_int`,
  `parse_float`, the conversions (`int`, `float`, `string`, ...), and the map and set operations.
  Everything else is in a module: `str` (bytes and code points, trim, case, split, find), `array`
  (`sort`, `take`, `drop`, `unique`, `sum`, `min`, `max`, `flat_map`, ...), `math` (`pi`, `e`,
  `sqrt`, `clamp`, `gcd`, bit operations), `random`, `time`, `fs`, `json`, `process`, `env`, `re`,
  `hash`, `statistics`, `para`, `tensor`, `nn`, `autodiff`, `onnx`, `python`.
  [Appendix B](camel-for-ai-stdlib-signatures.md) lists every exported name with its signature;
  `camel inspect --ops` lists the operators the compiler knows, with their purity.

Sources: `docs/language/05_modules.md`, `docs/stdlib-reference.md`.

**Common errors.** An exported name is not in scope until its module is imported: `area` without
`import` is `SemanticDiag::UnresolvedReference` (§12.15; `docs/stdlib-reference.md` names the module
a name lives in).

<!-- camel-snippet: counter-example: an exported name needs its module -->

```camel
func main(): int sync {
    println(area(Shape::Circle(1.0)))
    return 0
}
```

## 10. Passes, the command line, the graph

`camel prog.cml` runs the program with the default pipeline. Words after the file name are **passes**,
run in the order given; a pass parameter is written `key=value` after the pass name
(`std::jit hot_threshold=0`).

| Pass | What it does |
|---|---|
| `std::nvm` | The fallback: interprets the graph on the node VM |
| `std::fvm` | Bytecode VM |
| `std::jit` | Bytecode VM with just-in-time compilation, falling back to the interpreter |
| `std::prl` | Parallel scheduling: independent nodes run concurrently |
| `std::opt`, `std::opt::inline`, `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::opt::saturate` | Optimizations; they return a graph |
| `std::macro` | Evaluates `#macro` calls early and materializes them as constants |
| `std::gir`, `std::rgir`, `std::agir`, `std::jgir` | Print the graph and run nothing |
| `std::stats` | Print `GRAPH_STATS {json}` and pass the graph on |
| `onnx::export`, `std::cpp` | Translate instead of run |

- The language does not fix the pass order, and the order can change the result: passes modify the
  graph. A pass that consumes the graph (the schedulers and the translators) ends the pipeline;
  passes written after it do not run, with the warning `RuntimeDiag::PassesNeverRun`.
- The compiler builds the graph from `var`/`let` and `sync`; after that those rules no
  longer constrain the passes.
- `std::gir` is the form to read a program's order from — the nodes and the edges between them are
  the program — and `std::rgir` is the same graph for Graphviz.

```camel
// order.cml
// camel order.cml std::gir
func main(): int sync {
    let a = 1 + 2
    let b = a * 3
    println(a, b)
    return 0
}
```

<!-- camel-snippet: graph-dump: std::gir prints a graph, not the program's output -->

- Useful options: `--arg name=value` (an input of `main`), `-o <file>` (write the result), `-I <dir>`
  (a module search directory), `--max-recursion-depth <n>`, `-Wno-<warning>` and `-Werror`
  (diagnostics), `--output-format json`, `--explain <diagnostic name or code>` (the compiler
  explains one diagnostic). `camel check prog.cml` reports code-health problems without running it.
- An expression statement whose value no one needs is a **warning**, not a silent no-op
  (`SemanticDiag::EffectNotExecuted`, `SemanticDiag::UnusedPureResult`,
  `SemanticDiag::UnusedFallibleValue`).

Sources: `docs/language/04_execution_model.md` §4.7, `docs/cli.cn.md`.

**Common errors.** A pass list is not a run. `camel f.cml std::gir` prints the graph and stops, so a
reader who wanted output sees none; pass a scheduling pass (`std::nvm`) when output is what is
wanted.

## 11. Idioms

Written by someone who knows Camel, the same program looks like this.

- **Data flows through a pipeline; context sits in the angle brackets.** `xs->map<f>` and
  `x->clamp<0, 10>` read as sentences. Write one such call per statement.
- **A function is worth extracting when it is configuration-shaped**: `with` parameters are what a
  model's weights, a tensor's shape or a flag becomes, and the result is a function value that can be
  passed on (`let predict = net<weights>`).
- **Ranges and composition instead of index loops.** `1..n->map<...>`, `1..n->filter<...>->reduce<...>`
  for a fused accumulation, and `f..g` when a pipeline of functions is the value being passed. When a
  loop is really a loop, write the language's own form and let it carry the state:
  `loop acc = 0 for i in 0..n do acc + i` (§6).
- **Weights are `with` parameters, and `grad` reads the first of them.** With `with <x: float>` in
  front of `func f(y: float): float`, `grad(f)<3.0>(2.0)` is the derivative at `x = 3.0, y = 2.0`;
  a function with no `with` parameter is `SemanticDiag::NotDifferentiable`, and a function-typed
  parameter is not a weight.
- **`T?` and `T!` are the boundary types.** Let a function that can miss return `T?` and one that can
  fail return `T!`; open them where the decision is made (`??`, `!!`, `match`), not by inventing
  sentinel values like `-1` or `''`.
- **`match` over an `enum`** for values that are one of several things, instead of flags and `if`
  chains: the compiler then checks that every case is handled.
- **Prefer higher-order operators to hand-written loops**, and `reduce` for accumulation:
  `words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc->get<w> ?? 0) + 1>, [] as Map<string, int>>`.
- **`let` by default, `var` only for what is reassigned**, and in-place update left to the compiler
  (`xs = xs->sort`), never simulated with an index.
- **Effects in `sync` blocks**, one block per thing that must happen in order, because that is the
  only thing that orders them.

```camel
// shapes.cml
module shapes

import { sqrt } from math

export type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
}

export func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
    }
}

with <shapes: Shape[]>
func areas(): float[] {
    return shapes->map<area>
}

export func total_area(kinds: Shape[], scale: float): float {
    return kinds
        ->map<(s: Shape): float => scale * scale * area(s)>
        ->reduce<(a: float, b: float): float => a + b, 0.0>
}
```

Sources: `docs/language/07_statements.md`, `docs/language/06_expressions.md`,
`docs/design-principles.cn.md` §11.

A model is a configured function, and its weight is a `with` parameter:

```camel
import { grad } from autodiff

with <x: float>
func f(y: float): float {
    return x * x * y + y
}

func main(): int sync {
    println(f<3.0>(2.0), grad(f)<3.0>(2.0))
    return 0
}
```

```text
20.0 12.0
```

**Common errors.** An idiom's types must line up: mixing the two spellings of the 32-bit integer is
`SemanticDiag::NoMatchingFunction` at the call; use one spelling throughout (§5, §12.4).

<!-- camel-snippet: counter-example: one type has one spelling in a signature -->

```camel
func add(a: i32, b: int): int {
    return a + b
}
```

## 12. Common errors

Each entry is *what a first draft writes* — *what the compiler answers* — *what to write instead*.
Every counter-example below is compiled by `node scripts/camel-for-ai.mjs --verify-errors`, which
checks that the diagnostic named here is the one the compiler reports.

**1. An effect outside a `sync` block.** The program compiles, prints nothing from that line, and the
compiler answers `SemanticDiag::EffectNotExecuted` (a warning). **Wrong:**

<!-- camel-snippet: counter-example: the effect is dropped because nothing needs its result -->

```camel
func greet(): int {
    println('hello')
    return 1
}
```

**Write instead** — order it, either in a sync body or by using the result:

```camel
func greet(): int sync {
    println('hello')
    return 1
}
```

**2. `Tensor` with no import.** `Tensor` is the `tensor` module's name for the numeric array whose
element type is not fixed, so it is imported; without that the name is unknown and the compiler
answers `SemanticDiag::UnresolvedTypeReference`. **Wrong:**

<!-- camel-snippet: counter-example: Tensor is a name the tensor module exports and has to be imported -->

```camel
func f(x: Tensor): int {
    return 0
}
```

**Write instead** — import the name and build values with the module's functions: `Tensor<f32, [2, 3]>`
and `f32[2, 3]` write one type, so the tensor operators take either spelling. A bare `Tensor`
parameter is on its way out (**target**, `docs/design-principles.cn.md` §5.4).

```camel
import tensor
import { Tensor } from tensor

func twice(x: f32[..]): float {
    return tensor::sum(tensor::multiply(x, 2.0))
}

func main(): int sync {
    println(twice(tensor::new([1.0, 2.0] as f32[])))
    return 0
}
```

```text
6.0
```

**3. `any`, or a value whose type is not known.** There is no `any`; the compiler answers
`SemanticDiag::UnresolvedTypeReference`. **Wrong:**

<!-- camel-snippet: counter-example: there is no any; a parameter that accepts several types is generic -->

```camel
func show(x: any): string {
    return string(x)
}
```

**Write instead** — a type parameter, with a `when` condition when the body needs something of it:

```camel
func show::<T>(x: T): string {
    return string(x)
}
```

**4. Two numeric types in one expression, or a variable assigned to another numeric type.** Numeric
types do not convert implicitly: `i32` with `int`, or `int` with `float`, is
`SemanticDiag::NoMatchingFunction` in an expression and `SemanticDiag::DeclaredTypeMismatch` in a
typed `let`. **Wrong:**

<!-- camel-snippet: counter-example: i32 and int are different types and do not mix -->

```camel
func add(a: i32, b: int): int {
    return a + b
}
```

**Write instead** — convert explicitly:

```camel
func add(a: i32, b: int): int {
    return int(a) + b
}
```

**5. Text used as a number.** `'42'` is a string: `let x: int = '42'` is
`SemanticDiag::DeclaredTypeMismatch`. `'42' as int` is the other error: `as` is no numeric
conversion, so the compiler answers `SemanticDiag::DynamicCastForbidden`.
**Wrong:**

<!-- camel-snippet: counter-example: as does no numeric conversion; text is not a number -->

```camel
func n(s: string): int {
    return s as int
}
```

**Write instead** — `int(s)` (or `parse_int(s)` when the text may be junk and a `T!` is wanted):

```camel
func n(s: string): int {
    return int(s)
}
```

**6. An empty array with no type.** `[]` takes its element type from its position, and a `let` with
no type is not one: `SemanticDiag::EmptyArrayWithoutType`. **Wrong:**

<!-- camel-snippet: counter-example: the element type of an empty literal must come from somewhere -->

```camel
func main(): int sync {
    let xs = []
    return len(xs)
}
```

**Write instead:**

```camel
func main(): int sync {
    let xs: int[] = []
    return len(xs)
}
```

**7. A `when` after the body.** The condition belongs between the result type and the body; after a
function body the compiler answers `SyntaxDiag::UnexpectedToken`. **Wrong:**

<!-- camel-snippet: counter-example: when goes before the body of a function, not after it -->

```camel
func pick::<T>(x: T): T {
    return x
} when is_ordered::<T>()
```

A trailing `when` on a `rule` is the same mistake, and answers `SyntaxDiag::WhenAfterBody`: the
condition comes before the body it guards.

<!-- camel-snippet: counter-example: a rule's condition follows its parameters, not its body -->

```camel
rule add_zero(x: int) {
    x + 0 => x
} when is_zero(0)
```

**Write instead:**

```camel
import { is_ordered } from types

func pick::<T>(x: T): T when is_ordered::<T>() {
    return x
}
```

**8. A keyword as a name.** Keywords are reserved, so `let match = 1` is
`SyntaxDiag::KeywordAsIdentifier`. **Wrong:**

<!-- camel-snippet: counter-example: match is a keyword -->

```camel
func main(): int sync {
    let match = 1
    return match
}
```

**Write instead:**

```camel
func main(): int sync {
    let matched = 1
    return matched
}
```

**9. A `match` that misses a variant.** A `match` over an `enum` must list every variant unless a
`case _` is written; the compiler answers `SemanticDiag::EnumMatchNotExhaustive` and names the
missing ones. **Wrong:**

<!-- camel-snippet: counter-example: the Empty variant is not handled -->

```camel
type Shape = enum { Circle(float), Empty }

func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
    }
}
```

**Write instead** — add the missing arm (or `case _ => 0.0`).

**10. Reading an `enum`'s data directly.** A variant's data is not a field of the enum value:
reading it is `SemanticDiag::EnumFieldAccess`. **Wrong:**

<!-- camel-snippet: counter-example: an enum value's data is read with match, not with a member -->

```camel
type Shape = enum { Circle(float), Empty }

func radius(s: Shape): float {
    return s.r
}
```

A pattern names the variant with its type: a bare `case Empty` is
`SemanticDiag::EnumPatternMismatch`. **Wrong:**

<!-- camel-snippet: counter-example: a variant pattern is qualified with the type it belongs to -->

```camel
type Shape = enum { Circle(float), Empty }

func radius(s: Shape): float {
    return match s {
        case Circle(r) => r
        case Empty => 0.0
    }
}
```

**Write instead** — qualify the variants and read the data in the pattern:

```camel
type Shape = enum { Circle(float), Empty }

func radius(s: Shape): float {
    return match s {
        case Shape::Circle(r) => r
        case Shape::Empty => 0.0
    }
}
```

**11. An enum value whose type arguments cannot be inferred.** `let x = Option::None` has no field to
infer from: `SemanticDiag::EnumTypeArgumentNotInferred`. **Wrong:**

<!-- camel-snippet: counter-example: None carries no field, so the type argument must be written -->

```camel
type Option<T> = enum { Some(T), None }

func main(): int sync {
    let x = Option::None
    return 0
}
```

**Write instead:**

```camel
type Option<T> = enum { Some(T), None }

func main(): int sync {
    let x: Option<int> = Option::None
    return 0
}
```

**12. Assigning through a path that is not a variable.** `f().x = 5` has no variable to rebind:
`SemanticDiag::AssignmentTargetNotVariable`. **Wrong:**

<!-- camel-snippet: counter-example: the left side of an assignment must be a variable or a path into one -->

```camel
type Point = { x: int, y: int }

func origin(): Point {
    return { x: 0, y: 0 }
}

func main(): int sync {
    origin().x = 5
    return 0
}
```

**Write instead** — bind it, then assign through the variable:

```camel
type Point = { x: int, y: int }

func origin(): Point {
    return { x: 0, y: 0 }
}

func main(): int sync {
    var p = origin()
    p.x = 5
    println(p)
    return 0
}
```

**13. A `var` parameter given a `let`.** A `var` parameter needs a variable that can be written back,
so a `let` is refused with `SemanticDiag::ImmutablePassedToVarParam`. **Wrong:**

<!-- camel-snippet: counter-example: a var parameter needs a variable that can be written -->

```camel
func bump(var x: int): int {
    x = x + 1
    return x
}

func main(): int sync {
    let n = 3
    println(bump(n))
    return 0
}
```

A `var` parameter is a resource parameter (design principles 3.3): it needs no declaration of its
own.

**Write instead:**

```camel
func bump(var x: int): int {
    x = x + 1
    return x
}

func main(): int sync {
    var n = 1 + 2
    println(bump(n), n)
    return 0
}
```

**14. Using a `T?` or `T!` as its inner type.** They are different types: a `T!` whose value
nothing uses is the warning `SemanticDiag::UnusedFallibleValue`, and arithmetic on a `T?` — as
below — has no reading at all: `SemanticDiag::NoMatchingFunction`. **Wrong:**

<!-- camel-snippet: counter-example: a value that may be absent must be opened before it is used -->

```camel
func positive(x: int): int? {
    return if x > 0 then x else null
}

func main(): int sync {
    println(positive(4) + 1)
    return 0
}
```

**Write instead** — open it where the decision belongs:

```camel
func positive(x: int): int? {
    return if x > 0 then x else null
}

func main(): int sync {
    println((positive(4) ?? 0) + 1)
    return 0
}
```

**15. A name nothing declares.** `SemanticDiag::UnresolvedReference` names it and lists the
candidates that came close. **Wrong:**

<!-- camel-snippet: counter-example: nothing declares total -->

```camel
func main(): int sync {
    return total
}
```

**Write instead:**

```camel
func main(): int sync {
    let total = 0
    return total
}
```

**16. A reserved compiler name.** A name with two leading and two trailing underscores is the
compiler's; using one is `SemanticDiag::ReservedIdentifier`. **Wrong:**

<!-- camel-snippet: counter-example: __add__ belongs to the compiler -->

```camel
func main(): int sync {
    let __add__ = 1
    return __add__
}
```

**Write instead:**

```camel
func main(): int sync {
    let added = 1
    return added
}
```

**17. `try`/`catch`.** They are not Camel, and neither is a keyword: `try`, `catch` and `finally` are
ordinary names, so the spelling below is an ordinary syntax error
(`SyntaxDiag::UnexpectedToken`). **Wrong:**

<!-- camel-snippet: counter-example: there is no try, catch or finally in Camel -->

```camel
func main(): int sync {
    try {
        println(1)
    } catch e: string {
        println(e)
    } finally {
        println(3)
    }
    return 0
}
```

**Write instead** — a function that can fail returns `T!`, and the caller opens it:

```camel
func read_number(s: string): int! {
    return parse_int(s)
}

func main(): int sync {
    println(read_number('7') !! -1, read_number('seven') !! -1)
    return 0
}
```

**18. A fault.** `println(xs[5])` on a three-element array answers `RuntimeDiag::IndexOutOfRange`: a
fault is not an error value, and `!!` does not catch it (§7). **Write instead** — check the index, or
take a slice, which tolerates the end (`xs[3:9]`).

**Common errors.** A reserved spelling reads like a construct the language has, and the compiler says
otherwise: `typeof` is refused where it is used (`SyntaxDiag::UnexpectedToken`); the fix is the
spelling the type system offers (§5).

<!-- camel-snippet: counter-example: a reserved spelling is read and then refused -->

```camel
func kind(x: int): int {
    return typeof(x)
}
```

## Appendix A. Diagnostics

The catalogue of every diagnostic the compiler, the passes and the run time can report — the
exact message template, the section-qualified name to match it against, and which block is an
error and which is a warning — is
[`camel-for-ai-diagnostics.md`](camel-for-ai-diagnostics.md). It is a document of its own
because it grows with the registry, and this page has to stay one read.

## Appendix B. Standard library signatures

The catalogue of every name the standard library exports, with its signature, is
[`camel-for-ai-stdlib-signatures.md`](camel-for-ai-stdlib-signatures.md). It is a document of its
own for the same reason.

## Appendix C. Sources

| Section | Normative source |
|---|---|
| 2. The shape of a program | `docs/design-principles.cn.md` §§2–4, 11.3; `docs/language/04_execution_model.md` |
| 3. The smallest program | `docs/language/04_execution_model.md` §4.1; `docs/cli.cn.md` |
| 4. Lexical | `docs/language/02_lexical.md`; `include/camel/parse/syntax/token_kinds.def` |
| 5. Expressions and precedence | `docs/language/06_expressions.md`; `docs/design-principles.cn.md` §§4.2, 5.1, 5.4 |
| 6. Statements and declarations | `docs/language/07_statements.md`; `docs/language/03_data_model.md` |
| 7. Effects and `sync` | `docs/language/04_execution_model.md` §§4.2–4.6; `docs/design-principles.cn.md` §3.4 |
| 8. Streams | `docs/design-principles.cn.md` §4.2; `docs/design/io-concurrency-streams/` |
| 9. Modules and the standard library | `docs/language/05_modules.md`; `docs/stdlib-reference.md` |
| 10. Passes and the command line | `docs/language/04_execution_model.md` §4.7; `docs/cli.cn.md` |
| 11. Idioms | `docs/language/07_statements.md`, `docs/language/06_expressions.md`, `docs/design-principles.cn.md` §11 |
| 12. Common errors | the registry `diagnostics/registry.tsv`, and the compiler through `node scripts/camel-for-ai.mjs --verify-errors` |
| Appendix A | `docs/reference/camel-for-ai-diagnostics.md`, generated from `diagnostics/registry.tsv` |
| Appendix B | `docs/reference/camel-for-ai-stdlib-signatures.md`, generated from `docs/stdlib-reference.md` |

Generated by `scripts/camel-for-ai.mjs`; the two generated regions of this page are rewritten with
`node scripts/camel-for-ai.mjs --write` and checked with `--check`, which is also what the plan
`test/plans/feat/docs_ai_reference.plan.toml` runs. The same command rewrites the two appendices,
each of which is checked against its own budget.