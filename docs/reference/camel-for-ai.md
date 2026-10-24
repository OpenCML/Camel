# Camel for AI

One file that says everything an AI needs to write Camel that compiles: the rules, the idioms, the
errors a first draft makes, and the exact name of every diagnostic it will meet. It is written to be
read whole — the whole file is under 112,000 characters, about 31,000 tokens, and the check below
keeps it there.

The language described here is the development version (`develop`), and it is tied to that tree:
`scripts/camel-for-ai.mjs --check` regenerates the four generated regions from their sources, checks
every diagnostic this file names against the registry, and fails when the file and the tree
disagree. Read it as the language you are compiling against, not as a snapshot of one.

## How to read this file

- **Answers first.** Sections 2–3 are what you need before writing a line. Sections 4–10 are the
  rules of each part of the language. Section 11 is how Camel is written when someone who knows it
  writes it. Section 12 is the most valuable one when a compile fails: it is organised as *what you
  wrote — what the compiler says — what to write instead*. Section 13 lists spellings the compiler
  accepts and then rejects, so a plausible-looking guess can be recognised before it is tried.
- **`target`.** A rule that is decided but not implemented yet is marked `**target**` and names the
  design it comes from under `docs/design/` (for example `docs/design/io-concurrency-streams/`).
  Code written against a `target` rule does not compile today. Never treat a `target` rule as
  available.
- **Diagnostics are named in full**: `SemanticDiag::UnresolvedReference`, never bare
  `UnresolvedReference`. Appendix A lists all of them with the exact message template; the compiler
  prints the section-qualified name, so the two can be matched mechanically.
- **Fenced blocks tagged `camel` compile.** Every one of them is compiled by the documentation check
  when the test suite runs, so any block on this page is valid Camel as of this tree. A block that
  is deliberately incomplete is marked `camel-snippet: fragment`, and one that is deliberately wrong
  is marked `camel-snippet: counter-example` and says what the compiler answers.
- **Where a rule comes from.** Hand-written sections name their source in the text — a section of
  `docs/design-principles.cn.md` (normative: when it disagrees with anything, it wins) or a page of
  `docs/language/`. Appendix C collects the map.

## What each part costs

Budgets, in characters: the file as a whole (112,000), the hand-written half (55,400) and each
generated region (61,000 together). `node scripts/camel-for-ai.mjs --report` prints the current
numbers, and `--check` fails when one of them is exceeded — including the per-chapter budget in the
last column below, which is read out of this table and measured against the chapter it names.

| Part | Answers | Budget |
|---|---|---|
| 2. The shape of a Camel program | what a program is: values, the graph, effects, passes | 3,000 |
| 3. The smallest program that compiles | entry point, module, output, exit | 2,000 |
| 4. Lexical: names, comments, literals | what the lexer accepts, keywords, operators | 3,200 |
| 5. Expressions, operators, precedence | how an expression is read | 8,200 |
| 6. Statements and declarations | functions, parameters, loops, rules, generics, `when` | 6,850 |
| 7. Effects and `sync` | what runs in order, and what the compiler folds | 2,700 |
| 8. Streams (`target`) | `T..`, `yield`, `tee` | 1,500 |
| 9. Modules and the standard library | imports, exports, namespaces, where the library is | 3,300 |
| 10. Passes, the command line, the graph | how to run a program and look at its graph | 3,000 |
| 11. Idioms | how Camel code is written when it is written by someone who knows | 3,600 |
| 12. Common errors | the error, the diagnostic, the fix | 11,800 |
| 13. Accepted and then rejected | reserved spellings and their diagnostics | 2,600 |
| 14. Not implemented yet (`target`) | what to avoid until its design lands | 1,700 |
| Appendix A. Diagnostics | name, severity, message — generated | 28,000 |
| Appendix B. Standard library signatures | every exported name — generated | 28,000 |
| Appendix C. Sources | which normative page each rule comes from | 1,800 |
| — | generated, all four regions | 61,000 |
| — | hand-written, this header and sections 2–14 with Appendix C | 55,400 |

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

Errors and faults are different things. A function whose failure the program is expected to handle
returns `T!`, and the caller opens it with `!!`, `?->` or `match`. A **fault** — an index out of
range, division by zero, recursion past the limit — is not a value: it stops the program with a
located diagnostic, and `!!` does not catch it (§7).

What runs the graph is a **pass**: `camel prog.cml` uses the default pipeline, `camel prog.cml
std::fvm` chooses the bytecode VM, and `std::gir` prints the graph instead of running it (§10).

Sources: `docs/design-principles.cn.md` §§2–4, 11.3; `docs/language/04_execution_model.md`.

**Common errors.** Executed statements belong in a function. A statement at the top level is
where the grammar stops reading, and the compiler reports `SyntaxDiag::UnexpectedToken` at the word
that did not fit. Move it into a function (§3).

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

**Common errors.** Printing is an effect, and in a function that is not `sync` nothing uses it:
the compiler answers the warning `SemanticDiag::EffectNotExecuted` and the line does not run. Mark the
function `sync` when its printing has to happen (§7, §12.1).

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
  `0.5-0.25i`), which are complex values. A numeric type is never converted to another implicitly
  (§5).
- **Text.** `'...'` and `"..."` are the same string type; `'''...'''` and `"""..."""` span lines. A
  backtick string interpolates: <code>`Hello, ${name}!`</code> is the formatted string of
  `'Hello, ${0}!'` and `name`. A string is UTF-8 and immutable, and its length and subscripts count
  code points.
- **Keywords are reserved**: using one as a name is `SyntaxDiag::KeywordAsIdentifier`. The table is
  generated from the token table the lexer reads, grouped as that file groups the words. Its
  `Reserved` group is the set of words the grammar accepts and the language does not implement yet
  (§13); whether a word in another group is usable is what the rest of this file says.

### Keywords and operators (generated)

<!-- camel-for-ai region: lexical (generated from docs/language/02_lexical.md §2.4 and include/camel/parse/syntax/token_kinds.def; node scripts/camel-for-ai.mjs --write) -->
| Group | Words |
|---|---|
| Declaration | `let` `var` `func` `type` `with` `module` `import` `export` `from` `namespace` `using` |
| Control | `if` `then` `else` `match` `case` `return` `throw` `sync` `loop` `while` `do` `for` `in` `static` |
| Type and conversion | `as` `const` `constas` `enum` |
| Literal | `null` `true` `false` |
| Reserved (accepted, not implemented — §13) | `is` `of` `try` `catch` `finally` `typeof` `keyof` |

**Operators and separators** (55): `;` `,` `:` `::` `.` `..` `...` `(` `)` `[` `]` `{` `}` `<` `<` `>` `<=` `<=>` `>=` `==` `!=` `===` `!==` `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `|=` `+` `-` `*` `/` `%` `^` `@` `#` `&` `|` `&&` `||` `!` `!!` `~` `?` `??` `->` `?->` `=>` `\\` `_`

The token table `include/camel/parse/syntax/token_kinds.def` groups `enum` (token table: `Reserved`; reference: Type and conversion). This file follows the reference.
<!-- camel-for-ai end: lexical -->

