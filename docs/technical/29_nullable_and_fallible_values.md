# Nullable and Fallible Values

This note describes how the two built-in forms `T?` ("may be null") and `T!` ("may have
failed"), the operators `??`, `!!`, `->`, the fixed `match` patterns and `throw` are
compiled. The normative text is `docs/design-principles.cn.md` sections 3.8, "可空", 3.9, "错误与故障",
and 3.7, "分支与 `match`";
the user-facing description is in `docs/language/` (3.4, 6.6, 6.8.2, 7.3).

Choices the design principles do not make are collected in section 10 as questions for the
language author. Each is implemented the way section 10 recommends and marked in the code with
`DESIGN-QUESTION(nullable-error) Qn`, so that a rule can be applied in one place.

## 1. What they are on the graph

| Construct | On the graph |
|---|---|
| `a ?? b`, `a !! b` | `BRCH` on the tag of `a`; arm 0 is `b`, arm 1 is the value taken out of `a`; `JOIN` |
| `x -> f`, `x: T?` or `T!` that no overload of `f` takes whole | `BRCH` on the tag of `x`; arm 0 hands the null or the error on, arm 1 calls `f` with the value |
| `match x { case null => A  case v => B }` | the same `BRCH`; arm 0 is `A`, arm 1 binds `v` and is `B` |
| `match r { case error e => A  case v => B }` | the same `BRCH`; arm 0 binds `e` and is `A`, arm 1 binds `v` and is `B` |
| `throw e` | the exit of the graph, exactly where `return` puts it; the value is the failed form of the declared `T!` (`OPER`, `CAST`) |
| `v as T?`, and a value that enters a `T?`/`T!` position by itself (an argument, an annotated `let`, an array element, a branch arm, `return`) | a `CAST` node per level |
| `try`/`catch` | nothing: a diagnostic |

Section numbers below: 2 the tag, 3 the nodes, 4 types, 5 `try`, 6 faults, 7 passes, 8
library, 9 a defect met in a pass, 10 questions.

A branch is the node pair `BRCH`/`JOIN` with one arm region per arm, the structure `if` has.
Nothing in this table depends on what `a`, `b`, `f` or the arms contain, which is the
criterion of section 1. The right-hand side of `??` and `!!` is an arm region of its own and
is entered only when the branch selects it: that is the whole of "evaluated only when needed".

## 2. Where the one-bit tag lives

### 2.1 Constraints

- A value is a `slot_t` (64 bits) and carries no type. Every run-time operation that needs a
  type (printing, tracing, cloning) is handed the static type of the slot.
- Integers and floats use all 64 bits: a scalar slot has no spare bit.
- No run-time type information may be introduced, and a program that does not use the two
  forms must not pay for them.

### 2.2 Alternatives

| | Second slot beside the value | Null pointer for references, something else for scalars | **Box (chosen)** |
|---|---|---|---|
| Representation | every `T?`/`T!` value is two slots | `string?` is a pointer that may be null; `int?` needs another scheme | one slot: a pointer to a tuple object, or null |
| The four backends | each must learn two-slot values: frame layout, calls, returns, `JOIN`, closures, array elements | each must know two layouts and choose by type | none changes: the value is a tuple |
| GC root scanning | frame maps must describe pairs | unchanged | unchanged: a traced slot whose type is a tuple type |
| JIT | new calling convention | unchanged for references | unchanged |
| `std::cpp`, ONNX, autodiff | each must model pairs | each must model two layouts | each sees what it sees for a tuple today |
| Nesting (`T?!`) | four slots | ambiguous for references (`string??`) | works by construction |
| Cost | none on the heap | none for references | one small allocation per value that is put *into* the form |

### 2.3 The choice

`T?` and `T!` are **tuple types with a form mark**, exactly as the variable cell `var T`
already is (`TupleType::cell`): `TupleType::nullable(T)` and `TupleType::fallible(T)`
(`include/camel/core/type/composite/tuple.h`). They have `TypeCode::Tuple`, so every part of
the system that stores, traces, copies, compares or prints a value by its static type treats
them as the tuples they are, and they are distinct *types*: `int?` equals neither `int` nor
`(int)`, and no tuple operation of the language accepts them (section 4.4).

| Type | Layout of the object | Tag |
|---|---|---|
| `T?` | null slot when null; otherwise a one-slot tuple `(value: T)` | the slot is the null slot |
| `T!` | a two-slot tuple `(value: T, error: Error)` | the `error` slot is not the null slot |

`Error` is the fixed built-in structure `{ kind: string, message: string, location: string }`
(`type::errorType()`), an ordinary struct type.

Why this is acceptable under the constraints:

- **No run-time type information.** Whether a slot is a `T?` is known from its static type
  only; the object is an ordinary `Tuple`. Reading the tag is a null test.
- **No cost for programs that do not use the forms.** No slot is widened, no frame layout
  changes, no node kind is added. The only shared code that changed is `Tuple::print` and
  `printSlot` (one branch on the static type) and the type functions of `TupleType`.
- **Backends, GC, JIT, exporters and autodiff need no change**, which was the stated goal.
  `null` is the null slot, which every traced slot may already hold.

The cost is one allocation when a value enters the form (`CAST`), and one indirection when it
is taken out. `null` itself allocates nothing. A later optimisation may leave reference
payloads unboxed in `T?`; it would change the two operators of section 3 and nothing on the
graph.

## 3. The nodes

Three things happen to a value of these forms, and each is an existing node kind.

**Reading the tag and the parts: `OPER`.** Five built-in operators
(`src/builtin/operators/tagged.cpp`, group names are reserved identifiers):

| Operator | Type | Meaning |
|---|---|---|
| `__is_null__` | `(T?) => bool` | the tag of a `T?` |
| `__null_value__` | `(T?) => T` | the value of a `T?` that is not null |
| `__is_error__` | `(T!) => bool` | the tag of a `T!` |
| `__error_value__` | `(T!) => T` | the value of a `T!` that has not failed |
| `__error_info__` | `(T!) => Error` | the error of a `T!` that has failed |

All five are declared pure. `__null_value__`, `__error_value__` and `__error_info__` are only
ever placed inside the arm their tag selects. Applied to the other case they do not fault:
they return the zero slot of their type, so a pass that moves one out of its arm (hoisting)
does not change behaviour as long as the result is used only inside the arm.

**Putting a value into the form: `CAST`.** `TupleType::castSlotFrom` allocates the box.
Allowed conversions (`castSafetyFrom`): `T -> T?`, `T -> T!`, `U! -> T!` for the failed case (compiler-made only, the error arm
of `->`), and `null -> T?` (the null slot; nothing is allocated).

**Choosing: `BRCH`/`JOIN`.**

### 3.1 `a ?? b` (and `a !! b` with `__is_error__` / `__error_value__`)

```
main:
  a      : int?                       (whatever produces a)
  OPER   __is_null__   : bool   <- a
  BRCH   : int64                <- [norm] OPER __is_null__
  FUNC   arm0  ( ) => int             (body: b)                 <- [ctrl] BRCH
  FUNC   arm1  ( ) => int             (body: __null_value__(a)) <- [ctrl] BRCH
  JOIN   : int                  <- [with] arm0, arm1; [norm] BRCH
```

`a` is evaluated once, in the enclosing graph; arm 1 captures it. The result type is `T`.

When `b` is itself a `T?` (a `T!` for `!!`) the result is that type (design principles
3.8): arm 1 then hands `a` on as it is instead of taking the value out. It is still one
branch, so `a ?? b ?? 9` is two branches in sequence whichever way it is grouped.

### 3.2 `x -> f`

`x: T?` or `T!`, and no overload of `f` (its `when` conditions included) takes `x` whole:
a pipe whose receiver is not tagged, or one that an overload takes as it is, builds the call
and nothing else (design principles 3.9). Which of the two it is, is decided by building the
call with `x` first, in an arm that is dropped afterwards (`Builder::visitPipeLink`), so that
the other arguments of the call are built, and run, once.

Otherwise (`Builder::peelPipeReceiver`) the branch is on the tag of `x`. Arm 1 is the call
`f(value-of(x))`; if `f` returns a plain `U` the arm ends in a `CAST` to `U?` (`U!`), and a
`U` that already has that form is kept. Arm 0 hands the null on (a null cast to the result
type) or the error (`x` cast to `U!`: an error box has no payload). The result is `U?` or
`U!`. A value tagged twice (`T?!`) is taken apart one tag at a time: the call in arm 1 sees
the value inside as its receiver and may be taken apart again. A chain `x -> f -> g` is two
such branches in sequence. Arm 1 is built first, because it is what determines `U`; on the
graph it is still arm 1.

### 3.3 `match`

`match x { ... }` with `x: T?` takes exactly the cases `null` and one "rest" case (`case v`
or `case _`); with `x: T!` exactly `error e` and one rest case. On the graph arm 0 is always
the tagged case, whichever is written first (`case v` may come before `case null`; a `case _`
written first makes what follows unreachable, as in every `match`). A case that binds a name
starts with the operator that takes the value out, inside the arm.

A scrutinee of any other type keeps the existing meaning of `match` (compare with literals);
there `case null`, `case error e` and a binding case are errors.

### 3.4 `throw e`

`throw e` is `return` of the failed form:

```
g:
  OPER :op/error_new   : Error   <- "K", "m", "file.cml:9:11"
  OPER :op/tag_throw   : void!   <- OPER :op/error_new
  CAST                 : int!    <- OPER :op/tag_throw
  EXIT                           <- CAST
```

`T!` is the declared return type of the function the statement belongs to (looking out of
branch arms; a lambda is a function of its own). `__throw__` makes the failed value, the
`CAST` gives it the declared type; both are the same object. It is not a jump: like `return`,
it gives the value of the body (or of the branch arm) it stands in.

`error(kind, message)` builds an `Error`; the compiler appends the source position of the
call (of the word `error`) as a third, constant argument (`file:line:column`, the file name
without its directory). Rethrowing a caught error (`case error e => throw e`) keeps its
original position.

Diagnostics:

- `ThrowOutsideFallible` (0x60): `throw` in a function whose declared return type is not
  `T!` (or is not declared), or at module level.
- `ThrowNotAnError` (0x61): the operand of `throw` is not an `Error`.
- `TaggedOperandMismatch` (0x65): the left side of `??`/`!!` is not of the form the
  operator opens, or the right side of `??`/`!!` does not have the payload type. Located at
  the operator.
- `TaggedMatchCases` (0x66): a `match` on a tagged value without exactly the two cases.

## 4. The type system

### 4.1 Representation

`TupleType::form()` is `Plain`, `Cell`, `Nullable` or `Fallible`; `isTagged()` is true for
the last two; `payloadType()` is `T`. Instances are interned per payload like cells. `equals`
compares the form, so the three tuple-shaped types of one layout are different types.
`mangle` gives `N<T>` and `E<T>`; `toString` gives `T?` and `T!` (with parentheses around a
function or union payload).

### 4.2 Assignment and unification

- `T?` is assignable from `T?` only, and `T!` from `T!` only (payloads equal).
- A value of type `T` enters a `T?`/`T!` position by itself wherever the target type is
  known (design principles 3.8); never the other way. The rule is two functions of the type
  library (`include/camel/core/type/composite/tuple.h`):
  - `entersImplicitly(target, source)`: `source` is assignable; or `target` is tagged and
    `source` enters its payload (so several levels are entered at once: `int` into `int?!`);
    or `source` is `void` (the type of `null`) and `target` is a `T?`; or `source` is the type
    of the empty literal `[]` and `target` is an array type. A tagged value of the same form
    with another payload does not enter (`int?` is not put into `float?`).
  - `joinTypes(a, b)`: the type two arms or two elements have in common: what they unify
    to, else the one the other enters, else (one of them being `null`) the other, nullable.
- On the graph every level is a `CAST` node, the one `v as T?` gives; the GIR builder adds it
  (`Builder::coerceTo`). The positions:

  | Position | Where |
  |---|---|
  | argument of a function, a function value, a decorated or a generic function | `visitLinkNode`: the callee is first tried with the argument types as they are, so an exact overload wins, then with each entering argument counted as its parameter's type (`enteringTypes`); the CASTs are added once the callee is known (`enterParameters`) |
  | annotated `let`, `as` | a GCT `CAST` as before; `visitCastNode` adds the levels a single conversion does not cover |
  | array literal with a known element type (`let xs: (int?)[] = [1, null]`, `[...] as (int?)[][]`) | GCT `visitDataInto`: every element is the value under a `CAST` to the element type, recursively for nested literals |
  | array literal without one, with a `null` among its elements (`[1, null, 3]`, `[x, null]`) | GCT `visitArrayData` leaves every element computed; `visitDataNode` joins the element types and puts the elements that are not of the joined type into it |
  | branch arms (`if`, `match`) | `buildBranch`: the join type is `joinTypes` of the arms; an arm that is not of it gets the CAST at its exit (`coerceArmExit`), added to the arm's own graph |
  | `return v` under a declared `T?`/`T!` | `adaptExitValue` |

- An argument of a built-in operator is not such a position: an operator's parameter types
  are computed from the argument types by its C++ resolver, so there is no target type before
  the resolver has accepted the arguments.
- `null` is the literal of every `T?`. On the graph it is the constant `null` under a `CAST`.
  Its type is `void`, and a `void` expression that is not the literal is treated the same
  (Q11).
- The empty literal `[]` (type `void[]`) is given the element type of its position by the
  same function: a static array of the target type takes its place. A name bound to a
  `void[]` without a type is `EmptyArrayWithoutType`.

### 4.3 Nesting and combination (Q2)

`T?!` (fallible(nullable(T))) and `T!?` (nullable(fallible(T))) are allowed and mean what they
say: an operation that may fail and whose result may be absent, and the reverse. Each operator
and each `match` takes off exactly the outer form. `T??` and `T!!` are rejected (`TaggedTypeNesting`, 0x62): the second mark adds no
information a program could use, and `??` could not tell the two nulls apart for the reader.
The same check applies to the signature of a generic function once its type variables are
replaced (`findRepeatedMark`), at the call; types written inside the body of an instance are
not checked again.

### 4.4 What a tagged value can be used for

Only `??`, `!!`, `->` and the `match` patterns look inside. Member access, indexing and
destructuring of a tagged value are errors (`TaggedValueNotOpened`, 0x63), and no operator
overload accepts one where it expects a `T`. A tagged value may be stored, passed, returned,
captured and printed (`println` prints `null`, the value, or `error <kind>: <message>`) (Q5).

Not closed: the five operators of section 3 are ordinary global names (reserved identifiers:
a program cannot *declare* such a name, but it can call `__null_value__(x)`). Nothing in the
language needs that, and it should be refused where the name is resolved.

### 4.5 Generics

`substituteTypeVars`, `bindTypeVars`, `containsTypeVar` and `collectBindableTypeVars` descend
through both forms. A `T?` parameter binds `T` from the payload of a nullable argument when `T` is declared at the function's head, so
`func or_zero::<T>(x: T?, d: T): T { return x ?? d }` instantiates per payload type. A
plain argument enters the parameter and binds the variable of the payload (`or_zero(5, 0)`
gives `T = int`). An instance is an ordinary graph of concrete types; nothing new exists at
run time.

## 5. `try` / `catch`

`try`, `catch` and `finally` are not keywords: they are ordinary names, and a `try { ... }` written
with them is an ordinary syntax error. (Until the syntax closure of 2026-10-25 the parser read the
form only to explain itself, with a diagnostic of its own; that code is empty now.) `raise` is
untouched (undecided, design principles section 15).

## 6. Errors and faults

Only an operation declared `T!` yields an error value. Division by zero, an index out of
range and deep recursion remain faults: nothing in this work turns a fault into an error
value, and `!!` does not catch one. Fault isolation (`attempt`) turns a fault
into an error value at the boundary of one call; it is described in `33_fault_isolation.md`.

## 7. Passes, inlining, exporters

Nothing in a pass, a backend or an exporter mentions the two forms. What they meet is

- `BRCH`/`JOIN` with two subgraph arms and a `bool` condition: an `if`;
- `OPER` nodes of pure built-in operators;
- `CAST` nodes whose target is a tuple type (`std::opt::fold` folds a `CAST` between
  primitive types only, so these stay in the graph);
- values whose type is a tuple type.

Verified (`test/plans/feat/compile/tagged.plan.toml`): one program over both forms gives the
same output on `std::nvm`, `std::fvm`, `std::jit`, `std::prl` and through `std::opt`,
`std::opt::inline`, `std::opt`, `std::opt::fold`, `std::opt::cse std::opt::dce`,
`std::macro std::opt` and `std::opt::inline std::opt`. No pass, backend or exporter was changed.

Not verified: `std::cpp`, `onnx::export` and autodiff over a graph that holds these nodes
(they are expected to refuse it the way they refuse tuples and unknown operators), and a
collection that runs while a box is the only reference to its payload beyond what the
ordinary test programs do.

## 8. Standard library

Three operators that can fail, added without changing an existing signature:

| Operator | Type | Fails with |
|---|---|---|
| `parse_int(s)` | `(string) => int!` | `ParseError` |
| `parse_float(s)` | `(string) => float!` | `ParseError` |
| `fs::read_text(path)` | `(string) => string!` | `IOError` |

An operator written in C++ builds its result with `rtdata::makeOk` / `rtdata::makeError`
(`include/camel/core/rtdata/tagged.h`); the position of an error raised by an operator is the
operator's name (`<parse_int>`), as an operator does not know its call site.

## 9. A defect met in a pass

Not caused by this work, and not fixed (the optimisation passes are being repaired on other
lines). `tryResolveStaticValue` (`src/runtime/draft_inline.cpp`) looks through a `CAST` node
to the constant under it and takes that constant, unconverted, as the value of the argument
when a callee is specialised on a constant argument. Any conversion of a constant is
affected, with or without the tagged forms:

```camel
func f(x: float): float { return x + 0.5 }
func main(): int sync {
    let i = 7
    println(f(float(i)))     // 7.5; under std::opt or std::opt: 0.5
    return 0
}
```

With a tagged form the unconverted constant is then read as a box, and the program crashes:

```camel
func main(): int sync {
    let c: int? = 7
    println(c ?? 1)            // 7; under std::opt or std::opt: segmentation fault
    return 0
}
```

`nullable.cml` of the test plan contains this shape, so it is run on the four backends and
through `std::opt::inline`, not through `std::opt`.

## 10. Questions for the language author

Each is implemented as recommended; none changes the behaviour of an existing program.

- **Q1. Does a `T` flow into a `T?`/`T!` position implicitly?** Ruled on (design principles
  3.8): yes, wherever the target type is known; never the other way. Section 4.2.
- **Q2. Nesting.** Ruled on (3.9): `T?!` and `T!?` allowed, `T??` and `T!!` rejected.
- **Q3. What `throw` takes and how an error is made.** Ruled on (3.9): `throw e` with `e`
  an error value made by `error(kind, message)`; the compiler fills in the position.
- **Q4. `match` on a tagged value.** Implemented: exactly two cases, the tagged one and one
  rest case; literal cases are not mixed in (`case null`, `case 0`, `case v` together is an
  error). The spelling `case error e` makes `error` a word with a meaning only in this
  position; it is not reserved. Over any other type an identifier pattern stays an error
  (what it would bind there is undecided).
- **Q5. Printing and comparing.** Implemented: a tagged value can be printed; `==` is not
  defined for it. Alternative: forbid printing as well.
- **Q6. `->` on a `T?`.** Ruled on (3.9): `->` carries a null along as it carries an error,
  and `?->` is gone. Section 3.2.
- **Q7. The right-hand side of `??`/`!!`.** Ruled on (3.8): a `T` gives a `T`, a `T?`
  (`T!`) gives that type. Section 3.1.
- **Q8. `let x: int! = v` written without a space** lexes `!=` as one token and is a syntax
  error; `let x: int! = v` with the space is accepted. Should the grammar accept the token,
  as it does `>=` after a type function's parameters?
- **Q9. Arms of different forms.** Ruled on (3.8): the arms join, `if c then 1 else null`
  is an `int?`.
- **Q10. `null` given to a parameter.** Ruled on (3.8): `f(null)` with `f(x: int?)`.
- **Q11. A void expression where a `T?` is expected.** `null` has the type `void`, so any
  `void` expression is accepted where `null` is. For a branch arm this is ruled on (3.7):
  an arm without a value and an arm of type `T` make the branch a `T?`
  (`if c then 1 else f()` with a `f` that returns nothing is an `int?`). Under `as T?` and
  as an argument the same holds in the implementation and has not been ruled on.
