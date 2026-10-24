# `check`'s instance space for a generic rule: which types, how many, and what cannot be drawn

Lane H401 (`handover/h401-check-generic`). The author's ruling of 2026-10-24
(`docs/handover/reviews/questions-2026-10-23.md`, section
`generic-rule-check-surface-and-multi-parameter-instances`) says `check` is to enumerate the
instances of a generic rule over "the types the operator group registered" that satisfy the
condition, and that a product too large is sampled by the seed, with the report saying which tuples
were taken and how many there are. Three things the ruling does not fix, and what this lane chose
until it is decided.

## 1. Which types are in the candidate set

The ruling asks for a core interface that lists the types an operator group registered
(`OperatorTable` enumerated by group). It does not say *which* group a rule's space comes from, and
a rule names no group: `int_combine::<T>` is not registered in any group; its pattern's operator is
whichever `+`, `-`, `*` the instance resolves to, and each of those is registered in a group of its
own.

**Chosen:** the union over every declared group. `OperatorGroup::publishResolvers` now declares the
group and its overload URIs to `OperatorTable` (`declareGroup`), and
`OperatorTable::allRegisteredTypes()` returns every type mentioned by the fixed signature of every
overload of every group (with-parameters, ordinary parameters and result), each once, in
registration order. `compile::gir::ruleInstanceTypes()` is that list.

Measured on the folded library: the union is `i32, int, f32, float, string, c64, c128, bf16, f16,
bool, Tensor<f32>, ...` plus whatever the other loaded modules register; the condition filter below
then keeps `{int, i32}` for `int_*` and `{float, f32, bf16, f16}` for `float_*`.

**Alternatives.**
- *(a) The union of the groups of the operators the pattern writes.* Costs a walk of the pattern
  graph per instance; a pattern with two operators from two groups then needs a rule for combining
  them. Gives the same answers for the standard library.
- *(b) `registeredWhenTypes()`* (`src/compile/gir/builder_helpers.h`, the list the sibling ruling of
  2026-10-24 on `impl-declaration-arms` already asks a condition about: int, i32, float, f32, bf16,
  f16, c64, c128, bool, string, void, one of each composite shape). Costs nothing, but it is a
  compiler-side list that no group wrote, it is not reachable from a module today (it lives under
  `src/`), and it would miss a type a group registered but the list does not name (`Tensor<f32>`).

## 2. The cap, and how a sampled tuple is chosen

The ruling fixes no number. **Chosen:** `kCheckTupleCap = 64` (`modules/rewrite/module.cpp`).
The tuples are built and filtered by the condition *first* -- asking a condition costs one
compile-time question, instantiating a tuple costs a body build -- and the survivors are sampled if
they are more than the cap. The sample is deterministic: `std::mt19937_64 rng(seed * 1000003u +
index)`, the same stream the entry's samples come from, so the same `check(rules, samples, seed)`
takes the same tuples on every run. The report says which and how many:

    sampled int_combine: 3 of 225 type tuples: (int, int) (i32, float) (f32, f32)

**Alternative:** cap the product *before* filtering, as a literal reading of "the product is too
large" would. With 20-odd registered types, an unfiltered `<T, U>` product is 400 and every
two-parameter rule would be sampled, including the ones whose condition admits four tuples; and a
rule would then sometimes not see a tuple the condition admits. Costs: fewer instances checked, a
sample that depends on the size of the registered set rather than on the law.

## 3. An instance whose values the check cannot draw

The check's sampler draws `int`, `i32`, `float`, `f32`, `bool` and tensors of them
(`Sampler::value`). A `when is_float::<T>()` law is also written for `bf16` and `f16` (design
principles 7.3: they are floating types with `f32`'s scalar arithmetic), and the pattern of every
reassociation law is built for them: `float_combine<bf16>`, `float_mul_one<f16>` and so on are real
instances of the law.

**Chosen (this lane):** those instances are reported, each with the reason, and not checked:

    unchecked float_combine<bf16>: the check cannot make a value of type 'bf16'

Making the sampler draw them is more than a sampler change. The two sides of a reassociating law
differ by last-place rounding *in the element type*: `bf16` has 8 significand bits, so the
difference is ~2^-8 relative, while `agreeFloat` compares with `f32`'s epsilon (1.2e-7) and the
permission's `steps + 1` ulps. `RewriteErrorBound`'s own doc comment says the bound is "in units of
**the element epsilon** and of the magnitude"
(`include/camel/core/operator_semantics.h`), so the reading that makes a `bf16` instance pass is
that the epsilon is the element type's (2^-8 for `bf16`, 2^-10 for `f16`, the same 2^-8 the
`change_precision` row already uses). That is a numerical-contract decision, not a sampler detail,
and it is not this lane's to make.

**Options.**
- *(a) Leave it as `unchecked` with the reason.* One line per un-drawable instance (22 of the 74
  instances of the folded library: 11 rules × bf16/f16). Costs: `check` says less than the law does,
  and the reader must know why the reduced-precision instances are singled out.
- *(b) Draw `bf16`/`f16` and compare with the element type's epsilon.* Needs: `Sampler::value` and
  `resultOf` to make and read a half (`rtdata::BFloat16`/`Float16`, 16 bits, `:op/*_bf` and `:op/*_f6`),
  `agree` to take the epsilon from the type instead of the `single` flag, `sampleText` to print one,
  and the accuracy path's reference for a law whose result type is a half (`float_log1p<bf16>` is
  not an instance -- `math:log1p` is registered for `f32`/`f64` only -- so the accuracy path stays
  on `float`/`f32`). Costs: one lane's worth of work, and it decides the epsilon question by doing
  it.
- *(c) Leave `bf16`/`f16` out of the candidate set for a scalar law.* Contradicts
  `is_float::<T>()`: the library's own comment (stdlib/rules.cml, the float section) says the family
  covers `bf16` and `f16` on purpose.

**Recommendation:** (b) as its own lane, with the epsilon read from the element type -- the field's
doc comment already says that is what it means. Until then (a) is what the report says, and the
reason is stated per instance rather than as one blanket "unchecked".

## Sources

- `docs/handover/reviews/questions-2026-10-23.md`, section
  `generic-rule-check-surface-and-multi-parameter-instances` (the ruling of 2026-10-24), and section
  `generic-rule-instantiation-set-and-predicate` (a failed instantiation is quiet).
- `docs/design-principles.cn.md` 7.3 (bf16/f16 are floating types; `change_precision` names 2^-8),
  4.4 (parameter defaults), 5.1 (literal types).
- `include/camel/core/operator_semantics.h` (`RewriteErrorBound`: "in units of the element epsilon").
- `stdlib/rules.cml` (the float section's comment on `is_float` covering `bf16`/`f16`).
- `src/compile/gir/builder_helpers.h` (`registeredWhenTypes`, `separateWhenConditions`).