**Common errors.** A keyword cannot be a name: a binding called `if` is
`SyntaxDiag::KeywordAsIdentifier`, and the fix is a name outside the reserved spellings (§13, §12.9).

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
equal contents are equal. Numeric types never convert implicitly: `i32` with `int`, or `int` with
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
14  unaryExpr           as is                              one
15  linkExpr            -> ?->                             repeat, left
16  negExpr             - ! ~                              prefix
17  negPower            - ! ~ ^                            prefix, right
18  compExpr            ..                                 repeat, left
19  annoExpr            !                                  repeat, left
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
unaryExpr          : linkExpr (('as' | 'is') typeExpr)?
linkExpr           : negExpr (('->' | '?->') compExpr)*
negExpr            : ('-' | '!' | '~') negPower | compExpr
negPower           : ('-' | '!' | '~') negPower | compExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | structData | angledValues | typeArguments | '!' | memberAccess)*
```
<!-- camel-for-ai end: precedence -->

**Common errors.** `as` restricts a value, it does not convert one: `s as int` on a string is
`SemanticDiag::DynamicCastForbidden`, and the fix is the conversion function `int(s)` (§12.5).

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
  value `x`, so `func main() sync { println('a') }` returns `'a'` and the run prints it twice: once
  as the statement, once as the result.
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
  `SemanticDiag::AssignmentTargetNotVariable`. Passing a value to a `var` parameter of a function is
  how a callee can write back into the caller's variable (`wait` orders that write, §7).
- **Generics** are declared after the name: `func first::<T>(xs: T[]): T? { ... }`, called as
  `first::<int>(xs)` or `first(xs)` when the arguments fix `T`.
- **`loop`** is the language's own iteration, and it yields its last state:
  `loop acc = 0 for i in 0..5 do acc + i` adds 0 to 4 and gives 10. The states before `for` are
  written `name = start`, comma-separated; `for <name> in <range>` and `while <condition>` may each
  be left out, and the body's value is the next state. The name after `for` is bare — never
  `for i in 0..5 { ... }`, which is `SyntaxDiag::UnexpectedToken`: a `for` on its own is not a
  statement.
- **`static loop`** folds a compile-time sequence, and its head is written like `loop`'s, without
  parentheses: `static loop acc = 0 for i in 0..3 do acc + t.[i]`. After `in` comes `indices ts`,
  `fields T`, or a constant interval `a..b`, half-open as `..` is elsewhere; `i` is a compile-time
  value and a `t.[i]` key at once. A sequence it cannot work out is
  `SemanticDiag::StaticLoopSequenceNotKnown`.
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

**Common errors.** A declaration must agree with what it is given: `let n: int = '42'` is
`SemanticDiag::DeclaredTypeMismatch`, and the fix is to convert at the binding (§12.5).

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
a sync block is an ordinary subgraph, and writing `sync` there again is what orders it.

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

**Common errors.** A fault is not an error value. `!!` opens a `T!`; it does not catch a fault,
and `xs[10]` past the end of an array stops the program with `RuntimeDiag::IndexOutOfRange` and exit
1 — the fix is to look at the length first. A function that writes must be `sync`, or its call is
`SemanticDiag::EffectNotExecuted` (§7, §12.1).

## 8. Streams (`target`)

`T..` (a stream of `T`), `yield`, and `tee` are decided and **not implemented**: writing them does
not compile today, and the design is `docs/design/io-concurrency-streams/` (design principles 4.2 names
the ruling). A stream will be the way a graph consumes data that does not fit in memory, with the
push/pull boundary written as a function the stream calls; until it lands, express a pipeline with
`1..n` and the `map`/`filter`/`reduce` operators, and read a file with `fs::read_text` or the
`io` module. What is **not** `target` and is worth checking before assuming otherwise: `a..b` on
integers, and `f..g` composition, are implemented today.

Sources: `docs/design-principles.cn.md` §4.2; `docs/design/io-concurrency-streams/`.

**Common errors.** Streams are not in the language yet, so a stream spelling is refused instead of
run: with the function it names declared, `xs..map(inc)` is `SemanticDiag::NoMatchingFunction` —
`..` composes two functions and nothing else, and there is no stream `map` to find. The fix today is
an array with `map` and a filter (§12.14); the design draft is
`docs/design/io-concurrency-streams/`.

<!-- camel-snippet: counter-example: a stream spelling is not in the language yet -->

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
  `hash`, `statistics`, `para`, `tensor`, `nn`, `autodiff`, `onnx`, `python`. Appendix B lists every
  exported name with its signature; `camel inspect --ops` lists the operators the compiler knows,
  with their purity.

Sources: `docs/language/05_modules.md`, `docs/stdlib-reference.md`.

**Common errors.** An exported name is not in scope until its module is imported: using `area`
without `import` is `SemanticDiag::UnresolvedReference`, and the fix is the import above the use
(§12.15; `docs/stdlib-reference.md` names the module a name lives in).

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
- The compiler builds the graph from `var`/`let`, `wait` and `sync`; after that those rules no
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
- Nothing is written to the output because of an unused value: an expression statement whose value
  no one needs is a **warning**, not a silent no-op, only where the language says so
  (`SemanticDiag::EffectNotExecuted`, `SemanticDiag::UnusedPureResult`,
  `SemanticDiag::UnusedFallibleValue`).

Sources: `docs/language/04_execution_model.md` §4.7, `docs/cli.cn.md`.

**Common errors.** A pass list is not a run. `camel f.cml std::gir` prints the graph and stops,
so a reader who wanted output sees none; pass a scheduling pass (`std::nvm`) when output is what is
wanted, and keep `std::gir` for inspection (§10).

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
  front of `func f(y: float): float`, `f<3.0>(2.0)` is the value at `x = 3.0, y = 2.0` and
  `grad(f)<3.0>(2.0)` is its derivative in `x`; a function with no `with` parameter is
  `SemanticDiag::NotDifferentiable`, and a function-typed parameter is not a weight.
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

**Common errors.** An idiom is only an idiom if its types line up: a signature that mixes the two
spellings of the 32-bit integer is `SemanticDiag::NoMatchingFunction` at the call, and the fix is one
spelling throughout (§5, §12.4).

<!-- camel-snippet: counter-example: one type has one spelling in a signature -->

```camel
func add(a: i32, b: int): int {
    return a + b
}
```

## 12. Common errors

Each entry is *what a first draft writes* — *what the compiler answers* — *what to write instead*.
The diagnostic names are the compiler's, and every counter-example below is compiled by
`node scripts/camel-for-ai.mjs --verify-errors`, which checks that the diagnostic named here is the
one the compiler reports.

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

**2. `Tensor` with no import.** `Tensor` is the type name the `tensor` module exports, so it is
imported; without that the name is unknown and the compiler answers
`SemanticDiag::UnresolvedTypeReference`. **Wrong:**

<!-- camel-snippet: counter-example: Tensor is the tensor module's type name and has to be imported -->

```camel
func f(x: Tensor): int {
    return 0
}
```

**Write instead** — import the name and build values with the module's functions: a shaped array is
a different type, so `f32[2, 3]` is six floats and the tensor operators do not take it. A bare
`Tensor` parameter is on its way out (**target**, `docs/design-principles.cn.md` §5.4).

```camel
import tensor
import { Tensor } from tensor

func twice(x: Tensor): float {
    return tensor::sum(tensor::multiply(x, 2.0))
}

