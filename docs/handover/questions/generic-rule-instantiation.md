# Instantiating a generic rule at the use site

Author: the H232 agent (rule language v2, step 1), 2026-10-24. Status: open, needs the coordinator
(or the language author).

## Problem

Step 1 of the rule-language v2 design asks for a rule whose element type is a parameter:

```
rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }
```

The syntax parses, `is_integer::<T>()` landed (H234), and the *value* semantics of a `const`
parameter landed (H235 and the review follow-ups). What is missing is a way to get a **`Rule`
value** out of a generic rule. A rule declaration is compiled by the engine from its own graph
(`src/runtime/rules.cpp`), while generic instantiation is a compile-time step of the front end
(`GenericTemplate`, `instantiateGeneric`, `visitTypeApplication`): `add_zero::<int>` type-checks
today and yields nothing, and design 8.5's "the engine instantiates at the match site" cannot be
implemented in this run -- the engine has no runtime monomorphization, and the matcher has no type
to bind `T` from.

## What is not decided

1. **Front end**: a type application on a rule name (`add_zero::<int>`) instantiates the generic
   rule's graph at the use site and compiles a `Rule` value from it, in the same place the function
   and operator path already instantiates (a hook in `visitTypeApplication`). Rule sets and
   `run`/`saturate` calls then take `[add_zero::<int>, ...]`.
2. **Engine**: a rule value carries a template and its pattern is monomorphized per match, which is
   a new runtime concept (a rule with a type variable) and would put instantiation back at the match
   site that design 8.5 describes.
3. **Neither**: the library keeps one rule per element type, and the acceptance sentence of H232
   ("no `int_add_zero`/`int32_add_zero` triple") is read as the operand-order, commutation and
   association duplication only.

## Why it is a question and not a change here

Whichever of the three is chosen constrains the front end, the rule value's runtime shape, and how
`check`, `explain` and the scheduling rule sets are written; the choice is not the rule matcher's,
and it decides what the *library* looks like, which is the template for how rules are written. The
deletions this branch already made are all *within* one element type (per operand order, `*_comm`,
`*_assoc`, and the `*_one_combine` twins), so they need no instantiation and stand on their own.

## What blocks on it

`stdlib/rules.cml` still holds the `int` and `i32` half of every law twice (`int_add_zero` and
`int32_add_zero`, `int_combine` and `int32_combine`, ...), and the float / f32 / tensor families
once per element type. Collapsing those needs option 1 or 2 above.

## Pointers

`include/camel/parse/syntax/node_kinds.def` (`RuleDecl`), `src/parse/ast/builder.cpp` (where a rule
declaration becomes its value), `src/runtime/rules.cpp` (compiling a rule's graph into a `Rule`),
`src/compile/*` (`visitTypeApplication`, `instantiateGeneric`), and
`docs/design/rewrite/v2.cn.md` 1 and 8.5.