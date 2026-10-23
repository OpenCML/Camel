# A generic rule where it matches: the type the acceptance names, the set the engine may instantiate for, and what the families actually merge into

Author: the H375 agent (H232 step 1, generic rules), 2026-10-24. Status: open — the reading of the
acceptance test (§1), the candidate-type set (§2) and the extent of the merge (§6) are the
coordinator's; §3 is a defect this work must fix either way; §4 and §5 are not questions, they are
recorded so the step is not re-derived.

The ruling this work reads is `docs/handover/reviews/questions-2026-10-23.md`,
`generic-rule-instantiation`: option 2 — a generic rule is instantiated **where it matches**, from
the matched nodes' types, and cached per type tuple; `check` runs it once per *registered* type
satisfying `when`; `explain`/`why_not` report the types of the instance. Its acceptance:

> `stdlib/rules.cml`'s int/i32 families (13 + 13 rules today) and its float/float32/tensor-f32
> families (14 + 2 + 15) collapse into one generic law each, except where conditions or permissions
> differ; and **a type a test defines itself, satisfying `is_integer`, gets the standard library's
> `add_zero`**.

## 0. Where the work stands, measured

A generic law written the way the ruling says a law is written — `rule add_zero::<T>(x: T) { x + 0
=> x }`, used as `rewrite(p, [add_zero])` — is refused today. `test/cases/modules/rules/generic_law.cml`,
run as `camel target.cml generic_law::apply std::nvm`:

```
[Error]: Invalid pass specification 'generic_law::apply': its library 'generic_law' does not
compile. [SemanticDiag::GenericFunctionAsValue: The generic function 'add_zero__lhs' cannot be
given its types here: the operator '__rule' does not say which function it expects, and no instance
suggested by its other arguments is one it accepts; its type variable(s) T stay undetermined. ...]
```

The refusal is raised while binding the maker's arguments (`src/compile/gir/builder_generics.cpp:1259-1270`,
`rejectGenericValue` at `:275`). The declaration itself parses and locks its syntax
(`test/plans/feat/parse/rules.plan.toml`, `rules.generic_head`). So the red-before for the
acceptance is not a law that fails to fire: **the law cannot be written at all**, and the first half
of the ruling (the value carries its template) is what this step has to build.

## 1. The type the acceptance names cannot satisfy the standard library's `is_integer`

- `stdlib/types.cml:29-34` — the family predicate is identity over the spelled types:
  `is_integer::<T>()` is `match type T { case int | i32 => true; case _ => false }`.
- `src/compile/gir/builder_when.cpp:778-781, 1041-1043` and the scope switch at `:805-817` — a
  `when` predicate is resolved **where the declaration is written**, not where it is selected:
  `ConditionContext` installs the *declaring template's* scope, so `add_zero`'s
  `when is_integer::<T>()` calls the library's `is_integer`, never the importing program's.
- `docs/design-principles.en.md:294` — `type Name = another existing type` is an alias, compared by
  structure; only a named struct or an enum takes a new identity.