func main(): int sync {
    println(twice(tensor::new([1.0, 2.0])))
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
`SemanticDiag::DeclaredTypeMismatch`. `'42' as int` is an error too, and it is the other one:
`as` is no numeric conversion, so the compiler answers `SemanticDiag::DynamicCastForbidden`.
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

**7. A `when` after the body.** The condition belongs between the result type and the body, and a
`when` written after the body is a syntax error: after a function body the compiler answers
`SyntaxDiag::UnexpectedToken`, because the condition does not fit where it was written. **Wrong:**

<!-- camel-snippet: counter-example: when goes before the body of a function, not after it -->

```camel
func pick::<T>(x: T): T {
    return x
} when is_ordered::<T>()
```

A trailing `when` on a `rule` is the same mistake, and the compiler has a diagnostic for that
position: `SyntaxDiag::WhenAfterBody`, whose message says the condition comes before the body it
guards.

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

**13. A `var` parameter given a `let`.** A `var` parameter is a mutable reference to a variable,
so a binding that cannot be written back is refused with
`SemanticDiag::ImmutablePassedToVarParam`. **Wrong:**

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

**17. `try`/`catch`.** They are not Camel. The full spelling is accepted by the grammar and
rejected with `SemanticDiag::TryCatchNotInLanguage`. **Wrong:**

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

**18. A fault.** A fault is not an error value: it stops the program with a located diagnostic and
exit code 1, and `!!` does not catch it. `println(xs[5])` on a three-element array answers
`RuntimeDiag::IndexOutOfRange`. **Write instead** — check the index, or take a slice, which
tolerates the end (`xs[3:9]`).

**Common errors.** A reserved spelling reads like a construct the language has, and the compiler
says otherwise: `typeof` is accepted by the grammar and refused where it is used
(`SyntaxDiag::UnexpectedToken`), so the fix is the spelling the type system offers (§5).

<!-- camel-snippet: counter-example: a reserved spelling is read and then refused -->

```camel
func kind(x: int): int {
    return typeof(x)
}
```

## 13. Accepted and then rejected

These spellings parse or are reserved, and the compiler rejects them on purpose. Do not build on
them: most have no decided meaning yet. The diagnostics are named as the compiler prints them.

| Spelling | The compiler answers | Write instead |
|---|---|---|
| Spread: `[...xs, y]`, `{ ...p, x: 1 }`, `f(...args)` | `SemanticDiag::ConstructNotSupported` | `concat(xs, [y])`; write the fields out |
| `e is T` | `SemanticDiag::ConstructNotSupported` | nothing — there is no type at run time; use a `match` over an `enum` |
| Non-null assertion `x!` | `SemanticDiag::ConstructNotSupported` | `x ?? fallback`, or `match x` |
| `f ?.. g` | `SemanticDiag::ConstructNotSupported` | nothing |
| `try { } catch e: T { } finally { }` | `SemanticDiag::TryCatchNotInLanguage` | return `T!`, open it with `!!`, `?->`, `case error e` |
| Tuple patterns, struct patterns, identifier patterns for ordinary types | `SemanticDiag::ConstructNotSupported` | `if` chains; read the members first |
| Destructuring `let { x, y } = p`, `let [a, b] = xs` | `SemanticDiag::FeatureNotSupported` | `p.x`, `xs[0]` (tuple destructuring works) |
| A slice that cuts a dimension other than the first: `xs[:, 0]`, `xs[a:b, c]` | `SemanticDiag::ConstructNotSupported` | take the rows with `take` first, one dimension at a time |
| A slice step other than 1: `xs[a:b:s]` | `RuntimeDiag::InvalidArgument` for arrays, `SemanticDiag::ArgumentsRejected` for tensors | `xs->slice<0, a, b, s>` |
| Type operations `A & B`, `A \ B`, `A + B`, `A - B`, `A ^ B` | `SemanticDiag::FeatureNotSupported` | nothing |
| `name of T` | `SemanticDiag::FeatureNotSupported` | nothing |
| `keyof` | a syntax error | nothing |
| Declaring a function type: `let f: (x: int) => int = g` | `SemanticDiag::FeatureNotSupported` | let the type be inferred: `let f = g` |

Source: `docs/language/08_top_level_and_grammar.md` §8.6.

**Common errors.** Writing a `target` feature as if it already existed. The compiler refuses it
today rather than doing something else: a stream spelling is `SemanticDiag::NoMatchingFunction`
(§9 above), and the fix until the feature lands is the array form of the same program. Every
`target` paragraph names the design draft that owns it.

## 14. Not implemented yet (`target`)

Everything here is decided and absent; code written against it does not compile today. Each entry
names the design it comes from.

- **Streams**: `T..`, `yield`, `tee`. `docs/design/io-concurrency-streams/`; the ruling is recorded
  in `docs/design-principles.cn.md` §4.2.
- **Match guards**: `case p when <condition> => ...`. `docs/design-principles.cn.md` §4.4. A guard
  written today is a syntax error, not a filter.
- **Type operations and `of`**: `A & B`, `A | B`, `A \ B`, `A + B`, `A - B`, `A ^ B`, `name of T`.
  `docs/language/08_top_level_and_grammar.md` §8.6.
- **Deleting the two-parameter `range(a, b)`**: `a..b` is the interval now, and `range(a, b)` is the
  same value under a name that is on its way out — `docs/design-principles.cn.md` §4.2. `range` with
  a step is not affected.
- **A bare `Tensor` in a signature**: it becomes an explicit type parameter over a shaped type
  (**target**, `docs/design-principles.cn.md` §5.4). `Tensor` is still the `tensor` module's own
  type name and is imported by name (§12.2).
- **Automatic differentiation and ONNX export over `enum`**: both refuse with a located rejection
  rather than mis-compiling (`docs/language/03_data_model.md` §3.5). That is a decision to work with,
  not a `target` to wait for: an enum at a differentiated boundary needs an explicit reading.

## Appendix A. Diagnostics (generated)

Every diagnostic the compiler, the passes and the run time can report. `Sev` is `E` for an error
(which fails the compile or stops the run) and `W` for a warning. The message is the template the
report fills in, with `{0}`, `{1}` for the values it names; the compiler prints a help line under it
when the message has one, and `camel --explain <name or code>` prints the entry.

Source: `diagnostics/registry.tsv`, the one place a diagnostic is declared. Regenerate with
`node scripts/camel-for-ai.mjs --write`.

<!-- camel-for-ai region: diagnostics (generated from diagnostics/registry.tsv; node scripts/camel-for-ai.mjs --write) -->
| Name | Sev | What the compiler says |
|---|---|---|

**Internal** (`InternalDiag`) — 1 (1 error, 0 warnings)

| `UnknownInternalError` | E | An error has occurred: {0}. |

**Lexical** (`LexicalDiag`) — 5 (4 errors, 1 warning)

| `IllegalCharacter` | E | Illegal character '{0}'. |
| `InvalidNumberFormat` | E | Invalid number format: '{0}'. |
| `UnclosedStringLiteral` | E | This string literal is not closed. |
| `UnterminatedComment` | E | Unterminated comment: {0}. |
| `InvalidEscapeSequence` | W | Invalid escape sequence in string: {0}. |

**Syntax** (`SyntaxDiag`) — 19 (18 errors, 1 warning)

| `MissingClosingDelimiter` | E | Expected '{0}' before {2}, to close the '{1}'. |
| `UnmatchedClosingDelimiter` | E | Unexpected '{0}': no '{1}' is open here. |
| `MissingThen` | E | Expected 'then' after the condition of the 'if', before {0}. |
| `ExtraSemicolon` | E | Unexpected ';': there is no statement between it and the ';' before it. |
| `AssignmentInCondition` | E | '=' assigns; it cannot be used in the condition of an 'if'. |
| `KeywordAsIdentifier` | E | '{0}' is a keyword and cannot be used as a name. |
| `MissingComma` | E | Expected {1} before {0}. |
| `MissingInitializer` | E | The declaration of '{0}' has no value. |
| `MissingToken` | E | Expected {0} before {1}. |
| `UnexpectedToken` | E | Unexpected {0}{1}. |
| `UnexpectedEndOfFile` | E | The file ends in the middle of a construct{0}. |
| `ComparisonNeedsSpace` | E | This '<' touches {0}, so it opens an angle-bracket argument list, which {1} does not continue. |
| `AngleCloseAfterSpace` | E | This '>' follows a space and is read as the comparison operator, so the angle-bracket list it was meant to close is still open at {0}. |
| `LineStartsWithOperator` | E | A statement cannot start with '{0}': the line break before it ended the statement of the line above. |
| `EmptyInterpolation` | E | An interpolation has no expression to format. |
| `AsRightSideIsType` | E | The right side of '{0}' is a type; write the interval as '(a..b)'. |
| `WhenAfterBody` | E | A 'when' condition comes before what it guards, so {0} cannot follow it. |
| `LambdaParametersNeedParentheses` | E | A lambda's parameters need their parentheses: write '(x) => e', not 'x => e'. |
| `ComparisonAcrossLines` | W | This '>' is read as a comparison with the next line, which starts with {0}: two statements may have been joined into one. |

**Semantic** (`SemanticDiag`) — 202 (190 errors, 12 warnings)

| `Redeclaration` | E | Redeclaration of reference: '{0}'. |
| `ModuleNotFound` | E | Module '{0}' not found. {1} |
| `ReservedIdentifier` | E | Identifiers starting and ending with double underscores are reserved for internal use. |
| `TupleUnpackingCountMismatch` | E | Tuple unpacking requires the same number of references and data nodes, or exactly one tuple data. |
| `InvalidLiteral` | E | Invalid {0} literal: '{1}'. |
| `DuplicateDictKey` | E | Duplicate key in dict: '{0}'. |
| `DuplicateParameter` | E | Duplicate parameter detected: '{0}'. |
| `UnresolvedTypeReference` | E | Unresolved type reference: '{0}'. |
| `UnresolvedReference` | E | Unresolved reference: '{0}'. |
| `BranchReturnTypeMismatch` | E | Return type mismatch between branches (in function '{0}'): expected '{1}', found '{2}'. |
| `InvalidAccessIndex` | E | Invalid access index: '{0}'. |
| `TypeNotIndexable` | E | Type '{0}' does not support index access. |
| `NoMatchingFunction` | E | No matching function found for argument types: {0}. |
| `ArgumentsMismatch` | E | Function of type '{0}' cannot be called with argument types: {1}. |
| `ReturnTypeMismatch` | E | Return type '{0}' does not match the function's declared return type '{1}'. |
| `ElementTypeMismatch` | E | Cannot insert data of type '{1}' into a(n) {0} with element type '{2}'. |
| `CallingIncompleteFunction` | E | Cannot call function '{0}' with type '{1}' as its type is not fully constructed yet. |
| `ImportNameNotExported` | E | Reference '{0}' is declared as imported but is not exported by its module. |
| `LiteralNotRepresentable` | E | The literal '{0}' cannot be represented as type '{1}': {2}. |
| `DynamicCastForbidden` | E | Dynamic cast from '{0}' to '{1}' is not allowed. |
| `ModuleParseFailed` | E | Failed to parse module file: {0}. |
| `InvalidOperatorURI` | E | Invalid operator URI format: '{0}'. Expected 'protocol:path'. |
| `EntryModuleMissingMain` | E | Entry module '{0}' must define a resolvable 'main' on the module root (__root__). |
| `ArgumentsRejected` | E | Invalid call to '{0}' with argument types {1}: {2}. |
| `ImmutablePassedToVarParam` | E | '{0}' is immutable and cannot be passed to a var parameter of '{1}'. |
| `AssignmentTargetNotVariable` | E | The target of an assignment must be a variable, or a path from one. |
| `ConstructNotSupported` | E | '{0}' is not supported: {1}. |
| `InvalidTypeArgument` | E | The arguments of the type '{0}<...>' are not accepted: {1}. |
| `ArgumentNeedsConversion` | E | The call to '{0}' needs an explicit conversion: {1}. |
| `ReturnNeedsConversion` | E | The returned value needs an explicit conversion to the function's declared return type '{0}': {1}. |
| `ImportNameConflict` | E | The name '{0}' is imported from the modules '{1}' and '{2}' with {3}. |
| `FeatureNotSupported` | E | The feature '{0}' is not supported yet. |
| `TypeVariableRedeclared` | E | The type parameter '{0}' is declared twice at the head of this declaration. |
| `TypeVariableNotBindable` | E | The type variable '{0}' of '{1}' can never be bound: {2}. |
| `GenericArgumentsMismatch` | E | The generic function '{0}' cannot be called with argument types {1}: {2}. |
| `GenericFunctionAsValue` | E | The generic function '{0}' cannot be given its types here: {1}. |
| `GenericInstantiationTooDeep` | E | Instantiating the generic function '{0}' with {1} is refused: {2}. |
| `TypeFunctionArguments` | E | The type function '{0}' cannot be used this way: {1}. |
| `GenericFunctionUnsupported` | E | The generic function '{0}' cannot be {1}. |
| `FunctionNameAsValue` | E | '{0}' is used as a value here, but it names {1}, not one function. |
| `ThrowOutsideFallible` | E | 'throw' needs a function declared to return 'T!': {0}. |
| `ThrowNotAnError` | E | 'throw' takes an error value, not a value of type '{0}'. |
| `TaggedTypeNesting` | E | The type '{0}' marks '{1}' twice. |
| `TaggedValueNotOpened` | E | A value of type '{0}' cannot be used here: {1}. |
| `TryCatchNotInLanguage` | E | 'try'/'catch' is not part of Camel. |
| `TaggedOperandMismatch` | E | '{0}' cannot be applied here: {1}. |
| `TaggedMatchCases` | E | This 'match' on a value of type '{0}' is not well formed: {1}. |
| `ImportedModuleHasErrors` | E | Cannot import module '{0}': it has errors ({1}). |
| `CollectionKeyNotAllowed` | E | The type '{0}' cannot be a key of '{1}': {2}. |
| `DuplicateCollectionKey` | E | The key '{0}' is written twice in this literal converted to '{1}'. |
| `MatchWithoutDefault` | E | This 'match' on a value of type '{0}' has no 'case _'. |
| `IfWithoutElseAsValue` | E | This 'if' has no 'else' and is used as a value. |
| `EmptyArrayWithoutType` | E | '{0}' is bound to an empty array '[]' that has no element type. |
| `PartialApplicationRefused` | E | The context arguments <{0}> cannot be bound to '{1}' as a value: {2}. |
| `ComprehensionSourceRefused` | E | The source of this list comprehension has type '{0}', which has no elements to take. |
| `LogicalOperandNotBool` | E | The {1} side of '{0}' has type '{2}', not 'bool'. |
| `ExportedVariable` | E | The variable '{0}' cannot be exported. |
| `ModuleValueCycle` | E | The value '{0}' depends on itself: {1}. |
| `CaptureAfterRecursiveCall` | E | The function '{1}' uses '{0}' only after it is called in its own recursion, where the call could not pass it. |
| `AmbiguousName` | E | The name '{0}' is ambiguous here: {1}. |
| `ConditionNotBool` | E | The condition of this 'if' has type '{0}'; a condition is a 'bool'. |
| `MatchCaseTypeMismatch` | E | This case of a 'match' on a value of type '{0}' is a value of type '{1}'. |
| `UnusedPureResult` | E | The result of '{0}' is not used, and '{0}' changes nothing: it returns a new value{1}. |
| `OperatorNotOverloadable` | E | '{0}' cannot be overloaded: {1}. |
| `OperatorOverloadInvalid` | E | This overload of operator '{0}' is not valid: {1}. |
| `OperatorOverloadConflict` | E | Operator '{0}' is already defined for ({1}){2}. |
| `AmbiguousCall` | E | The call to '{0}' with ({1}) fits more than one overload. |
| `DeclaredTypeMismatch` | E | This value has type '{0}', which is not the declared type '{1}'. |
| `ModuleTypeCycle` | E | The type '{0}' is part of a cycle of types: {1}. |
| `InvalidAnnotation` | E | The annotation '@{0}' is not accepted: {1}. |
| `LoopBodyTypeMismatch` | E | The body of this loop gives a value of type '{0}'; the state of the loop has type '{1}'. |
| `LoopStateWithoutType` | E | The first value of the loop state '{0}' does not give it a type ('{1}'). |
| `LoopConditionNotBool` | E | The condition after 'while' has type '{0}'; a condition is a 'bool'. |
| `LoopNameRepeated` | E | The name '{0}' is declared twice in this loop head. |
| `LoopSequenceNotIterable` | E | 'for' takes the elements of an array, a string, a map or a tensor; '{0}' has type '{1}'. |
| `DuplicateEnumVariant` | E | The variant '{0}' is declared twice in enum '{1}'. |
| `DuplicateEnumField` | E | The field '{0}' is declared twice in variant '{1}' of enum '{2}'. |
| `EnumVariantFieldCount` | E | The variant '{0}' of enum '{1}' takes {2} field(s), but {3} were given. |
| `EnumPatternMismatch` | E | The pattern '{0}' does not match a value of type '{1}': {2}. |
| `EnumMatchNotExhaustive` | E | The match over '{0}' has no case for {1}, and no 'case _'. |
| `EnumFieldAccess` | E | '{0}' is a value of enum '{1}': its fields are not read by name or index. |
| `EnumTypeArgumentNotInferred` | E | Cannot infer the type argument '{0}' of enum '{1}' from the fields of '{2}'. |
| `EnumFieldTypeMismatch` | E | Field {0} of variant '{1}' has type '{2}', but the value given has type '{3}'. |
| `EnumConstructionMismatch` | E | Variant '{0}' of enum '{1}' cannot be built from this value: {2}. |
| `ConstasNotAllowed` | E | The dimension name '{0}' is bound here, but only a parameter type can bind one. |
| `DimensionNameNotBound` | E | The dimension name '{0}' is not bound by any parameter of this signature. |
| `DimensionNameAlreadyBound` | E | The dimension name '{0}' is already bound; write '{0}' here. |
| `DimensionMismatch` | E | Argument {0} has extent {1}, but the dimensions the earlier arguments bound do not allow it. |
| `InvalidExtentExpression` | E | This is not a shape: the extents of a type written with dimension names are integers, bound names, or linear expressions over them ('f32[n, 3]'). |
| `NotDifferentiable` | E | The function cannot be differentiated: {0}. |
| `TagNotAtPosition` | E | The tag '{0}' may not be written at a {1}: it is declared for {2}. |
| `TagUndeclared` | E | No tag '{0}' is declared. |
| `TagArgumentCount` | E | The tag '{0}' takes {1} parameter(s), but {2} argument(s) are written. |
| `TagArgumentNotConstant` | E | The argument '{0}' of the tag '{1}' is not a compile-time value. |
| `TagOnUnnamedField` | E | A tag on the field '{0}' of an unnamed struct type belongs to no declaration. |
| `TagArgumentNamed` | E | A tag argument is positional: '{0}' is named. |
| `TagArgumentType` | E | The parameter '{0}' of the tag '{1}' is declared '{2}', but the argument is '{3}' ({4}). |
| `TagNotAPermission` | E | The tag '{0}' grants no numeric permission. |
| `RuleTagNotRequires` | E | A rule may be preceded only by '#requires<...>', not by the tag '{0}'. |
| `TagParameterType` | E | The parameter '{0}' of the tag '{1}' is declared '{2}': a tag parameter is an int, a float, a bool, a string or a func. |
| `TagShadowsPermission` | E | The tag '{0}' is declared with the name of a tag of the language. |
| `StaticMatchNotExhaustive` | E | No case of this 'match type' selects the type '{0}'. |
| `StaticMatchUnreachableCase` | E | The case '{0}' selects every type the cases after it could select. |
| `StaticTypeNotConcrete` | E | The type matched here is still '{0}' when the function is instantiated. |
| `StaticMatchTooDeep` | E | The 'match type' bodies are nested {0} deep. |
| `GenericInstantiationCycle` | E | The generic function '{0}' is instantiated with {1} while that same instance is being built, and its return type is not known yet. |
| `StaticForNotComposite` | E | The type '{0}' has no fields for a 'static for' to walk. |
| `StaticAccessNotInternal` | E | A field cannot be taken from '{0}' by a compile-time key. |
| `StaticAccessUnknownField` | E | The struct '{1}' has no field '{0}'. |
| `StaticAccessIndexOutOfRange` | E | The key '{0}' is not an element of the tuple '{1}' ({2} elements). |
| `StaticAccessNotConstant` | E | The key '{0}' of a 't.[...]' access is not known when the function is compiled. |
| `StaticComprehensionVoidElement` | E | The block for the field '{0}' of '{1}' gives no value, so the comprehension has no element for it. |
| `StaticIndicesNotTuple` | E | 'indices' walks the positions of a tuple, but '{0}' has type '{1}'. |
| `StaticLoopSequenceNotKnown` | E | The sequence of a 'static loop' must be known when the function is compiled: '{0}' is not. |
| `RestParameterNotLast` | E | The rest parameter '..{0}' must be the last parameter. |
| `RestParameterTypeNotSupported` | E | The rest parameter '..{0}' cannot write a type. |
| `RestArgumentNotLast` | E | The rest argument '..{0}' must be the last positional argument. |
| `RestArgumentNotTuple` | E | The rest argument '..{0}' is not a compile-time tuple. |
| `RestFunctionNeedsExpansion` | E | The rest function '{0}' cannot be used here: {1}. |
| `RestArgumentsMismatch` | E | The call to '{0}' gives {1} argument(s), but its fixed parameters need {2}. |
| `StaticTupleEscapes` | E | The build-time tuple '{0}' is not a value. |
| `ConstasTypeRequired` | E | The 'constas' parameter '{0}' writes no type. |
| `ConstasValueNotConstant` | E | The value given for the 'constas' parameter '{0}' is not a compile-time constant of its parameter's type ('{1}'). |
| `ConstasFunctionNeedsCall` | E | The declaration '{0}' takes a 'constas' parameter, so it has no function value. |
| `ConstasInNativeDeclaration` | E | The body-less declaration '{0}' cannot take a 'constas' parameter. |
| `ConstasArgumentMissing` | E | The call gives no value for the 'constas' parameter '{0}'. |
| `TagValueConflict` | E | The tag '{0}' gives the fact '{1}' the value {2}, but the node already says {3}. |
| `DimensionAfterAnyRank` | E | A dimension pack ('..') must be the last item of its '[...]', and only one may be written. |
| `UnknownResource` | E | The name '{0}' in the resource clause of '{1}' is neither a parameter nor a resource. |
| `PureAndUses` | E | The signature of '{0}' says both 'pure' and 'uses'. |
| `ResourceGroupCycle` | E | The resource group '{0}' contains itself: {1}. |
| `HandleTypeNotNominal` | E | '{0}' is not a nominal type declaration, so '#resource' says nothing about it. |
| `NativeResourceMismatch` | E | The declaration '{0}' says '{1}', but the operator '{2}' declares '{3}'. |
| `CmlUnsupportedConstruct` | E | std::cml cannot print {0}: {1}. |
| `CmlBigConstant` | E | std::cml refused the constant '{0}' of type {1} in {2}: {3} bytes, over max_literal_bytes ({4}). |
| `WhenConditionNotBool` | E | The 'when' condition of '{0}' is not a bool (it is '{1}'). |
| `WhenConditionNotConstant` | E | The 'when' condition of '{0}' cannot be evaluated when the declaration is instantiated: {1}. |
| `WhenUnknownName` | E | '{0}' is not visible to the 'when' condition of '{1}'. |
| `WhenTypeQueryNotConcrete` | E | The type query '{0}' cannot be answered for '{1}' in the 'when' condition of '{2}': the type still has a type variable. |
| `WhenConditionUnmet` | E | No candidate of '{0}' applies to ({1}): every candidate's 'when' condition is false. Candidates: {2} |
| `WithArgumentUnknown` | E | '{0}' is not a 'with' parameter of '{1}'. |
| `WithArgumentDuplicate` | E | The 'with' parameter '{0}' of '{1}' is given twice. |
| `WithArgumentMissing` | E | The call gives no value for the 'with' parameter '{0}' of '{1}', and it has no default. |
| `WithDefaultNotConstant` | E | The default of the 'with' parameter '{0}' is not a compile-time constant: {1}. |
| `WithDefaultWrongType` | E | The default of the 'with' parameter '{0}' has type '{1}', not '{2}'. |
| `WithParameterAfterDefault` | E | The 'with' parameter '{0}' has no default and follows '{1}', which has one. |
| `WithArgumentCallee` | E | The 'with' argument '{0}' is given by name, and the signature of '{1}' is not known where the binding is written. |
| `WithArgumentInValue` | E | The 'with' argument '{0}' is given by name, and this binding is used as a function value. |
| `WhenPredicateNotImported` | E | '{1}' is declared in module '{2}', but that name is not imported where the 'when' condition of '{0}' is written. |
| `WhenPredicateNotCompileTime` | E | '{1}' is not a compile-time predicate that the 'when' condition of '{0}' can call: {2}. |
| `WhenPredicateAmbiguous` | E | The 'when' condition of '{0}' calls '{1}', and that name is ambiguous: it is imported from {2}. |
| `NativeDeclarationIncomplete` | E | The body-less declaration '{0}' is incomplete: it has no result type. |
| `NativeDeclarationNotLoaded` | E | The declaration '{0}' names the operator '{1}', which no loaded module implements. |
| `NativeDeclarationOverlap` | E | Two declarations of '{0}' can take the same call: '{1}' and '{2}'. |
| `WhenDeclarationOverlap` | E | Two declarations of '{0}' both apply to the type '{1}': '{2}' and '{3}'. |
| `NativeDeclarationForeignOperator` | E | The declaration '{0}' names the operator '{1}', which another module declares. |
| `LambdaParameterWithoutType` | E | The type of the parameter '{0}' of this lambda is not written and cannot be inferred: {1}. |
| `TypeOfNotAValue` | E | typeof {0} has no value type: {1}. |
| `TypeOfNotWholeType` | E | typeof is written inside the larger type {0}. |
| `SameShapeComprehensionNotComposite` | E | {0} is not a struct or a tuple, so it has no shape to fill. |
| `SameShapeComprehensionMismatch` | E | The shape {0} is filled from the fields of {1}. |
| `StaticMatchPatternCannotMatch` | E | The identity pattern '{0}' cannot select a type here: {1}. |
| `FoldCallArgumentNotConstant` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallHasEffects` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallTooDeep` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallResultNotConstant` | E | The call of '{0}' cannot be used as a compile-time constant: {1}. |
| `FoldCallNotEvaluable` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `ParamArgumentUnknown` | E | '{0}' is not a parameter of '{1}'. |
| `ParamArgumentDuplicate` | E | The parameter '{0}' of '{1}' is given twice. |
| `ParamArgumentMissing` | E | The call gives no value for the parameter '{0}' of '{1}', and it has no default. |
| `ParamArgumentCallee` | E | The argument '{0}' is given by name, and the signature of '{1}' is not known where the call is written. |
| `OverloadParameterNames` | E | The argument '{0}' is given by name, and the overloads of '{1}' do not name the parameter at that position the same way. |
| `ParamArgumentPositionalAfterNamed` | E | A positional argument follows a named one in the call of '{0}'. |
| `ParamDefaultNotConstant` | E | The default of the parameter '{0}' is not a compile-time constant: {1}. |
| `ParamDefaultWrongType` | E | The default of the parameter '{0}' has type '{1}', not '{2}'. |
| `ParamAfterDefault` | E | The parameter '{0}' has no default and follows '{1}', which has one. |
| `ParamArgumentWrongType` | E | The argument '{0}' of '{1}' has type '{2}', not '{3}'. |
| `CompOperandMismatch` | E | '..' takes two functions or two integers; the left operand is '{0}' and the right one '{1}'. |
| `CompOperandNotUnary` | E | '..' composes a unary function on the left; '{0}' takes {1} parameters. |
| `CompOperandGenericWithoutType` | E | '..' composes two functions, and its {0} operand, the generic function '{1}', has no instance here: write its type arguments ('{1}::<int>'), or give the whole expression an expected function type. |
| `RangeTwoArgumentsRemoved` | E | The two-parameter 'range(a, b)' is gone; write the interval 'a..b'. |
| `RangeStepZero` | E | The range 'range({0}, {1}, 0)' has a step of 0, so it is no sequence of integers. |
| `IgnoredSideEffect` | W | {0} is not in a sync block, and {1}. |
| `EffectNotExecuted` | W | {0} has side effects but is never executed: nothing depends on it and it is not in a sync block. |
| `ExitInUnusedBranch` | W | This '{0}' gives the value of the '{1}' it is in, and nothing uses that value: it does not leave the function. |
| `UnusedFallibleValue` | W | This value has type '{0}' and nothing uses it: an error it holds is lost{1}. |
| `NullTestNeverNull` | W | {0} has type '{1}', which is never null: this test is always {2}. |
| `KnownNotNull` | W | '{0}' is known not to be null here (it has type '{1}'): the null case of '{2}' is never taken. |
| `EnumVariantCaseRepeated` | W | The variant '{0}' is matched more than once; the later case is never taken. |
| `MatchCaseAfterWildcard` | W | This case follows 'case _': it is never taken. |
| `MatchWildcardRedundant` | W | Every variant of '{0}' is matched before 'case _': the wildcard is never taken. |
| `TagHasNoReader` | W | The tag '{0}' is written, but none of the passes it names ({1}) is in the pipeline. |
| `DimensionNameUsedOnce` | W | The dimension name '{0}' is written once in this signature, so it relates nothing. |
| `TypeVariableUnused` | W | The type parameter '{0}' is declared at the head but never written in the signature. |

**Run time and passes** (`RuntimeDiag`) — 38 (35 errors, 3 warnings)

| `RuntimeError` | E | An error has occurred: {0}. |
| `DivisionByZero` | E | Attempted division by zero. |
| `IndexOutOfRange` | E | Index {0} is out of range for {1} of length {2}. |
| `MaxRecursionDepthExceeded` | E | Maximum recursion depth ({1}) exceeded when evaluating graph '{0}'. |
| `TensorDimensionMismatch` | E | Tensor shape mismatch in '{0}': {1}. |
| `UnrecognizedGraphPass` | E | Unrecognized graph rewriting/scheduling/translation pass '{0}'. |
| `TranslationRefused` | E | Translation pass '{0}' refused the graph: {1}. |
| `PassInternalError` | E | Pass '{0}' failed with an internal error: {1}. |
| `MacroEvaluationFailed` | E | '{0}' could not be evaluated by std::macro: {1}. |
| `EntryArgumentMissing` | E | No value was given for the {0} {1} of 'main'. |
| `EntryArgumentInvalid` | E | Cannot bind the argument '{0}' of 'main': {1}. |
| `EntryResultNotWritten` | E | Cannot write the result of 'main' to '{0}': {1}. |
| `MemoryExhausted` | E | Out of memory{0}: {1}. |
| `InvalidNumberText` | E | Cannot convert the string '{0}' to {1}. |
| `FormatFailed` | E | Cannot format with the format string '{0}': {1}. |
| `OperandTypeUnsupported` | E | '{0}' does not support a value of type '{1}'; it takes {2}. |
| `FieldNotFound` | E | The struct has no field '{0}'. |
| `InvalidArgument` | E | Invalid argument of '{0}': {1}. |
| `FileOpenFailed` | E | '{0}' cannot open the file '{1}': {2}. |
| `FileSystemError` | E | '{0}' failed for '{1}': {2}. |
| `RegexInvalid` | E | '{0}': '{1}' is not a valid regular expression: {2}. |
| `TimeFormatFailed` | E | '{0}' failed: {1}. |
| `GradientElementTypeMismatch` | E | The gradient of a value of type '{0}' has element type '{1}': {2}. A gradient must have the element type of the value it is the gradient of (design principles 9.9); accumulated as it is, the value would be read at the wrong width and answer with a number that is not the derivative. |
| `ValueWithNoDerivativeRule` | E | The operator '{0}' takes a differentiable value through its with parameter {1} (counting from 1), of type '{2}', but {3}. |
| `ConversionOutOfRange` | E | Cannot convert {0} to {1}: {2}. |
| `UnhandledError` | E | An error value reached the exit of 'main' without being handled: {0}: {1}. |
| `SliceOutOfRange` | E | The range {0} to {1} is out of range for {2} of length {3}. |
| `InvalidPassParameter` | E | Invalid pass specification '{0}': {1}. |
| `GraphPassFailed` | E | Pass '{0}' failed: {1}. |
| `PassNeedsExtraction` | E | Pass '{0}' cannot take the program: some values of it have several writings. |
| `PassNotProvidedByModule` | E | Module '{0}' provides no pass '{1}'; it provides {2}. |
| `PassNotExportedByModule` | E | Module '{0}' declares '{1}' but does not export it, so it cannot be named as a pass; it provides {2}. |
| `DeviceUnavailable` | E | CUDA device {0} was named by --devices but is not available: {1}. |
| `GpuScopeUnsupported` | E | `#gpu` scope '{0}': {1} cannot run on the device ({2}). |
| `GpuScopeNoDeviceProgram` | E | `#gpu` scope '{0}': {1} ({2}). |
| `PassesNeverRun` | W | Pass '{0}' consumes the graph, so {1} written after it will not run. |
| `ConstantExpressionFaults` | W | This constant expression faults whenever it is evaluated ({0}); it is not folded. |
| `TranslationLinksRuntime` | W | std::cpp has no standalone translation of {0}: the program it writes calls the Camel runtime there and is built with libcamel. |

**Semantic** (`SemanticDiag`) — 202 (190 errors, 12 warnings)

| `ReducedPrecisionUnsupported` | E | The call of '{0}' cannot take a value of the reduced-precision element type '{1}'. |
| `ImplOnBodylessDeclaration` | E | The body-less declaration '{0}' cannot carry '#impl<{1}>': it has no Camel body. |
| `ImplKernelUnknown` | E | The '#impl' tag of '{0}' names '{1}', which is no body-less declaration. |
| `ImplKernelForeign` | E | The '#impl' tag of '{0}' names '{1}', which another module declares. |
| `ImplSignatureMismatch` | E | The kernel '{0}' of the '#impl' function '{1}' has the signature {2}, but the function has {3}. |
| `ImplKernelNotBodyless` | E | The '#impl' tag of '{0}' names '{1}', which has a body. |
| `StreamInContainer` | E | '{0}' cannot be the element of {1}: a stream is consumed once, while the elements of a container are read more than once. |
| `NodeOfNotAParameter` | E | '{0}' is not a const parameter of the rule '{1}', so node_of({0}) names no node. |
<!-- camel-for-ai end: diagnostics -->

## Appendix B. Standard library signatures (generated)

Every name the standard library exports, with its signature. `name: <with-parameters> (parameters) =>
result`: the ordinary parameters are the data the function works on, the `with` parameters are what
configures it, and a call chains them — `s->trim->split<','>`, `xs->filter<p>->sort_by<key>`. `int`
is the 64-bit integer and `float` the 64-bit float. A result `T!` is an error to be handled (`!!`,
`?->`, `match`), `T?` a value that may be absent (`??`). A signature whose declaration carries type
parameters is printed without them: `is_numeric: () => bool` is declared `is_numeric::<T>()` and is
written with its type argument when the arguments do not fix it. `Tensor` is a type name the `tensor`
module exports, so it is imported (§12.2).

Source: `docs/stdlib-reference.md`, generated from the declarations in `stdlib/*.cml` and
`stdlib/signatures/*.cml` by `scripts/stdlib-reference.mjs --write`. Regenerate this region with
`node scripts/camel-for-ai.mjs --write`; regenerate the page it reads with
`node scripts/stdlib-reference.mjs --write`.

<!-- camel-for-ai region: stdlib-signatures (generated from docs/stdlib-reference.md; node scripts/camel-for-ai.mjs --write) -->
**`Built-in functions`** — no import — 58 name(s)

```text
std::mem::blocks: () => { allocs: int, frees: int, live: int, bytes: int }
int: (x: i32) => int
int: (x: int) => int
int: (x: f32) => int
int: (x: float) => int
int: (x: string) => int
int: (x: bf16) => int
int: (x: f16) => int
i64: (x: i32) => int
i64: (x: int) => int
i64: (x: f32) => int
i64: (x: float) => int
i64: (x: string) => int
i64: (x: bf16) => int
i64: (x: f16) => int
i32: (x: i32) => i32
i32: (x: int) => i32
i32: (x: f32) => i32
i32: (x: float) => i32
i32: (x: string) => i32
i32: (x: bf16) => i32
i32: (x: f16) => i32
float: (x: i32) => float
float: (x: int) => float
float: (x: f32) => float
float: (x: float) => float
float: (x: string) => float
float: (x: bf16) => float
float: (x: f16) => float
f64: (x: i32) => float
f64: (x: int) => float
f64: (x: f32) => float
f64: (x: float) => float
f64: (x: string) => float
f64: (x: bf16) => float
f64: (x: f16) => float
f32: (x: i32) => f32
f32: (x: int) => f32
f32: (x: f32) => f32
f32: (x: float) => f32
f32: (x: string) => f32
f32: (x: bf16) => f32
f32: (x: f16) => f32
bf16: (x: i32) => bf16
bf16: (x: int) => bf16
bf16: (x: f32) => bf16
bf16: (x: float) => bf16
bf16: (x: bf16) => bf16
bf16: (x: string) => bf16
f16: (x: i32) => f16
f16: (x: int) => f16
f16: (x: f32) => f16
f16: (x: float) => f16
f16: (x: f16) => f16
f16: (x: string) => f16
c64: (x: i32) => c64
c64: (x: int) => c64
c64: (x: f32) => c64
c64: (x: float) => c64
c64: (x: c64) => c64
c64: (x: c128) => c64
c128: (x: i32) => c128
c128: (x: int) => c128
c128: (x: f32) => c128
c128: (x: float) => c128
c128: (x: c64) => c128
c128: (x: c128) => c128
string: (x: T) => string
parse_int: (text: string) => int!
parse_float: (text: string) => float!
error: (kind: string, message: string) => Error
print: (x: T, ...rest: U...) => T
println: (x: T, ...rest: U...) => T
eprintln: (x: T, ...rest: U...) => T
input: (prompt: string) => string
sleep: (milliseconds: int) => void
exit: (code: int) => void
format: <...args: T...> (fmt: string) => string
join: <sep: string> (xs: string[]) => string
len: (x: string) => int
len: (x: T[..]) => int
range: (start: int, end: int, step?: int) => int[]
head: (xs: T[]) => T
last: (xs: T[]) => T
tail: (xs: T[]) => T[]
slice: <start: int, end: int> (xs: T[]) => T[]
concat: (xs: T[], ys: T[]) => T[]
contains: <value: T> (xs: T[]) => bool
contains: <sub: string> (s: string) => bool
zip: (xs: T[], ys: U[]) => (T, U)[]
append: <value: T> (xs: T[]) => T[]
extend: <other: T[]> (xs: T[]) => T[]
map: <f: (item: T) => U> (xs: T[]) => U[]
filter: <f: (item: T) => bool> (xs: T[]) => T[]
reduce: <f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U
foreach: <f: (item: T) => void> (xs: T[]) => void
apply: <f: (item: T) => T> (xs: T[]) => T[]
attempt: (f: (...args: A) => T, ...args: A) => T!
len: (x: Map<K, V>) => int
len: (x: Set<T>) => int
get: <key: K> (m: Map<K, V>) => V?
get_or: <key: K, fallback: V> (m: Map<K, V>) => V
put: <key: K, value: V> (m: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Map<K, V>) => Map<K, V>
remove: (c: Set<K>) => Set<K>
has: <key: K> (c: Map<K, V>) => bool
has: (c: Set<K>) => bool
keys: (m: Map<K, V>) => K[]
values: (m: Map<K, V>) => V[]
items: (m: Map<K, V>) => (K, V)[]
items: (m: Set<T>) => T[]
merge: (m: Map<K, V>, other: Map<K, V>) => Map<K, V>
contains: <value: T> (s: Set<T>) => bool
add: <value: T> (s: Set<T>) => Set<T>
union: (s: Set<T>, other: Set<T>) => Set<T>
intersect: (s: Set<T>, other: Set<T>) => Set<T>
difference: (s: Set<T>, other: Set<T>) => Set<T>
is_subset: (s: Set<T>, other: Set<T>) => bool
```

**`str`** — import { ... } from str — 14 name(s)

```text
split: <sep: string> (s: string) => string[]
trim: (s: string) => string
trim_start: (s: string) => string
trim_end: (s: string) => string
to_upper: (s: string) => string
to_lower: (s: string) => string
replace: <from_: string, to: string> (s: string) => string
find: <sub: string> (s: string) => int?
substr: <start: int, length: int> (s: string) => string
starts_with: <prefix: string> (s: string) => bool
ends_with: <suffix: string> (s: string) => bool
repeat: <count: int> (s: string) => string
codepoints: (s: string) => int[]
from_codepoints: (xs: int[]) => string!
```

**`bytes`** — import { ... } from bytes — 6 name(s)

```text
encode: (text: string) => Bytes
decode: (content: Bytes) => string!
from_array: (values: int[]) => Bytes
to_array: (content: Bytes) => int[]
size: (content: Bytes) => int
concat: (left: Bytes, right: Bytes) => Bytes
```

**`ckpt`** — import { ... } from ckpt — 3 name(s)

```text
tree_leaves: (tree: T) => L
save_tree: (dir: string, tree: T, dataset_manifest_sha256?: string) => void
load_tree: (dir: string, dataset_manifest_sha256?: string) => T!
```

**`tree`** — import { ... } from tree — 4 name(s)

```text
tree_paths: (t: T) => ?
tree_map: <f: F> (t: T, ..ts) => ?
tree_reduce: <f: (acc: R, leaf: A) => R> (t: T, init: R) => R
tree_unzip: (r: R, s: S) => ?
```

**`tensor`** — import { ... } from tensor — 69 name(s)

```text
new: (values: Tensor | number[]) => Tensor
empty: (shape: int[]) => Tensor
zeros: (shape: int[]) => Tensor
ones: (shape: int[]) => Tensor
full: (shape: int[], value: number) => Tensor
eye: (n: int) => Tensor
range: (start: int, stop?: int, step?: int) => Tensor<int64>
random: (shape: int[], low: number, high: number) => Tensor
randn: (shape: int[], mean?: number, std?: number) => Tensor
add: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
subtract: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
multiply: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
divide: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
pow: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
maximum: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
minimum: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
where: (cond: Tensor, lhs: Tensor | number, rhs: Tensor | number) => Tensor
neg: (t: Tensor) => Tensor
abs: (t: Tensor) => Tensor
exp: (t: Tensor) => Tensor
log: (t: Tensor) => Tensor
sqrt: (t: Tensor) => Tensor
rsqrt: (t: Tensor) => Tensor
sigmoid: (t: Tensor) => Tensor
tanh: (t: Tensor) => Tensor
relu: (t: Tensor) => Tensor
gelu: (t: Tensor) => Tensor
erf: (t: Tensor) => Tensor
reshape: (t: Tensor | number[], shape: int[]) => Tensor
flatten: (t: Tensor, axis?: int) => Tensor
unsqueeze: (t: Tensor, axis: int) => Tensor
transpose: (t: Tensor) => Tensor
permute: (t: Tensor, perm: int[]) => Tensor
concat: (a: Tensor | number[], b: Tensor | number[], axis: int) => Tensor
slice: (t: Tensor, axis: int, start: int, end: int, step?: int) => Tensor
take: (t: Tensor, indices: Tensor | number[]) => Tensor
one_hot: (labels: Tensor | number[], classes: int) => Tensor
shape: (t: T[..]) => int[] when is_numeric::<T>()
shape: (t: Tensor) => int[]
numel: (t: T[..]) => int when is_numeric::<T>()
numel: (t: Tensor) => int
dim: (t: T[..], axis: int) => int when is_numeric::<T>()
dim: (t: T[..], axis: i32) => int when is_numeric::<T>()
dim: (t: Tensor, axis: int) => int
dim: (t: Tensor, axis: i32) => int
sum: (t: Tensor) => float
sum: (t: Tensor, axis: int, keepdims?: bool) => Tensor
mean: (t: Tensor) => float
mean: (t: Tensor, axis: int, keepdims?: bool) => Tensor
max: (t: Tensor, axis: int, keepdims?: bool) => Tensor
min: (t: Tensor, axis: int, keepdims?: bool) => Tensor
argmax: (t: Tensor, axis: int, keepdims?: bool) => Tensor
argmin: (t: Tensor, axis: int, keepdims?: bool) => Tensor
softmax: (t: Tensor, axis?: int) => Tensor
log_softmax: (t: Tensor, axis?: int) => Tensor
softmax_grad: (y: Tensor, dy: Tensor, axis?: int) => Tensor
layer_norm: (x: Tensor, gamma: Tensor, beta: Tensor, eps?: number) => Tensor
matmul: (lhs: Tensor, rhs: Tensor) => Tensor
linear: (x: Tensor, weight: Tensor, bias?: Tensor) => Tensor
zeros_like: (t: Tensor) => Tensor
ones_like: (t: Tensor) => Tensor
cast: (t: Tensor | number[], dtype: string) => Tensor
show: (t: Tensor) => void
show: (t: A[]) => void
from_bytes: (bytes: Bytes, shape: int[]) => Tensor
to_bytes: (t: T[..]) => Bytes when is_numeric::<T>()
to_bytes: (t: Tensor) => Bytes
load_npy: (path: string) => Tensor
save_npy: (t: T[..], path: string) => void when is_numeric::<T>()
save_npy: (t: Tensor, path: string) => void
num_threads: () => int
set_num_threads: (n: int) => void
set_num_threads: (n: i32) => void
gemm_backend: () => string
gemm_threads: () => int
gemm_library_threads: () => int
gemm_library: () => string
seed: (seed: int) => void
seed: (seed: i32) => void
```

**`types`** — import { ... } from types — 8 name(s)

```text
is_integer: () => bool
is_float: () => bool
is_complex: () => bool
is_numeric: () => bool
is_ordered: () => bool
is_function: () => bool
is_string: () => bool
is_bool: () => bool
```

**`array`** — import { ... } from array — 23 name(s)

```text
argsort: (keys: i32[]) => int[]
argsort: (keys: int[]) => int[]
argsort: (keys: f32[]) => int[]
argsort: (keys: float[]) => int[]
argsort: (keys: bool[]) => int[]
argsort: (keys: string[]) => int[]
sort: (xs: i32[]) => i32[]
sort: (xs: int[]) => int[]
sort: (xs: f32[]) => f32[]
sort: (xs: float[]) => float[]
insert_sort: (xs: i32[]) => i32[]
insert_sort: (xs: int[]) => int[]
insert_sort: (xs: f32[]) => f32[]
insert_sort: (xs: float[]) => float[]
quick_sort: (xs: i32[]) => i32[]
quick_sort: (xs: int[]) => int[]
quick_sort: (xs: f32[]) => f32[]
quick_sort: (xs: float[]) => float[]
merge_sort: (xs: i32[]) => i32[]
merge_sort: (xs: int[]) => int[]
merge_sort: (xs: f32[]) => f32[]
merge_sort: (xs: float[]) => float[]
merge_sorted_arrays: (left: i32[], right: i32[]) => i32[]
merge_sorted_arrays: (left: int[], right: int[]) => int[]
merge_sorted_arrays: (left: f32[], right: f32[]) => f32[]
merge_sorted_arrays: (left: float[], right: float[]) => float[]
reverse: (xs: T[]) => T[]
take: <n: int> (xs: T[]) => T[]
drop: <n: int> (xs: T[]) => T[]
enumerate: (xs: T[]) => (int, T)[]
flatten: (xss: T[][]) => T[]
unique: (xs: T[]) => T[]
index_of: <x: T> (xs: T[]) => int?
find: <pred: (x: T) => bool> (xs: T[]) => T?
any: <pred: (x: T) => bool> (xs: T[]) => bool
all: <pred: (x: T) => bool> (xs: T[]) => bool
count: <pred: (x: T) => bool> (xs: T[]) => int
sum: (xs: T[]) => T when is_numeric::<T>()
min: (xs: T[]) => T? when is_ordered::<T>()
max: (xs: T[]) => T? when is_ordered::<T>()
flat_map: <f: (x: T) => U[]> (xs: T[]) => U[]
sort_by: <key: (x: T) => K> (xs: T[]) => T[]
zip_with: <f: (a: T, b: U) => V> (xs: T[], ys: U[]) => V[]
```

**`math`** — import { ... } from math — 48 name(s)

```text
pi: float
e: float
inf: float
nan: float
sqrt: (x: i32) => f32
sqrt: (x: int) => float
sqrt: (x: f32) => f32
sqrt: (x: float) => float
abs: (x: i32) => i32
abs: (x: int) => int
abs: (x: f32) => f32
abs: (x: float) => float
abs: (x: c64) => f32
abs: (x: c128) => float
abs: (x: bf16) => bf16
abs: (x: f16) => f16
re: (x: c64) => f32
re: (x: c128) => float
im: (x: c64) => f32
im: (x: c128) => float
conj: (x: c64) => c64
conj: (x: c128) => c128
exp: (x: f32) => f32
exp: (x: float) => float
exp: (x: c64) => c64
exp: (x: c128) => c128
expm1: (x: f32) => f32
expm1: (x: float) => float
log: (x: f32) => f32
log: (x: float) => float
log2: (x: f32) => f32
log2: (x: float) => float
log10: (x: f32) => f32
log10: (x: float) => float
log1p: (x: f32) => f32
log1p: (x: float) => float
pow: (x: f32, y: f32) => f32
pow: (x: float, y: float) => float
hypot: (x: f32, y: f32) => f32
hypot: (x: float, y: float) => float
sin: (x: f32) => f32
sin: (x: float) => float
cos: (x: f32) => f32
cos: (x: float) => float
tan: (x: f32) => f32
tan: (x: float) => float
asin: (x: f32) => f32
asin: (x: float) => float
acos: (x: f32) => f32
acos: (x: float) => float
atan: (x: f32) => f32
atan: (x: float) => float
atan2: (y: f32, x: f32) => f32
atan2: (y: float, x: float) => float
sinh: (x: f32) => f32
sinh: (x: float) => float
cosh: (x: f32) => f32
cosh: (x: float) => float
tanh: (x: f32) => f32
tanh: (x: float) => float
floor: (x: f32) => f32
floor: (x: float) => float
ceil: (x: f32) => f32
ceil: (x: float) => float
round: (x: f32) => f32
round: (x: float) => float
trunc: (x: f32) => f32
trunc: (x: float) => float
min: (a: i32, b: i32) => i32
min: (a: int, b: int) => int
min: (a: f32, b: f32) => f32
min: (a: float, b: float) => float
min: (a: string, b: string) => string
min: (a: bool, b: bool) => bool
min: (a: bf16, b: bf16) => bf16
min: (a: f16, b: f16) => f16
max: (a: i32, b: i32) => i32
max: (a: int, b: int) => int
max: (a: f32, b: f32) => f32
max: (a: float, b: float) => float
max: (a: string, b: string) => string
max: (a: bool, b: bool) => bool
max: (a: bf16, b: bf16) => bf16
max: (a: f16, b: f16) => f16
clamp: (x: i32, lo: i32, hi: i32) => i32
clamp: (x: int, lo: int, hi: int) => int
clamp: (x: f32, lo: f32, hi: f32) => f32
clamp: (x: float, lo: float, hi: float) => float
sign: (x: i32) => i32
sign: (x: int) => int
sign: (x: f32) => f32
sign: (x: float) => float
is_nan: (x: f32) => bool
is_nan: (x: float) => bool
is_inf: (x: f32) => bool
is_inf: (x: float) => bool
gcd: (a: i32, b: i32) => i32
gcd: (a: int, b: int) => int
lcm: (a: i32, b: i32) => i32
lcm: (a: int, b: int) => int
bin: (x: i32) => string
bin: (x: int) => string
oct: (x: i32) => string
oct: (x: int) => string
hex: (x: i32) => string
hex: (x: int) => string
shl: (x: i32, count: i32) => i32
shl: (x: i32, count: int) => i32
shl: (x: int, count: i32) => int
shl: (x: int, count: int) => int
shr: (x: i32, count: i32) => i32
shr: (x: i32, count: int) => i32
shr: (x: int, count: i32) => int
shr: (x: int, count: int) => int
bit_and: (a: i32, b: i32) => i32
bit_and: (a: int, b: int) => int
bit_or: (a: i32, b: i32) => i32
bit_or: (a: int, b: int) => int
bit_xor: (a: i32, b: i32) => i32
bit_xor: (a: int, b: int) => int
bit_not: (x: i32) => i32
bit_not: (x: int) => int
```

**`statistics`** — import { ... } from statistics — 5 name(s)

```text
mean: (xs: i32[]) => float
mean: (xs: int[]) => float
mean: (xs: f32[]) => float
mean: (xs: float[]) => float
stdev: (xs: i32[]) => float
stdev: (xs: int[]) => float
stdev: (xs: f32[]) => float
stdev: (xs: float[]) => float
variance: (xs: i32[]) => float
variance: (xs: int[]) => float
variance: (xs: f32[]) => float
variance: (xs: float[]) => float
median: (xs: i32[]) => float?
median: (xs: int[]) => float?
median: (xs: f32[]) => float?
median: (xs: float[]) => float?
quantile: <q: float> (xs: i32[]) => float?
quantile: <q: float> (xs: int[]) => float?
quantile: <q: float> (xs: f32[]) => float?
quantile: <q: float> (xs: float[]) => float?
```

**`random`** — import { ... } from random — 10 name(s)

```text
seed: (value: int) => void
rand: () => float
randn: () => float
randint: (low: int, high: int) => int
choice: (xs: T[]) => T
sample: (xs: T[], n: int) => T[]
shuffle: (xs: T[]) => T[]
key: (seed: int) => { hi: int, lo: int }
split: (key: { hi: int, lo: int }) => ({ hi: int, lo: int }, { hi: int, lo: int })
permutation: (key: { hi: int, lo: int }, n: int) => int[]
```

**`time`** — import { ... } from time — 3 name(s)

```text
now: () => float
strftime: (timestamp: float, format: string) => string
strptime: (text: string, format: string) => float
```

**`fs`** — import { ... } from fs — 15 name(s)

```text
read_text: (path: string) => string
try_read_text: (path: string) => string!
read_chunk: (path: string, offset: int, size: int) => Bytes
read_bytes: (path: string) => Bytes
write_bytes: (path: string, content: Bytes) => void
replace: (path: string, content: Bytes) => void
write_text: (path: string, content: string) => void
exists: (path: string) => bool
is_file: (path: string) => bool
is_dir: (path: string) => bool
file_size: (path: string) => int
mkdir: (path: string) => bool
mkdirs: (path: string) => bool
remove: (path: string) => void!
remove_dir: (path: string) => void!
```

**`io`** — import { ... } from io — 5 name(s)

```text
set_terminal_raw_mode: (enabled: bool) => void
has_input: () => bool
get_char: () => string
get_chars: (max: int) => string
clear_input_buffer: () => void
```

**`process`** — import { ... } from process — 4 name(s)

```text
sleep: (milliseconds: int) => void
whoami: () => string
cpu_count: () => int
thread_count: () => int
```

**`env`** — import { ... } from env — 2 name(s)

```text
get_env: (name: string) => string
get_cwd: () => string
```

**`json`** — import { ... } from json — 14 name(s)

```text
encode: (value: T) => string
encode: (value: T, indent: int) => string
decode_as: <shape: T> (text: string) => T!
decode_as: <shape: T> (text: Json) => T!
decode: (text: string) => Json!
kind: (j: Json) => string
is_null: (j: Json) => bool
get: <key: string> (j: Json) => Json?
get: <key: string> (j: Json?) => Json?
get: <key: string> (j: Json!) => Json!?
get: <key: string> (j: Json!?) => Json!?
get: <key: int> (j: Json) => Json?
get: <key: int> (j: Json?) => Json?
get: <key: int> (j: Json!) => Json!?
get: <key: int> (j: Json!?) => Json!?
as_bool: (j: Json) => bool?
as_bool: (j: Json?) => bool?
as_bool: (j: Json!) => bool!?
as_bool: (j: Json!?) => bool!?
as_int: (j: Json) => int?
as_int: (j: Json?) => int?
as_int: (j: Json!) => int!?
as_int: (j: Json!?) => int!?
as_float: (j: Json) => float?
as_float: (j: Json?) => float?
as_float: (j: Json!) => float!?
as_float: (j: Json!?) => float!?
as_string: (j: Json) => string?
as_string: (j: Json?) => string?
as_string: (j: Json!) => string!?
as_string: (j: Json!?) => string!?
as_array: (j: Json) => Json[]?
as_array: (j: Json?) => Json[]?
as_array: (j: Json!) => Json[]!?
as_array: (j: Json!?) => Json[]!?
as_object: (j: Json) => Map<string, Json>?
as_object: (j: Json?) => Map<string, Json>?
as_object: (j: Json!) => Map<string, Json>!?
as_object: (j: Json!?) => Map<string, Json>!?
```

**`hash`** — import { ... } from hash — 3 name(s)

```text
sha256: (s: string) => string
sha256: (s: Bytes) => string
sha256_rounds: (s: string, rounds: int) => string
```

**`re`** — import { ... } from re — 1 name(s)

```text
test: (pattern: string, text: string) => bool
```

**`para`** — import { ... } from para — 3 name(s)

```text
rendezvous: (parties: int, timeout_ms: int) => int
unordered_foreach: <f: (item: T) => R> (xs: T[]) => void
unordered_reduce: <f: (acc: T, item: T) => T, initial: T> (xs: T[]) => T
```

**`this`** — import { ... } from this — 1 name(s)

```text
zen: () => string
```

**`graph`** — import { ... } from graph — 42 name(s)

```text
current: () => Program
graphs: (p: Program) => Graph[]
root: (p: Program) => Graph
name: (g: Graph) => string
nodes: (g: Graph) => Node[]
inputs: (n: Node) => Node[]
with_inputs: (n: Node) => Node[]
ctrl_inputs: (n: Node) => Node[]
users: (n: Node) => Node[]
value_users: (n: Node) => Node[]
kind: (n: Node) => NodeKind
op: (n: Node) => Op?
op_name: (o: Op) => string
is_pure: (n: Node) => bool
is_anchor: (n: Node) => bool
is_written_slot: (n: Node) => bool
is_constant: (n: Node) => bool
type_of: (n: Node) => TypeRef?
type_name: (t: TypeRef) => string
callee: (n: Node) => Graph?
tags: (n: Node) => string
origin: (n: Node) => int
constant: (n: Node, sample: i32) => i32?
constant: (n: Node, sample: int) => int?
constant: (n: Node, sample: f32) => f32?
constant: (n: Node, sample: float) => float?
constant: (n: Node, sample: bool) => bool?
constant: (n: Node, sample: string) => string?
region_of: (n: Node) => Region
region_kind: (r: Region) => RegionKind
region_parent: (r: Region) => Region?
show: (n: Node) => string
id: (h: Graph) => int
id: (h: Node) => int
id: (h: Op) => int
effects: (g: Graph) => Effects
shape: (n: Node) => string?
permissions: (n: Node) => int
program_of: (f: F) => Program
graph_of: (f: F) => Graph
open_function: (f: F) => Txn
to_function: (t: Txn, sample: F) => F!
open: (p: Program) => Txn
replace: (t: Txn, old: Node, new: Node) => void
erase: (t: Txn, n: Node) => void
eval: (t: Txn, n: Node) => Node?
commit: (t: Txn) => Program!
abort: (t: Txn) => void
add_constant: (t: Txn, g: Graph, value: i32) => Node
add_constant: (t: Txn, g: Graph, value: int) => Node
add_constant: (t: Txn, g: Graph, value: f32) => Node
add_constant: (t: Txn, g: Graph, value: float) => Node
add_constant: (t: Txn, g: Graph, value: bool) => Node
add_constant: (t: Txn, g: Graph, value: string) => Node
```

**`rewrite`** — import { ... } from rewrite — 22 name(s)

```text
rewrite: (p: Program, rules: Rule[]) => Program!
rewrite: (p: Program, rules: Rule[], strict: bool) => Program!
saturate: (p: Program, rules: Rule[], rounds: int) => Program!
saturate: (p: Program, rules: Rule[], rounds: int, strict: bool) => Program!
extract: (p: Program, cost: string) => Program!
check: (rules: Rule[], samples: int = 100, seed: int = 0, program: Program? = null) => CheckReport
check_text: (r: CheckReport) => string
check_row_count: (r: CheckReport) => int
check_rule: (r: CheckReport, row: int) => string!
check_instance: (r: CheckReport, row: int) => string!
check_verdict: (r: CheckReport, row: int) => string!
check_proposition: (r: CheckReport, row: int) => string!
check_evidence: (r: CheckReport, row: int) => string!
check_reason: (r: CheckReport, row: int) => string!
check_coverage: (r: CheckReport, row: int) => string!
check_coverage_table: () => string
run: <rules: Rule[], rounds: int = 16, strict: bool = false, explain: bool = false> (p: Program) => Run<Program>!
explain: (r: Run<Program>, a: Node, b: Node) => Proof!
explain_all: (r: Run<Program>) => Proof!
why_not: (r: Run<Program>, rule: Rule, v: Node) => string!
rule_name: (r: Rule) => string
rule_two_way: (r: Rule) => bool
rule_needs: (r: Rule) => int
rule_consts: (r: Rule) => int
```

**`tiles`** — import { ... } from tiles — 9 name(s)

```text
upload: (value: Tensor<f32>) => Device<Tensor<f32>>
download: (value: Device<Tensor<f32>>) => Tensor<f32>
run_preferred_resident: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => Device<f32>
source: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
inspect: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
benchmark: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
regions: <fn: (a: f32, b: f32) => f32> () => string
available: () => bool
device: () => string
```

**`pyplot`** — import { ... } from pyplot — 1 name(s)

```text
plot: (data: i32[]) => void
plot: (data: int[]) => void
plot: (data: f32[]) => void
plot: (data: float[]) => void
plot: (data: i32[], filename: string) => void
plot: (data: int[], filename: string) => void
plot: (data: f32[], filename: string) => void
plot: (data: float[], filename: string) => void
```

**`python`** — import { ... } from python — 12 name(s)

```text
py_call: (fn: string | PyObject, ...args: PyObject[]) => PyObject
py_call: (fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: Struct<...PyObject>) => PyObject
py_print: (...objs: PyObject[]) => PyObject?
py_println: (...objs: PyObject[]) => PyObject?
py_wrap: (x: T) => PyObject<T>
py_unwrap: (obj: PyObject<T>) => T
py_exec: (code: string) => PyObject
py_eval: (expr: string) => PyObject
py_run: (file_path: string) => PyObject
py_attr: (obj: PyObject, attr: string) => PyObject
py_import: (module_name: string) => PyObject
py_dispose: (obj: PyObject) => void
```

**`nn`** — import { ... } from nn — 19 name(s)

```text
conv2d: (input: Tensor, kernel: Tensor, bias: Tensor, stride?: int, padding?: int) => Tensor
conv2d_input_grad: (input: Tensor, kernel: Tensor, dy: Tensor, stride?: int, padding?: int) => Tensor
conv2d_kernel_grad: (input: Tensor, kernel: Tensor, dy: Tensor, stride?: int, padding?: int) => Tensor
conv2d_bias_grad: (bias: Tensor, dy: Tensor) => Tensor
max_pool2d: (input: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
avg_pool2d: (input: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
max_pool2d_grad: (input: Tensor, dy: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
avg_pool2d_grad: (input: Tensor, dy: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
batch_norm: (input: Tensor, mean: Tensor, var: Tensor, gamma: Tensor, beta: Tensor, eps?: number) => Tensor
batch_norm_train: (input: Tensor, gamma: Tensor, beta: Tensor, running_mean: Tensor, running_var: Tensor, momentum?: number, eps?: number) => (Tensor, Tensor, Tensor)
batch_norm_train_grad: (input: Tensor, gamma: Tensor, dy: Tensor, eps?: number) => (Tensor, Tensor, Tensor)
embedding: (table: Tensor, indices: Tensor) => Tensor
embedding_table_grad: (table: Tensor, indices: Tensor, dy: Tensor) => Tensor
softmax_cross_entropy: (logits: Tensor, target: Tensor) => float
softmax_cross_entropy: (logits: Tensor, target: Tensor | number[]) => float
softmax_cross_entropy_grad: (logits: Tensor, target: Tensor, dy: number) => Tensor
softmax_cross_entropy_grad: (logits: Tensor, target: Tensor | number[], dy: number) => Tensor
sgd: (params: P, grads: P, lr: float) => P
adam_state: (params: P) => OptimizerState
adam: (params: P, grads: P, state: OptimizerState, lr: float, beta1?: number, beta2?: number, eps?: number) => (P, OptimizerState)
save_params: (params: P, dir: string) => void
```

**`optim`** — import { ... } from optim — 10 name(s)

```text
constant: (rate: float) => (step: int) => float
linear_decay: (peak: float, total: int, floor: float) => (step: int) => float
cosine_decay: (peak: float, total: int, floor: float) => (step: int) => float
sgd: (schedule: (step: int) => float) => Sgd
adam: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.0) => Adam
adamw: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.01) => Adam
init: (opt: Sgd, params: P) => SgdState
init: (opt: Adam, params: P) => AdamState<P>
update: (opt: Sgd, params: P, grads: G, state: SgdState) => (P, SgdState)
update: (opt: Adam, params: P, grads: G, state: AdamState<P>) => (P, AdamState<P>)
```

**`autodiff`** — import { ... } from autodiff — 3 name(s)

```text
grad: (f: (x: float) => float) => (x: float) => float
with_grad: (f: (x: float) => float) => (x: float) => (float, float)
stop_gradient: (x: T) => T
```

**`sim`** — import { ... } from sim — 2 name(s)

```text
pi_hits: (samples: int, seed: int) => int
asian_call_price: (paths: int, steps: int, seed: int) => float
```

**`onnx`** — import { ... } from onnx — 2 name(s)

```text
export_model: (fn: (x: Tensor<f32>) => void, example: Tensor<f32>, path: string) => void
supported_operators: () => string[]
```
<!-- camel-for-ai end: stdlib-signatures -->

## Appendix C. Sources

| Section | Normative source |
|---|---|
| 2. The shape of a program | `docs/design-principles.cn.md` §§2–4, 11.3; `docs/language/04_execution_model.md` |
| 3. The smallest program | `docs/language/04_execution_model.md` §4.1; `docs/cli.cn.md` |
| 4. Lexical | `docs/language/02_lexical.md`; `include/camel/parse/syntax/token_kinds.def` |
| 5. Expressions and precedence | `docs/language/06_expressions.md`; `docs/design-principles.cn.md` §§4.2, 5.1, 5.4 |
| 6. Statements and declarations | `docs/language/07_statements.md`; `docs/language/03_data_model.md` |
| 7. Effects and `sync` | `docs/language/04_execution_model.md` §§4.2–4.6; `docs/design-principles.cn.md` §3.4 |
| 8. Streams (`target`) | `docs/design-principles.cn.md` §4.2; `docs/design/io-concurrency-streams/` |
| 9. Modules and the standard library | `docs/language/05_modules.md`; `docs/stdlib-reference.md` |
| 10. Passes and the command line | `docs/language/04_execution_model.md` §4.7; `docs/cli.cn.md` |
| 11. Idioms | `docs/language/07_statements.md`, `docs/language/06_expressions.md`, `docs/design-principles.cn.md` §11 |
| 12. Common errors | the registry `diagnostics/registry.tsv`, and the compiler through `node scripts/camel-for-ai.mjs --verify-errors` |
| 13. Accepted and then rejected | `docs/language/08_top_level_and_grammar.md` §8.6 |
| 14. Not implemented yet | the designs named in the section |
| Appendix A | generated from `diagnostics/registry.tsv` |
| Appendix B | generated from `docs/stdlib-reference.md` |

Generated by `scripts/camel-for-ai.mjs`; the four regions of this file are rewritten with
`node scripts/camel-for-ai.mjs --write` and checked with `--check`, which is also what the plan
`test/plans/feat/docs_ai_reference.plan.toml` runs.