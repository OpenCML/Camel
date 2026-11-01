# Two cotangents `transpose` answers wrongly today

Status: **to fix, recorded, not asserted.** No plan case is added for either, and no `known_fail` mark
is added: the runner's `known_fail` is per backend, and what is wrong here is a reading on every
backend at once. The two entries are the same kind of thing as the `vmap`/`remat` kernel readings --
a number the tree prints today that the law says is not the number.

Both are inside the five `law == ...` branches of `modules/transforms/transpose.cpp` -- the C++ that
re-writes the law instead of expanding the rule's right side. Both are dead weight *and* wrong, which
is the argument for deleting the branches rather than keeping them.

## T1 -- `neg` answers the cotangent with the wrong sign

`transpose` of `neg(x) = -x` at `x = 5`, cotangent `1`:

| case | today | the law |
|---|---|---|
| `-x`, cotangent `1.0` | **`1.0`** | `-1.0` |
| `-x`, cotangent `2.0` | **`2.0`** | `-2.0` |

Measured on `std::nvm` with a scratch case (`transpose(neg)(5.0, 1.0)`), not with a plan case: no
plan case covers the sign of `neg`'s cotangent, which is why this stayed green.

The branch is `sub_t || neg_t`, and its loop treats the first non-constant operand as `first` and
hands it `ct` -- correct for `x - c`, whose cotangent in `x` is `ct`, and wrong for `-x`, whose only
operand is both the first and the negated one. `sub_t` and `neg_t` are the same branch only because
one operand's position happens to line up; the laws are not the same law.

**The library already has the right answer**: `neg_cotangent(ct) = -ct` in `stdlib/autodiff.cml`.
Once `transpose` expands the rule's right side instead of dispatching by name, this number becomes
`-1.0` / `-2.0` with no separate fix -- the rule path is the fix, not a change of behaviour the
branch was protecting. So the acceptance for the branch deletion lists `-x` going from `1.0/2.0` to
`-1.0/-2.0` as **a repair, not a regression**.

## T2 -- `div` answers the cotangent in the divisor from `x * x`, not `c * c`

The branch computes, for the second operand of `x / c`:

    c   = knownValue(inputs[1 - i])     // i = 1, so this is the *numerator* x
    a   = constantValue(inputs[0])      // the numerator again
    top = ct * a
    sq  = c * c                         // x * x
    answer = -(top / sq)                // -(ct * x / (x * x)) = -ct / x

The law says the cotangent in `c` is `-(ct * x / (c * c))`, which is what
`div_cotangent_c(x, c, ct)` in `stdlib/autodiff.cml` answers. The branch's `c*c` is the *other*
operand's square, so it answers `-ct/x`: a different function of the same inputs, equal only where
`x == c`.

It has never run. `i = 1` is the divisor, and a divisor is a constant in every program the tree
transposes, so `isConstant(inputs[1])` skips the position before the formula is reached. That is why
this is dead code *and* wrong: nothing measured it, so nothing noticed.

**The library already has the right answer** here too: `div_cotangent_c(x, c, ct)` is written for
exactly this position, and is called by nothing today. The rule path reaches it, so this number
becomes the law's number as a consequence of deleting the branch.

## What would count as fixed

- `-x` at cotangent `1.0` / `2.0` answers `-1.0` / `-2.0`, printed by a case that names the sign.
- `x / c` with a non-constant divisor answers `-(ct * x / (c * c))`, or is refused by name if the
  value it needs is not there -- but not `-ct/x`.
- Neither is asserted before that: a case asserting a number the tree does not answer would only
  record the defect, and `known_fail` would record it as one backend's problem.