# The stable softmax is not always the more accurate form (f32)

## What the check measured

Rule under test (f32, axis is a rule parameter):

```
exp(x) / sum(exp(x), axis, true)  =>  softmax(x, axis)
```

The check sampled the rule's own tensor shape and axis and found this sample:

```
x = [-1e-16, 27.004, -33.563]     axis = 0     (the rank-1 form of -1)
```

Against the long-double reference of each side:

| side | error on the tiny element |
|---|---:|
| left (naive `exp(x) / sum(exp(x))`) | 7.76e-8 |
| right (stable `softmax(x)`) | 1.91e-6 |

The actual error ratio is 24.6x. `check` reports 8.01648x because it floors the
denominator at 2 ULP of f32 (2.384e-7) so a rule that is exact on the left does
not produce an infinite ratio. Either way the right side is worse, so the rule
fails:

```
FAIL tensor_softmax_naive: the right side is worse at
(tensor<f32> [3] {-1e-16, 27.004, -33.563}, 0): its error 1.91128e-06 exceeds
the left side's 7.75714e-08 (ratio 8.01648)
```

## Mechanism

The naive form exponentiates the small element at `x = -1e-16`, essentially
zero, where `exp` is exactly 1 and both the argument scaling and the subtraction
contribute nothing. Its tiny probability is then accurate to about 7.8e-8.

The stable form exponentiates `d = x - max`. Here `d = -27.004` for the
`-1e-16` element, and the other element has `d = -60.567`; the probability
ratio is `e^-60`, about 1e-26, hence "elements about 60 below the maximum" when
measured in nats. Two effects grow with `|d|`:

1. The f32 subtraction `x - max` rounds. Its absolute error is up to half an
   ULP of `d`; at `|d| = 60.567` that is about 1.9e-6. `exp` maps an absolute
   error in its argument directly to a relative error in its result (the
   relative error of `exp(d)` equals the absolute error of `d`), so the
   probability inherits that 1.9e-6.
2. `expf`'s own argument reduction computes `d * log2(e)`, whose absolute error
   is about `|d| * 2^-24`; the resulting relative error is about
   `|d| * 2^-24 * ln 2`, roughly 1e-6 at `|d| = 27` and 2e-6 at 60.

Both effects are invisible in the naive form for this sample because its
argument is near zero. **Subtracting the maximum therefore trades
overflow-avoidance for a worse relative error on elements far below the
maximum.** That is the opposite of the claim in rewrite design 8.10 and in the
showcase, which treat the stable form as uniformly "no worse".

## The two options (not decided here)

**(a) Licence the stable form with the trade documented.** Keep
`exp(x) / sum(exp(x), axis, true) => softmax(x, axis)` behind
`#requires<refine_accuracy>`, and state in the library that it removes
overflow, underflow and 0/0 (the naive form returns NaN for logits above
`ln(FLT_MAX)` or below about -104) while its relative error on elements about
60 below the maximum can be worse by roughly one to two orders of magnitude
(1.9e-6 against 7.8e-8 on this sample, 24.6x). This is a real domain trade, not
a bit-for-bit improvement.

**(b) Keep it unlicensed until a C++ subtract-max kernel subtracts before the
f32 rounding.** A kernel that forms `x - max` without rounding the difference
to f32 (for example in f64 or with an extended-precision step) removes both
effects above, and then the stable form is no worse on this sample. The rule
stays out of `rules::accuracy` until such a kernel exists; the showcase's
softmax claim waits for it.

## Caveat: is the reference's maximum apples to apples?

Yes for the input and the axis. `refSoftmax` reads the same f32 tensor and
reduces over the rule's axis operand with the same `outer/extent/inner` layout
as the kernel. The maximum is formed from the exact f32 element values in long
double; comparison is exact, so it is the same maximum the f32 kernel selects
(the sample has no NaN, where `max` propagation could differ). What is
deliberately higher precision is everything after the maximum: `x - max`,
`exp`, the sum and the division are long double. The comparison is therefore
apples to apples on the inputs and the reduction axis, and intentionally not
apples to apples on the arithmetic precision, which is the point of a
higher-precision reference.

Pinned by `test/cases/modules/rules/check_rejected_softmax.cml`; the rule is
not exported by `rules::accuracy` or `rules::simplify`.