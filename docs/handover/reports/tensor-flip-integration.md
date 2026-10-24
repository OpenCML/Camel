# The tensor flip after integrating develop: 494 red, and what each one is

`origin/develop` (`ea624c980`) is merged into the flip's frozen tip (`c8e5e561d`) as
`5dad9d646`, a real merge commit. The flip's diff is untouched: no rebase, no rewrite.

## What the merge brought, and how the two collisions were resolved

The flip deletes `TensorType`; develop added `TensorType::literalOperandType()` in the same
file, so the two touch `modules/tensor/type.{h,cpp}`:

- `modules/tensor/type.h` — resolved to the flip's side: the `TensorType` class and
  `asTensorType` stay deleted, and `bool isTensorType(const type::Type *)` stays. Develop's only
  change to this file was the declaration of `TensorType::literalOperandType()`.
- `modules/tensor/type.cpp` — resolved to the flip's side: the `TensorType` implementation,
  `literalOperandType` included, goes with the class. **The rule survives on
  `ArrayType::literalOperandType()`** (`src/core/type/composite/array.cpp:641`), which the same
  develop commit (`2c9c05e78`) added to the core and the flip never touches.
- `modules/tensor/ops/indexing.cpp` — no conflict: the `sum_to` law string is byte-identical on
  both sides (`{sumLaw("sum_to_f32", …), sumLaw("sum_to_f64", …)}`), and the merge carries it
  once; develop's `sumToLaw` helper, a change the flip never makes, is kept beside it.

The merge also carries the f64 half of `sum_to` (the gradient kernels now take f32 and f64, one
arm per element type), the disjoint-`when` declaration rule, the completed literal rule, the
formatter's comment placement, the `#gpu` scope regions, the runtime's statement chain, and the
author's rulings.

## The three measurements, all on the same host in the same window

| tree | command | tests | fail | skip | wall |
|---|---|---|---|---|---|
| flip tip `c8e5e561d` | `smoke,feat --tier fast --jobs 8` | 5900 | **489** | 14 | 194.1 s |
| integrated `5dad9d646` | same | 5966 | **494** | 11 | 223.0 s |
| `origin/develop` `ea624c980` | same (same window, `-NoBuild`) | 5956 | **0** | 11 | 172.6 s |

Logs: `notes/h388-fast-integrated.log`, `notes/h388-fast-develop-base.log`, the flip's own
`h366-flip7.log`; per-test class lists in `notes/h388-{baseline,integrated}-reds.log`.

Develop is green in this window, so **not one red on the integrated tree is environmental or
develop's**. The three CUDA-device tests (`device_startup.available`, `device_startup.one_device_named_missing`,
`gpu_preference.named_device`) were *skipped* at the flip's baseline and *pass* on both trees
now: the cards were available, and there is no device red to explain away.

## Every red, by class, before → after

Bucketing is the attribution report's, applied by one classifier to both logs: the first
diagnostic in the block, with the diagnostic classes taking precedence over the runner's text.

| class | flip tip | integrated | delta |
|---|---|---|---|
| kernel element-type refusal | 162 | 3 | −159 |
| `NoMatchingFunction` hole | 49 | 49 | 0 |
| internal error | 43 | 43 | 0 |
| exit-code catch-all | 88 | 243 | +155 |
| print form | 59 | 63 | +4 |
| runtime error | 22 | 26 | +4 |
| other text | 66 | 67 | +1 |
| **total** | **489** | **494** | **+5** |

The counts the attribution report and the flip's own note state (162 kernel, 92 exit-code, 47
`NoMatchingFunction` as *first* diagnostic, 43 internal, 18 runtime, 36 old-print-form
expectations) are reproduced except at the edges: 162 and 43 exactly; the hole count is 49 under
the flip's own definition (49 blocks name a `NoMatchingFunction`, 47 have it first); my
exit-code / runtime numbers differ by up to four because a block can name a hole or a runtime
error behind an `expected exit` line. Nothing is left unexplained.

The transition is exact: **155** kernel refusals became exit-code, **4** became runtime errors,
**3** stayed kernel refusals, and the other 327 reds keep their class. **Not one of the 489
turned green.** Five reds are new, all of them develop's own new tests.

## What the integration fixed, and what is behind it

- **The f64 kernel half fixed the element-type refusal and bought no green.** The 159
  `Tensor<f64>` refusals disappeared, and every one of those tests fails at the next wall. Of the
  155 that landed in the exit-code class, 153 fail on `sum_to: cannot broadcast [N] to []`
  (`RuntimeDiag::TensorDimensionMismatch`) and 2 on `gelu_grad`'s shape check; of the 4 that
  landed in the runtime class, 3 fail on `matmul does not accept rank-0 tensors`
  (`grad_library*`) and 1 on the same `sum_to` broadcast. Every one of the 159 is a shape or
  rank check, and the ruling behind all of them is the same: a scalar result *is* the element
  type, so a reduction to `[]` reaches `sum_to` and `matmul`, neither of which has a rank-0
  case. The rest of the exit-code class (84) is smaller: onnx export refusals (25), functions
  declared to return a tensor whose result is now `f32` (10), the nine SIGSEGVs, tiles'
  gradient lowering (9), and singles.
- **The literal refusal, the formatter and the printer bring tests, not green.** Develop's new
  pins for the literal rule and for the f64 broadcast gradient fail on the flip's tree
  (`context.tensor_scalar_mismatch`, `context.tensor_literal_element_kind_in_instance`,
  `f64_broadcast_grad`, `f32_broadcast_grad.nvm|fvm`) — the last two show the gradient's *shape*
  wrong, not only its print form. Develop carries no change to the printing code
  (`src/core/rtdata/print.cpp` is untouched since the merge base), so no print-form red was
  fixed by it: the 63 print-form reds are the flip's own expectation debt, the array printer
  against goldens written for the tensor printer.

