# `check(..., program: Program? = null)`: the default the language cannot write

Lane H401 (`handover/h401-check-generic`). The ruling of 2026-10-24
(`docs/handover/reviews/questions-2026-10-23.md`, section
`generic-rule-check-surface-and-multi-parameter-instances`, §1) asks for

    export func check(rules: Rule[], samples: int = ..., seed: int = ..., program: Program? = null): CheckReport

with no program meaning "the types the operator group registered", and a program meaning that set
unioned with the program's own types. The no-program half is implemented and measured. The parameter
is not declared, because `null` cannot be the default of a parameter today.

## What was measured (2026-10-24, host, this branch)

A module with `func count(p: Program? = null): int`:

    error[SemanticDiag::ParamDefaultNotConstant]: The default of the parameter 'p' is not a
    compile-time constant: the value of 'null' is computed at run time.
    help: A default is evaluated where the declaration is, not at each call, so it can only be a
    constant expression.

The same error for `= current()`. The rest of the surface is there and was measured working:
`count(p: Program?)` with `match p { case null => ... }`, an explicit `count(null)`, and passing
`current()` (`Program` widens to `Program?`); defaults of `int`, `bool`, `string` and `f32` literals
all compile. `Program` is a handle type whose slot holds a `runtime::Program *`, and `NullSlot == 0`,
so a null of a nullable handle type is a value like any other -- what is missing is a compile-time
constant *of that type*.

## Why the default is rejected

`evaluateDefaults` (`src/compile/gir/builder_calls.cpp:796`) requires `constantOfNode(value)`. A
`null` written in a `T?` position goes through `visitCastNode`
(`src/compile/gir/builder_expr.cpp:969`, where `null`/`Void` deliberately skips) and reaches the
generic `addCastNode` (:1061) without `rememberConstant`, and `constantOfNode` sees through copy nodes
only, so the answer is "not constant". Remembering it would need a `Data` of type `Program?` holding
`NullSlot`: the only null data the compiler has is `NullData`, whose type is `Type::Void()` and whose
`convertTo` accepts `Void` alone, and `addStaticDataNode(value.data)`
(`src/compile/gir/builder_calls.cpp:1314`) would then put a `void`-typed node where the parameter's
type is `Program?`. So this is a gap in nullable defaults, not in `check`.

## Options

- *(a) Declare the parameter as two overloads*
  (`check(rules, samples, seed)` and `check(rules, samples, seed, program: Program)`). What the fold
  needs, and it needs no language change. Costs: two declarations of one native operator, whose
  signatures the module layer currently takes from one declaration ("the declarations are the one
  source of their signatures", `modules/rewrite/module.cpp`), and it contradicts the ruling's
  "parameter default" (design principles 4.4) -- a caller writing `check(rs, 200, 7, null)` would be
  refused where the ruling says it is the default.
- *(b) Make `null` a compile-time constant of a nullable type.* The honest fix: a `Data` whose type
  is the nullable type and whose slot is `NullSlot` (`NullData` generalized to carry the type it is
  a null of, or the cast node remembering the null it converted). Costs: it touches every
  `constantOfNode` consumer, and a nullable default then exists for every `T?`, not just `Program`
  (which is the point of a default, and is what a review of `addCastNode`'s `Null`/`Void` special
  case has to decide).
- *(c) A `Program?` default written as a named constant.* Nothing in the language makes a
  `Program?` value constant today either; it is (b) with extra steps.
- *(d) Leave it out.* What this branch does: `check(rules, samples, seed)` checks the registered
  types, the union-with-a-program half of §1 waits. Costs: a law whose instance exists only for a
  type some *program* uses and no group registered is not checked; `check` says nothing about it.

**Recommendation:** (b) as its own lane -- nullable defaults are a language surface, not a rewrite
one, and the ruling's §1 is only one of its callers. Until then (a) is the smallest way to get the
parameter, and it is not this branch's call to add a second declaration of a native operator.

## Sources

- `docs/handover/reviews/questions-2026-10-23.md`, section
  `generic-rule-check-surface-and-multi-parameter-instances` (the ruling of 2026-10-24).
- `docs/design-principles.cn.md` 4.4 (parameter defaults), 5.1 (literal types), 9.3 (how a
  parameter's declared type is read).
- `src/compile/gir/builder_calls.cpp` (`evaluateDefaults`, `addStaticDataNode`),
  `src/compile/gir/builder_expr.cpp` (`visitCastNode`), `src/compile/gir/builder.cpp`
  (`constantOfNode`, `rememberConstant`).
- Measured on the host with this branch's build, 2026-10-24: `Program` is a handle type
  (`HandleType`, `TypeFlag::GC_Traced`), `NullSlot == 0`, `isNullValue(slot) == (slot == 0)`,
  `nullablePayload(slot)`.