# Nullable and Fallible Values

This note describes how the two built-in forms `T?` ("may be null") and `T!` ("may have
failed"), the operators `??`, `!!`, `?->`, the fixed `match` patterns and `throw` are
compiled. The normative text is `docs/design-principles.cn.md` section 2.9, "空值、错误与故障";
the user-facing description is in `docs/language/` (3.4, 6.6, 6.8.2, 7.3).

Choices the design principles do not make are collected in section 10 as questions for the
language author. Each is implemented the way section 10 recommends and marked in the code with
`DESIGN-QUESTION(nullable-error) Qn`, so that a ruling can be applied in one place.

## 1. What they are on the graph

| Construct | On the graph |
|---|---|
| `a ?? b`, `a !! b` | `BRCH` on the tag of `a`; arm 0 is `b`, arm 1 is the value taken out of `a`; `JOIN` |
| `x ?-> f` | `BRCH` on the tag of `x`; arm 0 hands the error on, arm 1 calls `f` with the value |
| `match x { case null => A  case v => B }` | the same `BRCH`; arm 0 is `A`, arm 1 binds `v` and is `B` |
| `match r { case error e => A  case v => B }` | the same `BRCH`; arm 0 binds `e` and is `A`, arm 1 binds `v` and is `B` |
| `throw e` | the exit of the graph, exactly where `return` puts it; the value is the failed form of the declared `T!` (`OPER`, `CAST`) |
| `v as T?`, `return v` in a function declared `T?`/`T!` | a `CAST` node |
| `try`/`catch` | nothing: a diagnostic |

Section numbers below: 2 the tag, 3 the nodes, 4 types, 5 `try`, 6 faults, 7 passes, 8
library, 9 a defect met in a pass, 10 questions.

A branch is the node pair `BRCH`/`JOIN` with one subgraph per arm, the structure `if` has.
Nothing in this table depends on what `a`, `b`, `f` or the arms contain, which is the
criterion of section 2.9. The right-hand side of `??` and `!!` is a subgraph of its own and
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
of `?->`), and `null -> T?` (the null slot; nothing is allocated).

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

### 3.2 `x ?-> f`

`x: T!`. Arm 1 is the call `f(__error_value__(x))`; if `f` returns a plain `U` the arm ends
in a `CAST` to `U!`. Arm 0 is a `CAST` of `x` to `U!` (the same object: an error box has no
payload), or `x` itself when `U` is `T`. The result is `U!`. A chain `x ?-> f ?-> g` is two
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
- `TaggedOperandMismatch` (0x65): the left side of `??`/`!!`/`?->` is not of the form the
  operator opens, or the right side of `??`/`!!` does not have the payload type. Located at
  the operator (`??`, `!!`) or at the start of the expression (`?->`).
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
- A value of type `T` does **not** flow into a `T?`/`T!` position by itself (Q1). It enters
  - where the program converts it: `v as T?`, `let x: T? = v`;
  - at `return v` in a function whose *declared* return type is `T?`/`T!` — the counterpart
    of `throw`: one exit statement gives the value form, the other the error form.
  An argument is not converted: `f(5)` with `f(x: int?)` is an error that suggests `5 as int?`.
- `null` is the literal of every `T?`, by the same three spellings: `let x: T? = null`,
  `null as T?`, `return null`. On the graph it is the constant `null` under a `CAST`.
- Branch arms unify as before (equal types). `if c then 1 else null` is an error; write the
  type on one arm.

### 4.3 Nesting and combination (Q2)

`T?!` and `T!?` are allowed and mean what they say: an operation that may fail and whose
result may be absent, and the reverse. Each operator and each `match` takes off exactly the
outer form. `T??` and `T!!` are rejected (`TaggedTypeNesting`, 0x62): the second mark adds no
information a program could use, and `??` could not tell the two nulls apart for the reader.
The same check applies to the signature of a generic function once its type variables are
replaced (`findRepeatedMark`), at the call; types written inside the body of an instance are
not checked again.

### 4.4 What a tagged value can be used for

Only `??`, `!!`, `?->` and the `match` patterns look inside. Member access, indexing and
destructuring of a tagged value are errors (`TaggedValueNotOpened`, 0x63), and no operator
overload accepts one where it expects a `T`. A tagged value may be stored, passed, returned,
captured and printed (`println` prints `null`, the value, or `error <kind>: <message>`) (Q5).

Not closed: the five operators of section 3 are ordinary global names (reserved identifiers:
a program cannot *declare* such a name, but it can call `__null_value__(x)`). Nothing in the
language needs that, and it should be refused where the name is resolved.

### 4.5 Generics

`substituteTypeVars`, `bindTypeVars`, `containsTypeVar` and `collectBindableTypeVars` descend
through both forms. `(typeas T)?` binds `T` from the payload of a nullable argument, so
`func or_zero(x: (typeas T)?, d: T): T { return x ?? d }` instantiates per payload type. A
plain argument does not bind it (no implicit injection, Q1). An instance is an ordinary
graph of concrete types; nothing new exists at run time.

