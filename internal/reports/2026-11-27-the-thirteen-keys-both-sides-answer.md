# The thirteen keys both sides answer: what each side says, and whether the numbers differ

Measured at `lane/gauge-two-sided @ 0d0222be92` (its base `origin/integration @ 15ff84c89c`). The
numbers below are **the two formulas evaluated at the sample points** -- read out of the rule and the
registration and worked through by hand -- not execution readings. The distinction matters and is
kept in every row: a formula pair that is the same arithmetic expression cannot drift, while two
different orderings of the same arithmetic can, and only running both would show it.

Thirteen keys, from `node scripts/vjp-parity.mjs`: `abs@1 bf16@1 f16@1 f32@1 log@1 maximum@2
minimum@2 reshape@1 argmax@1 argmin@1 concat@2 dim@1 unsqueeze@1`. Eight of them the library answers
in the forward direction only (`jvp` law, no reverse law), and the engine answers in reverse
(`setVjp`/`setRule`); five of them the library files a batching law for (`vmap.cml`) and no
derivative at all. So "both sides answer" is true of the key, not of one quantity: in the first
eight the two answers are a forward law and the reverse rule that is its transpose, and in the last
five the two answers are different kinds of statement.

## The eight the library answers forward, the engine in reverse

For these the common quantity is the derivative coefficient each side applies at a point (`dx` on
the library side, `dy` on the engine side). Written as coefficient, the two are directly comparable.

```
key         sample points            Camel (library law)              C++ engine (its rule)        verdict
abs@1       x=2, x=-3, x=0           1, -1, 1                        1, -1, 1                     same (exact, and the tie at 0 goes positive on both)
log@1       x=2, x=0.5, x=1e-8       0.5, 2, 1e8                     0.5, 2, 1e8                  same (both are 1/x, same divide)
maximum@2   (3,1), (1,3), (2,2)      (1,0), (0,1), (1,0)             (1,0), (0,1), (1,0)          same (tie at equal operands goes left on both)
minimum@2   (3,1), (1,3), (2,2)      (0,1), (1,0), (1,0)             (0,1), (1,0), (1,0)          same (mirror of the above, tie still left)
reshape@1   (2,3)->(6,) values 1..6  reshape of the tangent          reshape of the cotangent     same (no arithmetic: a layout move both ways)
f32@1       dx=1.0, dx=0.1, dx=1e-45 1.0, 0.10000000149011612, 1.401298464324817e-45
                                     1.0, 0.1, 1e-45                                              differ in the last bits (see below)
f16@1       dx=1.0, dx=0.1, dx=6e-5  1.0, 0.0999755859375, 6.103515625e-05
                                     1.0, 0.1, 6e-05                                              differ (2.4e-4 relative at 0.1)
bf16@1      dx=1.0, dx=0.1, dx=1e-3  1.0, 0.10009765625, 0.001007080078125
                                     1.0, 0.1, 0.001                                               differ (9.8e-4 relative at 0.1)
```

Where the two sides' arithmetic is the same expression -- `abs`, `log`, `maximum`, `minimum`,
`reshape` -- the values are equal exactly, tolerance zero, and the only difference the parity reader
sees is which direction each side files. Tie behaviour was checked on purpose because that is where
such a pair usually splits: `abs(0)` and an equal `maximum`/`minimum` pair both go to the same side
on both implementations.

The three conversions are a real difference and a different kind of one: the library's law narrows
the tangent to the target width (`jvp_f32_from_float` is `f32(dx)`), while the engine's
`:op/ftof` rule is `identityVjp` -- it hands the cotangent through unchanged, and the narrowing, if
any, happens wherever the gradient is finally stored. At `dx=0.1` that is `0.10000000149011612`
against `0.1` for `f32`, `0.0999755859375` against `0.1` for `f16`, `0.10009765625` against `0.1` for
`bf16`: relative differences of 1.5e-8, 2.4e-4 and 9.8e-4. **Neither side is shown wrong here.** A
finite-difference reading with a step well above the type's ulp gives a derivative of 1 for all
three, which both sides report; what differs is whether the cotangent is rounded on the way out.
Marking a wrong side would be a guess, so: **cannot be decided from these two formulas; needs a
finite-difference third reading.**

The next step, written down so no reader takes the numbers above for a measurement: **one case, one
build, both readings at the same points** -- the library side through `vjp` (or `jvp`), the engine
side through `with_grad`, printing both numbers for `dx = 1.0`, `0.1` and the width's smallest
normal. That is the line between "the formulas say this" and "the code does this"; until it is run,
these three rows are arithmetic on two sources, not observed behaviour.

## The five the library files a batching law for and no derivative

`argmax@1`, `argmin@1`, `concat@2`, `dim@1`, `unsqueeze@1`: `vmap.cml` files `vmap_rule_*` for each,
which says how to carry the operator over an extra axis, and files no derivative law. The engine has
a reverse entry for each: `noGradient` for `argmax`, `argmin` and `dim`, `reshapeLikeVjp` for
`unsqueeze`, `concatVjp` for `concat`.

```
key            what the library answers        what the engine answers            the same quantity?
argmax@1       batching only                  reverse: zero (noGradient)        no -- a batching rule is not a derivative
argmin@1       batching only                  reverse: zero (noGradient)        no
dim@1          batching only                  reverse: zero (noGradient)        no
unsqueeze@1    batching only                  reverse: reshape the cotangent    no
concat@2       batching only                  reverse: split the cotangent      no
```

Sampled where a number exists at all: `argmax([1, 3, 2])` and the tie `argmax([3, 3, 1])` -- the
engine answers 0 for a gradient; the library has no derivative answer to give. `concat([1,2],[3,4])`
at axis 0 with cotangent `[10,20,30,40]` splits to `[10,20]` and `[30,40]` on the engine side, and
the library again has no derivative answer. `unsqueeze` of a `(2,3)` value to `(1,2,3)` reshapes
back on the engine side; the library has the batching rule instead.

So these five are not "numbers that disagree": one side states a derivative (zero or a structural
adjoint) and the other states a batching law. Their difference is direction coverage and kind, not
value, and no side is wrong in anything these two sources show.

## What this classification does and does not say

· Four keys are the same expression written twice, one key is the same layout move written twice:
  nothing to fix, and the parity reader lists them because the *direction* each side files differs.
· Three keys (the float-width conversions) differ in the last bits by construction of the
  narrow-on-the-way-out rule. Not a defect either way; a finite-difference reading is what would
  settle which convention is wanted.
· Five keys are a batching law against a derivative rule. Not comparable as values.
· No key was shown to have a side computing the wrong derivative.

What would upgrade these readings from formulas to measurements: run each row through both paths on
a build (`vjp`/`jvp` for the library side, `with_grad` for the engine side) and print the two
numbers at the same points. That needs a build and a case written for it, which this reading did not
spend.