# The Z bucket of the flip integration: two rulings and not a lane's fix

Question raised by `handover/h445-flip-z-fixes` (the lane that was given the Z bucket of
`docs/handover/reports/tensor-flip-integration.md`, H445). It records measurements taken on
`handover/h443-flip-survey` (`a566fa1f8`) merged with `origin/develop` at `3e927a7dd`, full layer
`-Plan smoke,test/plans/feat -Tier fast -Jobs 8`:

    [SUMMARY] total: 6153, pass: 5842, fail: 292, skip: 11, known failures: 8

(h443's survey measured 6122/5812/291/11/8 on its own merge base; the one extra red is
`feat.docs_headers` `docs.headers.canonical`, which sees the two `notes/h419-*.py` drafts the flip
line commits — it does not exist on `origin/develop`.)

The lane set out to fix the Z items by plan. Measuring them one by one, **the great majority are
not a develop-side omission that a lane may repair**: they are two families whose fix is a ruling
this document asks for. Everything below is verbatim from that run.

## 1. The print form (the larger family)

A tensor value is printed with the array printer (`[8.0, 8.0]`), while the goldens of the
tensor-era tests still expect the tensor printer's text. Two different expected forms are in the
corpus, and today's code produces neither:

| Where | Expected (verbatim, `[INFO]`) | Got (verbatim, `[stdout]`) |
| --- | --- | --- |
| `feat.opt.generic` `arm_tail_gate.*` (5) | `does not contain 'tensor('` (`feat.opt.regions`) | `[8.0, 8.0]` |
| `feat.modules.rules` `tensor_laws_execute.*` (3) | `tensor([      8.,      16.,      24.], dtype=float32)` | `[8.0, 16.0, 24.0]` |
| `feat.opt.generic` `effect_order_folded.*` (6) | `[      0.,       1.,       0.` (the `%8.1f`-aligned array form) | `[[0.0, 0.0, …]]` |
| `feat.modules.tensor.dtype` `f32_broadcast_grad.*` (2) | `gx tensor([[…]], dtype=float32)` | `gx [0.1, 0.3, 0.7], 0.1, 0.3, 0.7]]` |

`docs/handover/questions/unstated-element-type-scalar-and-print.md` (H419) already asks which form
is terminal and notes that the flip's own `test/plans/feat/compile/array_alias.plan.toml` pins
"the printed text of every spelling is the array's". The measurements above show the corpus asking
for **three** forms at once — the tensor `tensor(…, dtype=…)` text, a `%8.1f`-aligned array text,
and today's `[8.0, 8.0]`. Choosing among them is a language-visible decision and it is the
author's. Rewriting ~90 goldens by hand is not this lane's account either way.

## 2. The unstated element type of a tensor leaf

`t[0]` on a bare `Tensor` (an array whose element type is not fixed) yields `void`, so the value
that reaches the next operator has no numeric type and no arm accepts it. Verbatim, the 22 case
level occurrences of this family in the run:

    (arms_laid_out_whole_tensor.cml:23) [Error]: No matching function found for argument types:
    <> (void, float) … (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)

    numeric_contract.cml:57:39: [Error]: No matching function found for argument types: <> (bool)
        <tensor:sum>: (t: Tensor) => float
        <tensor:sum_axis>: (t: Tensor, axis: int, keepdims?: bool) => Tensor
     (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)

Tests: `feat.opt.generic` `arms_laid_out_whole_tensor.*` (5), `feat.opt.screens`
`generic.arms_laid_out_whole_tensor`, `feat.modules.onnx` `{export_branch,ort_branch,ort_dynamic_batch}`,
`feat.trans.annotated_dump` `boundaries.{agir,argir,tooltip_slots}`,
`feat.exemplars/{01_scalar_boundary,08_structured_control_flow}`,
`feat.mm.gc_poison_sweep` `poison:trans.annotated_dump:boundaries.agir`,
`feat.modules.tensor` `numeric_contract.*` (3) and
`feat.modules.operator_semantics` `declared_numeric_semantics`.

