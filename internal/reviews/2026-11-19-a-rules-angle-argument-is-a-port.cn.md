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
`softmax<2>`, and answers the `1` law to it. Two readings, each with the answer computed by hand:

`f(x) = sum(softmax<2>(x) * w)` on `x = 0` along `dx = 1..12` (by hand: `sum(w)/3 = 26` and
`4 * 2/3 = 2.666667`):

```
softmax axis 2 value=26.000000 tangent=13.500000     <- the law in the tree, which says `softmax<1>`
softmax axis 2 value=26.000000 tangent=2.666667      <- the same law with the axis bound
```

`f(x) = sum(log_softmax<1>(x) * w)` on `x = 0` (`[2, 3]`, `w = 1..6`) along `dx = 1..6` (by hand:
`f = -log(3) * 21 = -23.070858`, and `dy = dx - mean(dx)` per row gives `f' = 4.0`):

```
log_softmax axis 1 value=-23.070858 tangent=13.500000   <- `jvp_log_softmax`, which says `<0>`
```

A wrong tangent read quietly is worse than a missing one, and nothing in the tree reported either.

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

so the value goes through a function body, which is where a `<>` argument stated at run time already
lives (`vmap_permute`, `reshape<batched_shape(xs, dims)>`; in this tree `reshape_jvp` and
`permute_jvp`). And in the reverted `softmax` repair that function was a callee with an open-rank
parameter, which is what the transposed walk refuses:

```
TRANSPOSE function=rulesets.jvp::softmax_axis_jvp<float> skip=parameter-shape uri=tensor:multiply
```

## Why it matters

`reshape` and `permute` landed (`0e9e882a5`, with the `@example` and snapshot follow-ups after it);
`softmax`'s repair is written, measured and **reverted**, because
`test/cases/compile/vjp/softmax_gradient_stops_at_split.cml` pins today's sentence
(`split does not build this function`, `skip=backward construct=a node whose input was not copied`)
and the named form makes the split *succeed* and fail one step later at the transpose. Moving a wall
is not supposed to move a case under it, and this lane may not edit expectations, so the wrong-axis
law stays and the case stays green.

`permute` is off the list: the operator mix `attention` writes lifts whole -- a `[8, 16]` input
through `reshape<[8, 2, 8]>`, `permute<[1, 0, 2]>`, `softmax<2>`, `transpose`, a multiply and a sum:

```
REINTERP jvp function=f ... rules=jvp_reshape,jvp_permute,jvp_softmax,jvp_transpose,jvp_mul_tensor,jvp_sum
mix value=2.000000 tangent=0.000000
```

(`2.0` is `128 / 64`, the same by hand; the tangent is zero because the direction of ones is the one
`softmax` is invariant along.)

The transformer itself stops on the operator that comes before the attention call: `layer_norm`.

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

1. **`jvp_softmax`'s wrong axis** -- answered one way, and it opens a second question. `271e02902`
   made the fit compare the `<>` values a law's own left side *writes*, so a law that states
   `softmax<1>` no longer answers a `softmax<2>` node, and the silent `13.5` is gone:

   ```
   READING_REFUSAL {"transform":"jvp","function":"f","cause":"rule","uri":"tensor:softmax"}
   error…: jvp has no forward rule for `tensor:softmax` (the rule 'jvp_softmax' writes 1 `<>` value(s)
   of its own ((int)), and this node's configuration is not that one ((float[2, 2, 3], int))).
   ```

   the same for `log_softmax<1>` (`'jvp_log_softmax' writes 1 `<>` value(s) of its own ((int))`), and
   both `softmax_gradient_stops_at_split` and `log_softmax_gradient_stops_at_split` still pass.

   **But the comparison does not yet tell a written value from a bound parameter.** A law that names
   its configuration -- `reshape<dims>(x)`, `permute<perm>(x)`, and `vmap`'s and `jvp`'s stated
   reductions -- writes no constant, and gets the same refusal:

   ```
   jvp has no forward rule for `tensor:reshape` (the rule 'jvp_reshape' writes 1 `<>` value(s) of its
   own ((int[])), and this node's configuration is not that one ((float[...], int[])))
   jvp has no forward rule for `tensor:permute` (the rule 'jvp_permute' writes 1 `<>` value(s) of its
   own ((int[])), and this node's configuration is not that one ((float[2, 2, 3], int[])))
   ```

   which is `06_layout.cml` refused again, `permute`'s forward law dead, and 27 reds in `feat/compile`
   (2644 / 2604 / 27 against the trunk's 2638 / 2622 / 0) and 8 in `feat/transforms`. So what this
   question still needs is the second half of that distinction: a `<>` slot the left side **writes**
   is compared by content, a `<>` slot the rule **binds** is matched and bound as before.
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

Laws: `stdlib/rulesets/jvp.cml:377` (`reshape_jvp`), `:1031` (`permute_jvp`), and
`stdlib/rulesets/transpose.cml:1092` (`permute_cotangent`), `:1097` (`permute_t`). Commits
`0e9e882a5` (the laws), then `7e6981346`, `4a5f68386` and `d5b157a8a` (the `@example` lines the
reference asks for, and the two-line, then one-line, AST snapshot those follow).

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

develop 上默认已落：`jvp_softmax`、`jvp_log_softmax` 以 `axis` 为参数（`src/stdlib/rulesets/jvp.cml:536/563`），`layer_norm` 前向律也已有。规则从节点绑定 `<>` 值就是普通匹配（9.11："规则与事实的形参 `x: T` 的类型是 `Node<T>`"）。还剩三件：规则右侧写 `<>` 属核心抽象第 6 节、第 11 节第 2 条"规则语言四项扩充"，由决策者在变换一步核实；这条拒绝要改成具名诊断（9.9："在编译期拒绝并点名这个运算"）；`rules_angle_args.plan.toml` 头那句"只能写死"要改。
