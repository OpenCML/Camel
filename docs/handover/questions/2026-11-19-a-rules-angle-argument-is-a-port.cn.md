# A rule's `<>` argument is one of its operator's ports, and the value can be bound

> **答复在最后一节。** 本页前四节保留提问当时的原样。

**Question for the language author.** Found while making the character transformer differentiable
(`stdlib/vjp.cml:135`, the paper's blocked row). It is a question because the sentence it corrects
lives in a plan entry this lane may not edit, and because one of the two repairs is blocked by a case
that pins the wall it repairs.

## What the tree said

`test/plans/feat/compile/rules_angle_args.plan.toml`, header:

```
A rule names its data in parentheses only, so the two operators that state it in `<>` cannot be
written as rules that carry it:
  * the builder asks a `with` argument to be a compile-time value
    (`src/compile/gir/builder_generics.cpp#Builder::ruleWithArgumentValue`), and a value position's
    `<...>` is a context argument and nothing else (design principles 5.6) -- so the shape and the
    axis can only be *stated* by a rule, never bound from the node it matches;
```

and `stdlib/rulesets/jvp.cml`, above `jvp_log_softmax`:

```
/// carries; the axis lives in a `<>` argument, which is the part `softmax` is refused for.
```

So the tree said: `reshape<shape>` and `softmax<axis>` can only be stated, one constant per law.
`jvp_reshape` was therefore `reshape<[2, 2]>(x) => (dx) => reshape<[2, 2]>(dx)` and `jvp_softmax`
was `softmax<1>` of everything.

## What was measured

**The constants are not matched.** A law whose left side states `softmax<1>` matches a call written
`softmax<2>`, and answers the `1` law to it. `f(x) = sum(softmax<2>(x) * w)` on `x = 0` along
`dx = 1..12` (by hand: `sum(w)/3 = 26` and `4 * 2/3 = 2.666667`):

```
softmax axis 2 value=26.000000 tangent=13.500000     <- the law in the tree, which says `softmax<1>`
softmax axis 2 value=26.000000 tangent=2.666667      <- the same law with the axis bound
```

A wrong tangent read quietly is worse than a missing one, and nothing in the tree reported it. The
same shape of law is `jvp_log_softmax` and `log_softmax_t`, both at `<0>`.

**Naming the port works.** Three laws were written the way `vmap_rule_reshape` and
`vmap_rule_permute` already write theirs -- the `<>` port named as the rule's last parameter, the
value handed to a function:

```camel
export rule jvp_reshape::<T>(x: T[...], dims: int[]) when is_float::<T>() {
    reshape<dims>(x) => (dx: T[...]) => reshape_jvp(dx, dims)      // reshape<dims>(dx) in there
}
```

The reading that the constant cost, `showcase/c3-char-transformer/probes/06_layout.cml`, before and
after:

```
error[RuntimeDiag::TensorDimensionMismatch]: Array shape mismatch in 'reshape': reshape changes
tensor element count: [6] -> [2, 2]
```
```
value = 91.000000
tangent = 42.000000
```

(`sum(transpose(reshape([1..6], [3, 2]))^2) = 91`, and `2 * 21 = 42` along a direction of ones.)

`permute` had no law in either family, and its adjoint is the **inverse** permutation rather than the
list written back (the same list only when the list is its own inverse). On a permutation that is not
(`[2, 0, 1]` over a `[2, 2, 3]`, so a law answering `perm` again would still look right):

```
forward value=584.0 tangent=78.0
reverse value=584.0
cotangent=([[[1.0, 5.0, 9.0], [2.0, 6.0, 10.0]], [[3.0, 7.0, 11.0], [4.0, 8.0, 12.0]]])
```

and the two readings the plan entry pins are unchanged by all of it
(`rules_angle_args reshape value=30.0 tangent=5.0`, `rules_angle_args softmax value=1.5
tangent=-0.25`).

One refusal is real and stays: a `<>` name written **in the rule's own right side** --
`softmax<axis>(x) => (dx) => softmax<axis>(x) * (...)` -- is refused with

```
jvp has no forward rule for `tensor:softmax` (the rule's right side uses a node written after it)
```

so the value goes through a function body (`softmax_axis_jvp`), which is where a `<>` argument
stated at run time already lives (`vmap_permute`, `reshape<batched_shape(xs, dims)>`). And that
function is a callee with an open-rank parameter, which is what the transposed walk refuses:

```
TRANSPOSE function=rulesets.jvp::softmax_axis_jvp<float> skip=parameter-shape uri=tensor:multiply
```

## Why it matters

`reshape` and `permute` landed (`6df52b106`); `softmax`'s repair is written, measured and **reverted**,
because `test/cases/compile/vjp/softmax_gradient_stops_at_split.cml` pins today's sentence
(`split does not build this function`, `skip=backward construct=a node whose input was not copied`)
and the named form makes the split *succeed* and fail one step later at the transpose. Moving a wall
is not supposed to move a case under it, and this lane may not edit expectations, so the wrong-axis
law stays and the case stays green.

The transformer therefore stops where it stopped, one operator further on than `permute`:

```
REINTERP jvp function=block skip=rule uri=tensor:layer_norm reason=the operator has no forward rule bound at all
error[RuntimeDiag::RuntimeError]: ... transforms: jvp has no forward rule for `tensor:layer_norm`
   --> showcase/c3-char-transformer/train.cml:178
```

`layer_norm` has no forward law at all -- `scripts/vjp-parity-exemptions.tsv` already records that
debt in its own words (`no jvp_layer_norm: the library's forward half stops a program before the
reverse is asked`) -- and behind it sits the whole transpose family: `reshape`, `softmax` and
`layer_norm` have no `_t` law either (`transpose.cml` files 45 of them and none of these), and `vjp`,
which is what `train.cml` calls, needs both halves. Even with those, `attention` would currently
carry the wrong `softmax<2>` tangent into the step.

## Answer, and the default

**Default: this round does not finish the family; the paper's transformer row does not enter §6, and
the text says where it stops, with one reading per wall.** Four things are left, in the order the
transformer needs them:

1. **`jvp_softmax`'s wrong axis.** The repair is `permute`'s and `reshape`'s repair -- name the port,
   hand it to a function -- and it is blocked only by
   `test/cases/compile/vjp/softmax_gradient_stops_at_split.cml`. That case's subject is "the backward
   half of a softmax cannot be built"; the backward half still cannot be built after the repair, it
   just says so in a later sentence. Re-read that case, then take the repair (and the same one for
   `jvp_log_softmax` / `log_softmax_t`, both at `<0>`).
2. **`test/plans/feat/compile/rules_angle_args.plan.toml`'s header** says the value "can only be
   *stated* by a rule, never bound from the node it matches". That is refuted by the three laws above
   and by `vmap` before them; it is a plan entry, so this lane cannot touch it. Whoever owns it should
   correct the header, and say whether the protocol -- a rule's parameters are the operator's ports in
   port order, the normal ones then the `<>` ones (`modules/transforms/rule_expand.cpp`) -- belongs in
   design principles 4.4/5.6 as a statement rather than an implementation detail.
3. **A `layer_norm` forward law.** It does not exist; the formula needs the mean and the variance over
   the trailing axes `gamma` covers, where `sum_axis_stated`/`mean_axis_stated` state one axis at a
   time, so it wants a reshape to `[..., numel(gamma)]` (the `reshape<dims>` in a body this page is
   about) and a decision about `eps`'s default against the kernel's.
4. **The `_t` family** for `reshape`, `softmax`, `layer_norm`. Until those exist no model that
   normalises can be differentiated by `vjp`, which is a property of the library and not of the
   transformer.

What changed today is what the wall *is*. It used to read "a rule cannot name the call's `<>` value";
that is not true. It now reads "one law answers the wrong axis quietly, one law is missing, and two
law families are unwritten", and only the first of those is a defect.

Laws: `stdlib/rulesets/jvp.cml:376` (`reshape_jvp`), `:1022` (`permute_jvp`),
`stdlib/rulesets/transpose.cml:1092` (`permute_cotangent`). Commit `6df52b106`.