## 5. `try` / `catch`

`try` is recognised by the parser only to explain itself: `TryCatchNotInLanguage` (0x64), at
the `try` keyword, pointing to `!!`, `?->` and fault isolation. `raise` and `T ? U` are
untouched (undecided, design principles section 6).

## 6. Errors and faults

Only an operation declared `T!` yields an error value. Division by zero, an index out of
range and deep recursion remain faults: nothing in this work turns a fault into an error
value, and `!!` does not catch one. Fault isolation (`attempt`) turns a fault
into an error value at the boundary of one call; it is described in `32_fault_isolation.md`.

## 7. Passes, inlining, exporters

Nothing in a pass, a backend or an exporter mentions the two forms. What they meet is

- `BRCH`/`JOIN` with two subgraph arms and a `bool` condition: an `if`;
- `OPER` nodes of pure built-in operators;
- `CAST` nodes whose target is a tuple type (`std::opt::fold` folds a `CAST` between
  primitive types only, so these stay in the graph);
- values whose type is a tuple type.

Verified (`test/plans/feat/compile/tagged.plan.toml`): one program over both forms gives the
same output on `std::nvm`, `std::fvm`, `std::jit`, `std::prl` and through `std::opt`,
`std::inline`, `std::opt::simplify`, `std::opt::fold`, `std::opt::cse std::opt::dce`,
`std::macro std::opt` and `std::inline std::opt`. No pass, backend or exporter was changed.

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
| `fs::try_read_text(path)` | `(string) => string!` | `IOError` |

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
    println(f(i as float))     // 7.5; under std::opt::simplify or std::opt: 0.5
    return 0
}
```

With a tagged form the unconverted constant is then read as a box, and the program crashes:

```camel
func main(): int sync {
    let c: int? = 7
    println(c ?? 1)            // 7; under std::opt::simplify or std::opt: segmentation fault
    return 0
}
```

`nullable.cml` of the test plan contains this shape, so it is run on the four backends and
through `std::inline`, not through `std::opt`.

## 10. Questions for the language author

Each is implemented as recommended; none changes the behaviour of an existing program.

- **Q1. Does a `T` flow into a `T?`/`T!` position implicitly?** Implemented: no, except at
  `return` under a declared `T?`/`T!`, by annotation and by `as`. Alternatives: (a) nowhere
  implicitly, so `return 5 as int!`; (b) everywhere a type is declared, including arguments
  and branch arms.
- **Q2. Nesting.** Implemented: `T?!` and `T!?` allowed, `T??` and `T!!` rejected.
  Alternatives: reject every combination; allow all.
- **Q3. What `throw` takes and how an error is made.** Implemented: `throw e` with `e:
  Error`; `error(kind, message)` makes one and records the position of that call. The field
  names are `kind`, `message`, `location`; `location` is the string `file:line:column`.
  Alternatives: `throw 'message'`; `throw kind, message`; a structured location.
- **Q4. `match` on a tagged value.** Implemented: exactly two cases, the tagged one and one
  rest case; literal cases are not mixed in (`case null`, `case 0`, `case v` together is an
  error). The spelling `case error e` makes `error` a word with a meaning only in this
  position; it is not reserved. Over any other type an identifier pattern stays an error
  (what it would bind there is undecided).
- **Q5. Printing and comparing.** Implemented: a tagged value can be printed; `==` is not
  defined for it. Alternative: forbid printing as well.
- **Q6. `?->` on a `T?`.** Implemented: an error; the principles define `?->` for errors
  only. The AST name of the construct (`NotNullThen`) suggests null propagation was once
  meant.
- **Q7. The right-hand side of `??`/`!!`.** Implemented: it must have type `T`; the result
  is `T`. Alternative: also accept `T?`/`T!` and give that type (a chain of fallbacks).
- **Q8. `let x: int! = v` written without a space** lexes `!=` as one token and is a syntax
  error; `let x: int! = v` with the space is accepted. Should the grammar accept the token,
  as it does `>=` after a type function's parameters?
- **Q9. Arms of different forms.** `if c then 1 else null` is rejected (arms must have equal
  types), and so is `if c then x else throw e` written without `return` on the first arm.
  Should a branch whose arms are `T` and `null` (or `T` and a failed value) have type `T?`
  (`T!`)? That would put a conversion the program did not write into an arm.
- **Q10. `null` given to a parameter.** `f(null)` with `f(x: int?)` is an error like `f(5)`;
  written `f(null as int?)`. Should a literal `null` be accepted where a `T?` parameter is?
- **Q11. A void expression under `as T?`.** `null` has the type `void`, so `println(1) as
  int?` is accepted and is null. Should only the literal be?