`modules/tensor/ops/layout_ops.cpp` `takeInfer` is the entry that decides what a subscript of an
unstated array yields, and H419's question file lists three options (A/B/C) for it. The same
question file records that H419's own ledger puts the operator declarations written over the
migration spelling `Tensor` after the 22 declarations, and that `Tensor<void>` VJP typing and the
print goldens come last of all *because the ruling is the author's*.

## 3. What this lane did instead, and what a ruling unlocks

Landed here (each with its own commit, each red-before → red-after read out):

1. `node scripts/diagnostics-catalog.mjs --write` and `node scripts/headers.mjs --fix` —
   `feat.compile.diag_catalog` `catalog.doc_is_generated` and `feat.docs_headers`
   `docs.headers.canonical` are generated-artefact reds. Verbatim, on the merge of
   `origin/develop` at `3e927a7dd`:

       before: [SUMMARY] total: 6153, pass: 5842, fail: 292, skip: 11, known failures: 8
       after:  [SUMMARY] total: 6153, pass: 5844, fail: 290, skip: 11, known failures: 8

   Nothing else moved: the grouped `[FAIL]` sets differ by exactly those two tests and no test
   became red. Every other family in this document was measured and left alone for the reasons
   above; §4 records the one fix that was attempted, measured and reverted because it moved no
   red.

Still to decide, each of which moves reds this lane cannot move:

- **The print form** (§1): which of the three texts is terminal. A ruling turns ~90 expectation
  updates into mechanical work.
- **The unstated element type** (§2): what `t[0]` yields on a bare `Tensor`. A ruling turns the
  22 `NoMatchingFunction` holes into module-arm work.

## 4. Two defects found, diagnosed and deliberately not landed

`feat.modules.tensor.dtype` `gather_grad_f64.{nvm,fvm}` (`m.w[m.labels]` on an `f64[2, 3]` under
`with_grad`) refuses with
`autodiff: no derivative rule for operator ':op/idx_take' of type 'float[..]'`. The subscript of a
tensor is the **core** array gather `:op/idx_take` (`src/builtin/modules/globals.cpp:1710`) once
`Tensor` is the native array type, and the core has a kernel (`op/idx_take`,
`src/builtin/executors/builtin.cpp:327`) and a rule for the scalar-index form (`:op/idx_arr`) but
none for the index-array form. Registering the module's own adjoint under that URI
(`DerivativeRegistry::setRule(":op/idx_take", takeVjp)`, measured) does get past the refusal and
then fails inside the adjoint:

    Tensor shape mismatch in 'take_grad': take_grad expects a gradient shaped like the take result

so the missing rule is the *symptom*: the core arm's declared result is the whole array
(`(arr: T[], indices: int[]) => T[]`, i.e. `norm[0]`) while its kernel returns the taken rows
(`[len(indices), …]`, `modules/tensor/kernels/layout.cpp:362`), so the static type a gradient is
built against is not the value's shape. That is a typing defect of the core arm, not an arm a
mirror lane should add, and the change was reverted: it moved no red. A lane that owns the core
array kind should fix the arm's result type and then register the adjoint.

Two items this lane declines to touch without a ruling, recorded here so they are not lost:
`rules.tensor_saturation_keeps_values` fails with
`error RewriteInvalid: Two writings of a value have the same type, region and permissions, and the
permissions the equivalence needs.` (a `std::logic_error` out of `DraftWritings::assertEqualDeferred`,
`src/runtime/writings.cpp:779`) — a real defect of the rewrite engine's permission check rather
than a missing arm; and `alias.err_var_rank.nvm` pins that `f32[?, ?] = f32[5]` must keep being
refused, so the bare-array-into-a-shaped-`var` assignments (`modules.nn` `lrit`/`mnist`,
`modules.tensor` `static_types`) cannot be repaired by widening `ArrayType::assignableFrom`, which
deliberately refuses a numeric-unknown source for a stated element type.