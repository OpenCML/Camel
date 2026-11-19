# Item 7 — the closure of the transform rulesets

The rule: every operator `stdlib/rulesets/transpose.cml` can file a law for has to have a forward law
in `stdlib/rulesets/jvp.cml`, because the library is `transpose(split(reinterp<jvp>(f)))` — one half
writes the operators the other half walks. Measured here, the difference set is 11 keys, the 11 laws
are written, and the closure is a check in the fast layer.

## 1. The two sets and the difference

Read off the two files with `scripts/vjp-parity.mjs`'s own reader (`readLibrary`, the one that
reduces every rule to the `head@arity` key `src/core/forward.cpp#ruleKeyOf` derives):

| | laws | keys |
|---|---:|---:|
| `stdlib/rulesets/transpose.cml` | 44 | 44 |
| `stdlib/rulesets/jvp.cml` (before) | 59 | 43 |
| `stdlib/rulesets/jvp.cml` (after) | 70 | 54 |

`transpose` ⊆ `jvp` failed at **11 keys**:

`add/2`, `log_d/1`, `matmul_stated/4`, `max_axis_stated/5`, `mean_axis_stated/4`, `mean_stated/2`,
`min_axis_stated/5`, `mul_stated/4`, `sum_axis_stated/4`, `sum_stated/2`, `take_stated/3`.

The other direction is not a defect and was reported, not "fixed": 10 keys are `jvp`-only — `^/2`,
`abs/1`, `bf16/1`, `f16/1`, `f32/1`, `log/1`, `maximum/2`, `minimum/2`, `reshape/1`, `softmax/1` — and
those are laws the forward half needs and no transposed walk reaches.

Nine of the eleven are the **stated** spellings (`*_stated`), which are the linear half's own: a law
that computes a tangent already writes `sum_stated(dxs, shape(xs))`. The 2026-11-04 page
(`docs/handover/reviews/2026-11-04-does-the-library-need-forward-rules-for-the-stated-spellings.cn.md`)
named four of them and the author answered 甲 on that day ("四个都是线性的，每条规则一行"); `pick_at`
was closed then and the other nine were not. The remaining two are `add/2` (the array sum, whose
backward law `add_tensor_t` already existed) and `log_d/1` (`math:log_d`, the scalar logarithm).

## 2. What was added

Eleven `export rule`s at the end of `stdlib/rulesets/jvp.cml`, each one line and each the identity on
the tangent, plus two private stand-ins the family needs (`add`, `log_d` — a family cannot read
another module's private function, so `transpose.cml` declares its own copies the same way):

`jvp_sum_stated`, `jvp_sum_axis_stated`, `jvp_mean_stated`, `jvp_mean_axis_stated`,
`jvp_take_stated`, `jvp_mul_stated`, `jvp_matmul_stated`, `jvp_max_axis_stated`,
`jvp_min_axis_stated`, `jvp_add_tensor`, `jvp_log_d`.

`jvp_max_axis_stated` / `jvp_min_axis_stated` pass the caller's `mask` straight through: the mask is
the operator's selection, so it is a value of the call and not of the tangent — the same reading
`jvp_max_axis` writes, with the mask already computed. `jvp_mul_stated` and `jvp_matmul_stated` are
the two-term product rules `jvp_mul_tensor` and `jvp_matmul` already write, for the spelling that
carries the extent lists as operands.

No rule was changed and no existing law's right side moved.

## 3. The check

`scripts/vjp-parity.mjs --closure` reads the two files with the reader the parity gate already uses,
diffs the key sets in that direction only, prints both counts and `0 missing` (or one line per key
that has no forward law, naming the backward law and its left side), and exits 1 when any is missing.

`test/tools/check-transforms-closure.mjs` is the two readings that say it bites:

- the real tree — exit 0, `0 missing`;
- a `fake_closure_op_t` backward law appended to a copy of the two rulesets, with no forward twin —
  exit 1, reported by its key (`fake_closure_op@2`).

Both pass (measured on this worktree, `node test/tools/check-transforms-closure.mjs`): a checker that
answered `0 missing` for the second reading would be green on the day a backward law is filed alone.

The plan entry is `vjp_api.the_forward_half_closes_over_the_backward_half` in
`test/plans/feat/compile/transforms.plan.toml`, so it runs in the fast layer. Nothing was added to
`test/plan-selection.json`: the generic `stdlib/` rule already sends a change to `stdlib/rulesets/`
to `feat/compile`, which is the directory this entry lives in.

`feat/compile/transforms` with the entry: **total 209, pass 196, fail 0, known failures 13,
`RUN_EXIT=0`** (before the entry: 208 / 195 / 0 / 13).

## 4. The showcase re-run, and what it says

`showcase/c3-char-transformer/train.cml` (the pinned `train.stops_at_the_wall` entry) **still stops at
`transforms: jvp has no forward transform for this function`**. The closure is empty and the refusal
is unchanged, so the missing rules were **not** the cause of tonight's wall: the sentence names no
operator because no operator is missing a law — it is wall 1 of `showcase/c3-char-transformer/WALLS.cn.md`
(the type-pack concatenation losing multiplicity on array parameters) and the `grad` cut's
`first_with_as_param`, not a hole in the rulesets.

The same run reports one red that is **not this lane's**: `perf.showcase_c3` / `probes`,
`probes mismatch 01_grad_scalar:refused!=ok`. `grad(square)` no longer compiles: the diagnostic stands
on `stdlib/vjp.cml#grad`'s `split(jvp(first_with_as_param(f)))` and says `first_with_as_param` "moves
the first `with` parameter onto the ordinary list, and this function has no `with` parameter"
(`SemanticDiag::ArgumentsRejected`, `0x0300001C`).
Attribution, measured and not argued: with the eleven laws **stashed**, the same plan answers the same
`mismatched=1` and the same 18/1. It is the `grad` rewrite of 2026-11-18 (`vjp.cml#grad`: "the cut is
made here, once"), and it wants its own lane.

## 5. What this does not prove

- The key is `head@arity`, not the parameter-type list. A key with a forward law for one parameter
  list and not another is invisible here: the index distinguishes them, this check does not.
- A law being *present* is not a law being *right*. The 11 laws are unmeasured by this check; three of
  them (`jvp_sum_stated`, `jvp_mean_stated`, `jvp_take_stated`) are covered by the existing
  `vjp_api` / accuracy cases, the rest by nothing but their shape.
- The check reads the files, not the compiler's index. It uses the reader the parity gate has used
  since 2026-11-14 and shares its one assumption: a rule whose left side it cannot name is a failure,
  not a silent zero — `readLibrary` calls `fail()` in that case, so a new spelling of a left side is a
  red rather than a hole.