| the test's type | satisfies the library's `is_integer`? | does option 1 (`[add_zero::<int>]`) also cover it? |
| --- | --- | --- |
| an alias (`type MyInt = int`) | yes | yes — the node's type *is* `int` |
| a *refined* type the pattern reads (`type Vec = Tensor<f32, [3]>` vs the law's `Tensor<f32>`) | yes, for a tensor law | yes — measured below |
| its own identity (a named struct, an enum, a program's own type with its own `+`) | no | no |

The middle row is not hypothetical: the corpus already runs one. `test/cases/modules/rules/tensor_target.cml:10`
declares `type Vec = Tensor<f32, [3]>` and its docstring says the point — "`Vec` has a known extent, so
the rules (written for `Tensor<f32>`) are exercised through the type check: a less informed pattern
type reads a more informed node type" — and `rules.tensor_laws_counts` expects the library's
`tensor_f32_combine` to fold `x * 3.0 + x * 5.0` written on `Vec` (`combine 1 2` before, `combine 0 1`
after). So a type a test defines, standing to the law's pattern in the alias or refinement
relationship, **already gets the standard library's law today**, with the library written the way it
is now.

That leaves the last row as the only case that tells the two options apart, and under the current
resolution rule the acceptance test is not constructible in it and not a difference in the first two.
Readings:

- **(a) the test's type is an alias of `int`/`i32`.** Cost: nothing to build; the acceptance
  sentence is rewritten. Fallback if the sentence must stand at the lowest cost.
- **(b) a rule's `when` type predicates are resolved in the program that instantiates the rule.**
  The reading that makes the sentence mean what it says, and the only one under which "option 1
  could never meet it" is true: under option 1 the type argument is spelled where the rule set is
  written, so a type the *test* defines with *its own* `is_integer` cannot be covered without the
  law's own condition being read there. The program declares its own integer type and its own
  `is_integer`, and the *library's* law, instantiated there, asks that predicate. Cost: one
  deliberate exception to the lexical rule of the condition folding, stated in the design
  principles (a rule is not called where it is declared, it is instantiated where it matches), the
  instantiation entry passing the instantiating `ModuleUnit`, and a test. **Recommended** — it is
  the only reading under which the last row of the table exists, which is the sentence's point.
- **(c) a program can add its own type to a family** (a tag or a declaration bringing it into
  `is_integer`). Cost: a new language surface and its diagnostics, far past H232 step 1.

## 2. "Registered type" is not a set the runtime has

- the type table interns types but does not iterate them (`include/camel/core/type/intern.h`);
- the operator table registers per-type *operator instances*, not a type list;
- `check` today reads the parameter types off the rule's left function
  (`modules/rewrite/module.cpp:2131-2138`), which a generic rule has none of.

Options: **(i) the types of the nodes of the program being rewritten** — complete for the matching
path (a pattern's nodes are nodes of that program, so the tuple *is* the matched node's type),
includes a program's own types, one walk per engine run; **recommended for matching**. **(ii) the
concrete element types the pattern's operators are registered for**, read from `OperatorTable` — what
the word "registered" most directly names, stable across programs, gives `check` a set without a
program, misses a program's own type; **recommended for `check`, unioned with (i)**. **(iii)
iterating the interned type table** — no API, and the wrong set (it holds every composite type ever
made).

## 3. A failed instantiation must be quiet (a defect to fix either way)

`when` first removes most failures: a tuple the condition rejects is never instantiated. What is
left is a tuple the condition *admits* whose pattern has no operator, which the ruling calls
"unsolvable... → no match". Today that is not quiet: `Builder::instantiateGeneric`
(`src/compile/gir/builder_generics.cpp:1302`+) reports the body's diagnostics and throws
`BuildAbortException`, and `GenericInstance::failed` (`builder.h:77-78`) only makes *later* calls
with the same types fail quietly. No scope captures and drops diagnostics. The step needs one: an
RAII flag routing `diags_` to a scratch sink for the attempt, restoring on exit, and the tuple
recorded as *not made* in the cache, so `why_not` can say the type was tried and produced no law.

## 4. Where the front end is reached from, and the one mechanism that is missing (not a question)

The engine is `modules/rewrite` (runtime layer); the instantiation is compile-layer. The bridge
exists: `Builder::of(const context_ptr_t &)` (`src/compile/gir/program.cpp:186-195`) keeps the
program's Builder on the Context (`include/camel/core/context/context.h:145, 421`), and modules link
`libcamel` with an undefined-symbol export scan, so a public entry under `include/camel/compile/`
resolves from the module. A generic declaration already builds the three functions it needs
(`src/parse/ast/builder.cpp:832-1166` emits `name__lhs::<T>`, `name__rhs::<T>`, `name__when::<T>`);
`RuleObject` (`modules/rewrite/rule.h:47`) holds three `::Function*` today and would hold the three
templates plus the per-type instance cache.

The missing mechanism is how an instance's **graphs and condition function come to exist in the
program the pass is running on**. `compileRule` reads the sides out of the runtime program
(`rule.lhs()->program()`, `sourceOf` at `modules/rewrite/module.cpp:268-295`), and the pass runs
after the program is encoded, so an instance built by `instantiateGeneric` afterwards has no graph
the engine can read.

**Measured (2026-10-24), temporary probe in `engineRules` run through
`test/plans/feat/modules/rules.plan.toml`, one line per process:**

```
PROOF session=1 reserved=1 fresh_id=51 side=1 bound=1 table=52 version=1
```

* `session=1` — `Context::graphBuildSession()` is still set while a pass runs, so `Builder::of(ctx)`
  answers with the Builder of the program being compiled and does not make a second one.
* `reserved=1` — `Context::reserveProgram()` is usable from the engine at pass time.
* `fresh_id=51` — the context's `GraphIdAllocator` (`Context::graphIds()`, `context.h:119, 288`)
  still hands out ids after the program is built, and one allocator serves every program of the
  context.
* `bound=1`, `table=52`, `version=1` — `Program::bind(id, graph)` puts a graph the engine holds
  (the probe used a rule side's own graph) at that id, the table grows to hold it, and the program
  reads it back there.

So the first way is the one to build, and the encoder is not an obstacle: publication is the three
lines the runtime already uses at pass time for an operator's definition graph —
`context.reserveProgram()`, `encodeToRuntimeProgram(graph, *program, &context.graphIds())`,
`context.pinRuntimeProgram(program)` (`src/runtime/definitions.cpp:331-333, 410-412`) — and
`encodeToRuntimeProgram` is a public entry (`include/camel/compile/gir/encode.h:29`).

What is missing is smaller than it looked: `Builder`, `GenericTemplate` and `instantiateGenericAs`
live in `src/compile/gir/builder.h`, which no module can include — the public compile-side headers
are `include/camel/compile/gir/{graph,nodes,encode,types,value_entity,…}.h`. The bridge is therefore
one new public entry beside `encodeToRuntimeProgram` (the context, a template handle and the
expected `FunctionType` in, the instance's compile graph out, `why` when it does not fit), with the
handle carried by the `RuleObject` (the ruling's "the Rule value carries its template"). The engine
needs no lookup of its own for it: the public module API exposes an `Entity` only as an opaque
carrier (`include/camel/core/module/module.h:32, 61`), and the front end is the side that knows what
a rule's template is.

The second way stays an alternative for the *sides* only, and only if materialized patterns turn out
to be cheaper; the condition's function needs the first whatever happens.

## 5. What `when` is, for a generic rule

"Evaluated once per type tuple" is exact for the part of a condition about types
(`is_integer::<T>()` folds to a bool at instantiation). The rules of record also guard on *values*
(`int_combine`'s `c1`, `float_combine`'s `is_finite(c1) && sign(c1) == sign(c2)`), which is per
match. The reading this work takes: the instance carries the condition function for its types with
the type-level part folded, so the type-level verdict is computed once per tuple and cached, the
value-level part still runs per match (today's `callWhen`), and `why_not` reports the *type* verdict
and its reason from the front end (`WhenResult::detail`).

## 6. What the families merge into, measured from `stdlib/rules.cml`

69 rules. The shapes, as written:

| family | rules | merge | what the merge needs |
| --- | --- | --- | --- |
| `int_*` / `int32_*` | 13 + 13 | 13 laws, no `#requires` in either | the only difference is the type spelling — the clean case |
| `float_*` / `float32_*` | 12 + 12 | 12 laws | same: `float`/`f32`, with `#requires<reassociate>` / `<reassociate, no_infs[, no_signed_zeros]>` identical between the two |
| `tensor_f32_*` | 12 | 12 laws of their own, or the same 12 as the scalars | the tensor operands are elementwise operators and the coefficients are `const c: float` (f64, guarded by `exactIn32`), while the scalar law's operands *are* the element type — **measure first** whether one law instantiated at `T` and at `Tensor<T>` resolves both. A tensor operator is one id shared by the element types (`tensor:add`, and `tensor_target.cml:36-38`: "the operator id is shared, the element type is not"), so a tensor instance has to key on the *element* type the operator instance carries |
| `float_log1p` / `f32_log1p`, `float_expm1` / `f32_expm1` | 2 + 2 | 2 laws, `#requires<refine_accuracy>` | as the scalar family |
| `tensor_i64_distribute`, `tensor_log_softmax_of_softmax`, `tensor_sigmoid_chain` | 3 | stand alone | no family |

Two laws do **not** fold into one across the int and float families, and the ruling's exception is
what covers them: `int_add_zero` is `x + 0 => x` and `float_add_minus_zero` is `x + -0.0 => x` — the
zero the float law may drop is the *signed* zero, a different fact, so it stays its own law for the
floats (and for the tensors) and is not the int law.

So the collapse is 69 → **30 to 42** laws (13 int + 12 + (0 or 12) float/tensor + 2 logs + 2 expm1 +
3 standalone), depending on the tensor measurement above. Every renamed or dropped law has to be
re-pointed in `test/cases/opt/algebra_rules/pairs.mjs` and in the plan descriptions in the same
commit, with the reason.

The bar the merged law has to keep is already written down: `test/cases/modules/rules/typed_cross.cml`
is "the same law on four element types: a rule of one type must leave the others alone", and
`tensor_target.cml`'s `i64_self` is the tensor form of it. One law instantiated per type is what
keeps both, and it is the observable the `explain`/`why_not` reading of the cache should be measured
against (one instance per type tuple, not one per match).

## 7. What this work does next, on the readings above

(a) or (b) in §1; (i)+(ii) in §2; the speculative instantiation of §3 (done, commit `573502119`); the
bridge of §4 (measured, above); then, in order: the maker and the cache, the per-instance engine
rules, one merged law end to end with red-before/green-after and the cache read through
`explain`/`why_not`, then the rest of the families with `pairs.mjs` re-pointed. The full step is not a
single session's work: it touches the front end, the runtime boundary of §4, the module's engine loop,
`check`/`explain`/`why_not` and the library at once, and
the two decisions above are its prerequisites.

## 8. The maker, as the front end can build it (the slice after §4)

A rule declaration lowers to
`let name = __rule('name', twoWay, needs, consts, name__lhs, name__rhs)` with a third reference for
`__rule_when` (`src/parse/ast/builder.cpp:1101-1127`), and the two references are *functions*: under a
generic head they are generic functions, and giving one to `__rule` as a value is the refusal §0
measured.

A template is a compile-time object, so it cannot travel as a runtime value. The way across is an id:
the front end registers the rule's three templates (`name__lhs`, `name__rhs`, `name__when`) in a table
the context owns, emits `__rule_generic('name', twoWay, needs, consts, ruleId)`, and the `RuleObject`
keeps `ruleId`. The engine then has the templates through the table and the context it already holds
(`engineRules(Context &ctx, …)`, `modules/rewrite/module.cpp:301`), which is where the per-type-tuple
cache goes too.

The pieces, each with its own test:

1. **the public entry beside `encodeToRuntimeProgram`**: the context, a template handle and the
   concrete argument types in; the instance's compile graph out; `why` when the types do not fit or
   the body does not compile, with nobody told (§3's scope closed over it — a type tuple that
   resolves to no instance is not a program's error). Its front end side is a small function over
   `unifyTypeVars`/`instantiateFunctionType` (`src/compile/gir/builder_generics.cpp`), not a second
   unification.
2. **the registration table and `__rule_generic`** (the context's table, the module's kernel,
   `stdlib/rewrite.cml`'s declarations) and the front-end lowering for a generic head. The only case
   with a generic head today is `test/cases/parse/rules/generic_head.cml`, and its test
   (`rules.generic_head`, `test/plans/feat/parse/rules.plan.toml:19-27`) only reads the declaration's
   AST (`${inspect_ast}`) without compiling it: it does not lock today's refusal and needs no
   re-pointing. **No plan asserts the refusal** — it was measured with a temporary case (§0) — so the
   new behaviour wants its own test rather than a re-pointed one.
   A rule with no instance for a type tuple does not fire, so a rule value whose templates are
   registered and never instantiated is not an error; it is a rule that matches nowhere. Until step 3
   lands, `apply` of such a rule does nothing and says so.
3. **the per-type cache** in `RuleObject` (`modules/rewrite/rule.h:47`), keyed by the type tuple the
   match solves; one `compileRule` per instance; `explain`/`why_not` (and `check`) report the
   instance's types, which is the observable for "one instantiation per type tuple".
4. **one law end to end**: `int_add_zero`/`int32_add_zero` and `float_add_minus_zero`/`f32_add_minus_zero`
   first (the float zero law stays its own), red before and green after on every element type, with
   `test/cases/modules/rules/typed_cross.cml` green; then the rest of §6's families, with
   `test/cases/opt/algebra_rules/pairs.mjs` and the plan descriptions re-pointed in the same commit.