## The kernel refusals that remain: 3, all `Tensor<void>`

`train_step.loss.{nvm,fvm,jit}`, case `examples/models/naive-in-expert-out/train_step.cml`,
`'the gradient kernels' does not support a value of type 'Tensor<void>'; it takes f32 and f64
tensors`, raised at `logits.pb`. They pass on develop. None is a literal in the source to
convert, and none asks for a declaration to move to the module — the kernel already declares
f32 and f64. It is a genuine gap: a pullback result's element type is undetermined where the
ruling says a value's element type is always determined. The kernel's refusal is right; the
flip's VJP typing is incomplete.

## The 49 holes: unchanged, all of them

The same 49 tests name a `NoMatchingFunction` before and after, with the same operators. The 22
the previous lane gave a module route are **all still holes, none changed**:

- `tensor:gt` — 8 (`boundaries.{agir,argir,tooltip_slots}`, `export_branch`, `ort_branch`,
  `ort_dynamic_batch`, `onnx.export_branch`, `poison:trans.annotated_dump:boundaries.agir`);
- `tensor:add` — 6 (`arms_laid_out_whole_tensor.*`, `generic.arms_laid_out_whole_tensor`);
- `tensor:sum`/`sum_axis` — 6 (`numeric_contract.{nvm,fvm,jit,prl,generic_unit}`,
  `declared_numeric_semantics`);
- `tensor:numel` — 1 (`native.tensor_wrong_arity`);
- `tensor:show` — 1 (`native.tensor_show_wrong_arity`).

The other 27 keep no module route. In every case the value reaching the operator has element
type `void` (an unstated tensor leaf) or `bool`, and the operator's declaration is written over
the migration spelling `Tensor`.

## The internal errors are diagnostics; the crash is elsewhere, and it is the flip's

All 43 internal errors are compiler diagnostics, no signal among them, and all 43 carry the same
message: `camel: internal error: Runtime graph '…' breaks its edge invariants`, one root cause —
a variable or an argument whose type is a concrete numeric array (or a scalar) passed to a
parameter declared `var Tensor` or `Tensor`. By case: 34 in
`test/cases/modules/tensor/var_tensor_reference.cml` (the 33 `var_tensor.*` runs plus the poison
alias), 4 `var_tensor_read.*`, 2 `nn.lr` and `lr`, 1 `si_var_argument_plain`, and 2
(`grad_effects_kept.macro.simplify`, `grad_function_values_arms.macro.simplify`) surfaced as
`RuntimeDiag::PassInternalError` because a macro pass gave up on the same invariant.

The crash is a separate nine. Nine tests end `expected exit 0, got null` in both the flip's
baseline and the integrated tree, and the runner's own records name the signal:

```
exit=None signal=SIGSEGV timed_out=False   (wall 1.3–4.4 s, timeout_ms 8000–60000)
```

| repro (plan file; case) | tests |
|---|---|
| `test/plans/feat/compile/numeric_permissions.plan.toml`; `test/cases/compile/numeric/gemm_library.cml` | `library_gemm.tolerance`, `.eight_threads` |
| `test/plans/feat/modules/tensor.plan.toml`; `test/cases/modules/tensor/binary_roundtrip.cml` | `binary_roundtrip` and its poison alias |
| `test/plans/feat/modules/onnx.plan.toml`; `…/onnx/export_grad_mean.cml`, `…/export_grad_matmul_vector.cml` | `export_grad_mean`, `pass_export_grad_mean`, `export_grad_matmul_vector`, `pass_export_grad_matmul_vector` |
| `test/plans/feat/modules/index_semantics.plan.toml`; `test/cases/modules/tensor/index_semantics.cml` | `declaration_evaluator` |

All nine pass on develop in the same window. Nine tests over five cases, all on
tensor/gradient/export paths; they are the flip's, not the merge's, and they are what makes the
flip unlandable today.

## What has to be decided

Landing the flip now moves develop from 0 to 494 red, of which 9 are SIGSEGVs — that is the
unlandable part, independent of any count. The rest is known and ranked:

- **Fix the rank-0 representation first** (the wall behind the f64 arm): 159 reds, and it is one
  ruling, not 159 defects.
- **Then the `var Tensor` edge invariant**: 43 reds with one root cause (a declared `Tensor`
  parameter against a `f64[?, ?]` variable).
- **Then the 22 declarations** (`tensor:gt`, `:add`, `:sum`, `:numel`, `:show` written over
  `Tensor<T>` in the module): 22 holes, none of which the merge moved.
- **Then the 63 print-form goldens** and the 5 new pins, as expectation updates that follow from
  the ruling that the printer prints by the type the value holds.
- **The 9 SIGSEGVs need their own diagnosis**: they are not the rank-0 family's obvious
  members, and they are the one class that cannot be carried as "known red".

Cost of landing now, in user-visible behaviour: `Tensor` is a numeric-array alias whose
unstated element type reads as f64, rank-0 values are the element type itself, tensor printing
is the array printer's, and `var Tensor` parameters produce a hard "defect of the compiler"
message. Recommendation: **do not land yet**; the integration has settled the element-type
question on the kernel side (`sum_to_f64` is now a real arm), so the flip's remaining work is
the rank-0 path plus the three named items, and re-measuring after the rank-0 fix is one run.