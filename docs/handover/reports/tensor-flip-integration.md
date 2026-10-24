# The tensor flip after integrating develop: 454 red, the nine SIGSEGVs fixed, and what each red is

`origin/develop` (`ea624c980`) is merged into the flip's frozen tip (`c8e5e561d`) as
`5dad9d646`, a real merge commit. The flip's diff is untouched: no rebase, no rewrite.

**Read this note for the current state**: the ledger below has two measurement points — the
integration (`5dad9d646`, 494 red, nine SIGSEGVs) and the crash round (`9670cd287`, 454 red,
zero signals) — and the ranking at the end is written from the later one. Every number says
which target produced it: `smoke,feat --tier fast --jobs 8` is what the integration round ran
(through the remote helper's `-Tier fast`); **`node scripts/test.js test/plans --tier fast
--jobs 8` is this lane's gate from now on**, whole tree, because five crash sites live in the
`gpu.profile` plan, which smoke+feat never selects.

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
| integrated + crash round `9670cd287` | `node scripts/test.js test/plans --tier fast --jobs 8` | 5972 | **454** | 11 | 282.7 s |

The last row is the whole tree, not smoke+feat: it selects six more tests than the row above,
`gpu.profile/{host_loop.nvm,host_loop.fvm,host_loop.jit,json.nvm,off.nvm}` and its neighbour, and
all six pass after the crash round and **all of them crash on `5dad9d646`** (cause C below). So
the two rows compare test for test: **494 → 454, forty reds green, none new**, and the nine
`expected exit 0, got null` entries are **zero** (`notes/h388-fast-fixed2.log`, case-level
per-test output under `test/results/log/20261024_021832_2719996/`).

Logs: `notes/h388-fast-integrated.log`, `notes/h388-fast-develop-base.log`, the flip's own
`h366-flip7.log`, and the crash round's `notes/h388-fast-fixed2.log`; per-test class lists in
`notes/h388-{baseline,integrated}-reds.log`.

Develop is green in this window, so **not one red on the integrated tree is environmental or
develop's**. The three CUDA-device tests (`device_startup.available`, `device_startup.one_device_named_missing`,
`gpu_preference.named_device`) were *skipped* at the flip's baseline and *pass* on both trees
now: the cards were available, and there is no device red to explain away.

## Every red, by class, before → after

Bucketing is the attribution report's, applied by one classifier to both logs: the first
diagnostic in the block, with the diagnostic classes taking precedence over the runner's text.

| class | flip tip | integrated | + crash round | delta, integrated → now |
|---|---|---|---|---|
| kernel element-type refusal | 162 | 3 | 0 | −3 |
| `NoMatchingFunction` hole | 49 | 49 | 7 | −42 (see below) |
| internal error | 43 | 43 | 40 | −3 |
| exit-code catch-all | 88 | 243 | 250 | +7 |
| print form | 59 | 63 | 75 | +12 |
| runtime error | 22 | 26 | 15 | −11 |
| other text | 66 | 67 | 67 | 0 |
| **total** | **489** | **494** | **454** | **−40** |
| of which `expected exit N, got null` (a signal) | — | **9** | **0** | **−9** |

**Read the `NoMatchingFunction` row with care**: 49 → 7 is a property of the *block* the
classifier reads, not of the flip. The hole is still there in every one of those cases — running
`test/cases/trans/annotated/boundaries.cml std::agir` on `9670cd287` still prints
`error[SemanticDiag::NoMatchingFunction]: … operator '>' is not defined for these operand
types`, exit 1 — and at case level **47 of the 454 failing entries still name a
`SemanticDiag::NoMatchingFunction`** in their captured output (49 before, by block). What moved
is which part of a failing snapshot diff lands inside the block. Sixty-two other reds moved
class the same way: a crash or an earlier wall no longer stops them first, so they now report
the next expectation that fails (`NoMatchingFunction`/`runtime-error`/`INTERNAL`/`kernel-refusal`
→ exit-code for 46 of them, → print form for 10). They are still red; the numbers above count
them once each.

The transition from the integration to now is otherwise exact: **40 reds green, none new**, and
the green ones are the crash round's own families — the nine signal tests, the `tiles.*.source`
gradient lowering (9), the `export_train_step` family (15), `grad_through_as.*.nvm` (3),
`train_step*` (4) and the rest.

## The crash round (`9670cd287`): nine signals to zero, two causes

The nine SIGSEGVs were two causes, and both were the flip's own representation handing a value
on in a shape its declared type contradicts. Diagnosed on the host with the Debug build
(`out/debug`, asserts live, DWARF) and `-vvv --log-include FastVM`:

- **A — a rank-0 kernel result handed on as the bare element under an array type (7 of 9).**
  `wrap()` unwrapped every rank-0 result by value. When the declared result type is an array —
  a written shape, or one whose rank the compiler left open (`Tensor`, `f32[..]`) — every reader
  takes that slot for an array, so the array was handed to a consumer that read an `Array *` out
  of a float slot: `Array::rawData ← Operand::of ← operandArg ← binaryKernel`. Sites:
  `from_bytes` → `to_bytes`/`println` (`modules.tensor/binary_roundtrip`, its poison sweep), the
  literal-`0` axis of a reduction (`library_gemm.tolerance`, `.eight_threads`), the
  index-evaluation harness's `fromSlot<Array *>`, and a vector-by-vector `matmul` whose
  elementwise product reached the binary kernel with an element operand.
- **B — the tensor's tangent space was dropped (2 of 9).** The flip registers no tangent space of
  its own, so an array-coded tensor fell back to the core array-kind space, whose
  `:tangent/add_arr` and `:tangent/zeros_arr` sum elements through `get<slot_t>` and answer a
  *slot* array with no element type, where a numeric tensor is required:
  `Array::createCompact ← Array::view ← reshape ← expandAxis ← expandAxisKernel ←
  NodeVMSchedPass::evalNativeOperator` (`modules.onnx/export_grad_mean`, `pass_export_grad_mean`).
- **C — same rule, a different producer (five sites the nine never saw).** `tensor:iter` tagged a
  rank-1 input's rounds as `T[?]` and stored the element; when the input's shape is not fixed,
  `iterInfer` says the round is a tensor of unknown rank, so a loop over such a tensor reached
  the same frame. `gpu/profile/host_loop.{nvm,fvm,jit}`, `json.nvm`, `off.nvm` crash on
  `5dad9d646` and pass on `9670cd287` — they are only selected by the whole-tree gate, which is
  why that gate is now this lane's.

The fix is one rule at three places: **a value is handed on in the representation its declared
type asks for**. `wrap(type::Type *declared, Array *)` unwraps rank 0 only where the declared
type says the element and keeps the array under an array type (and keeps the array when no
result type is declared, which is what the index-evaluation harness reads); the tensor space
(`tensor:add`, `tensor:zeros_like`) is registered for the array code; `iterKernel` hands each
round on in the round type's representation. Two fusion sites pass `view.typeOf(node)`.

Repro commands, exit codes before → after (host, `$R=~/camel-work/h388-flip-staging`,
`CAMEL_HOME=$R/out/latest`):

| repro (all `std::fvm` unless said) | `5dad9d646` | `9670cd287` |
|---|---|---|
| `notes`' `min_a.cml` (`from_bytes` → `to_bytes`/`println`) | 139 | 0 |
| `notes`' `min_k.cml` (reduction axis from a literal `0`) | 139 | 0 |
| `notes`' `min_i1.cml` (`with_grad` of a `mean_axis`) | 139 | 0 |
| `notes`' `min_j3.cml` (vector @ vector) | 139 | 1 (`sum_to: cannot broadcast [2] to []`) |
| `test/cases/gpu/profile/host_loop.cml` | 139 | 0 |

Of the nine tests: **five pass** (`library_gemm.tolerance`, `.eight_threads`,
`binary_roundtrip`, `poison:modules.tensor:binary_roundtrip`, `pass_export_grad_mean`) and four
are still red **without crashing**, each on the rank-0 path below (`export_grad_mean`,
`export_grad_matmul_vector`, `pass_export_grad_matmul_vector`, `declaration_evaluator`).

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

## The `Tensor<void>` refusals: the refusal is gone, the gap is the element type (3 reds)

On `5dad9d646`, `train_step.loss.{nvm,fvm,jit}` (case
`examples/models/naive-in-expert-out/train_step.cml`) refused with `'the gradient kernels' does
not support a value of type 'Tensor<void>'; it takes f32 and f64 tensors`, raised at
`logits.pb`. On `9670cd287` **no failing entry names `Tensor<void>`** any more: the refusal is
gone and the three run further, to `train_step.cml:117:13`, where they fail with
`RuntimeDiag::RuntimeError: Expected Tensor argument` — raised by `tensorArg`
(`modules/tensor/ops/support.cpp:55`) because the argument's declared type is an array whose
element type is undetermined, which `isTensorType` rejects and the rank-0 scalar box does not
cover (`test/results/log/20261024_021832_2719996/fail/modules.naive_in_expert_out/train_step.loss.nvm/stdout.txt`).

That is the same gap the previous lane named, reached one step later: **a pullback result's
element type is undetermined where the ruling says a value's element type is always
determined**. The kernel's refusal was right; the flip's VJP typing is incomplete. Not the
rank-0 wall, not the `var Tensor` invariant: its own lever, 3 reds.

## The holes: 47 at case level, unchanged, and the list is longer than 22

The same tests name a `NoMatchingFunction` before and after, with the same operators: **47 of
the 454 failing entries** still name `SemanticDiag::NoMatchingFunction` in their captured
output (`grep -rl` over `test/results/log/20261024_021832_2719996/fail`), 49 by block before the
crash round. The class table's 7 is a block artefact (above). The 22 the previous lane gave a
module route are **all still holes, none changed**:

- `tensor:gt` — 8 (`boundaries.{agir,argir,tooltip_slots}`, `export_branch`, `ort_branch`,
  `ort_dynamic_batch`, `onnx.export_branch`, `poison:trans.annotated_dump:boundaries.agir`);
- `tensor:add` — 6 (`arms_laid_out_whole_tensor.*`, `generic.arms_laid_out_whole_tensor`);
- `tensor:sum`/`sum_axis` — 6 (`numeric_contract.{nvm,fvm,jit,prl,generic_unit}`,
  `declared_numeric_semantics`);
- `tensor:numel` — 1 (`native.tensor_wrong_arity`);
- `tensor:show` — 1 (`native.tensor_show_wrong_arity`).

The other 25 keep no module route, and several are *negative* tests whose golden is the
diagnostic itself (`modules.tensor.shape_types/{call,rank,propagated,unshaped}_argument`,
`native.tensor_wrong_arity`, `native.tensor_show_wrong_arity`, `alias.{err_var_rank,type_text}.nvm`,
`si_*`, `tensor.compound_assign.*`, `numeric_contract.*`, `declared_numeric_semantics`,
`module_import_builtins`, `lrit`, `mnist`, `static_types`, `onnx.export_branch`, `ort_*` and the
two poison aliases). In every case the value reaching the operator has element type `void` (an
unstated tensor leaf) or `bool`, and the operator's declaration is written over the migration
spelling `Tensor`.

## The internal errors are diagnostics; the crash nine is diagnosed and fixed

All 43 internal errors are compiler diagnostics, no signal among them, and all 43 carry the same
message: `camel: internal error: Runtime graph '…' breaks its edge invariants`, one root cause —
a variable or an argument whose type is a concrete numeric array (or a scalar) passed to a
parameter declared `var Tensor` or `Tensor`. By case: 34 in
`test/cases/modules/tensor/var_tensor_reference.cml` (the 33 `var_tensor.*` runs plus the poison
alias), 4 `var_tensor_read.*`, 2 `nn.lr` and `lr`, 1 `si_var_argument_plain`, and 2
(`grad_effects_kept.macro.simplify`, `grad_function_values_arms.macro.simplify`) surfaced as
`RuntimeDiag::PassInternalError` because a macro pass gave up on the same invariant. **40 of the
43 remain** after the crash round; the other three now fail at a later expectation
(`nn.lr`, `lr`, `si_var_argument_plain` → exit-code).

The crash nine, its two causes, its five extra sites and the fix are in **The crash round**
above. Their record: all nine ended `exit=None signal=SIGSEGV` on both the flip's baseline and
the integrated tree, over five cases (`compile/numeric/gemm_library.cml`,
`modules/tensor/binary_roundtrip.cml`, `modules/onnx/{export_grad_mean,export_grad_matmul_vector}.cml`,
`modules/tensor/index_semantics.cml`), and all nine pass on develop. Five of the nine pass on
`9670cd287`; four fail on the rank-0 wall below, without a signal.

## What has to be decided

Landing the flip now moves develop from 0 to 454 red, none of them a signal. The rest is known
and ranked, from the ledger above (gate: `node scripts/test.js test/plans --tier fast --jobs 8`,
5972 tests / 454 fail / 11 skip, `9670cd287`). The acceptance for the first item is the ruling's
own sentence: **rank 0 is the element type itself**.

- **The rank-0 representation first.** Three shapes, and the first is most of the lever (entry
  counts from `grep -rl` over `test/results/log/20261024_021832_2719996/fail`):
  - `sum_to: cannot broadcast [N] to []` — **158 of the 454 failing entries name it**, the wall
    the f64 arm exposed at the integration (153 of the 155 new exit-code reds). Repros: the
    `min_i2.cml` and `min_j1.cml` of the crash round, and the tests `export_grad_mean`,
    `export_grad_matmul_vector`. The same shape is why `matmul` refuses a rank-0 operand
    (4 entries, `grad_library*`) and why `gelu_grad`'s shape check fails (2).
  - `onnx::export_model`: "tensor:sum_to: the rank of an operand is not known statically" — 1
    entry, `pass_export_grad_matmul_vector`; the operand's rank is open where the export needs it
    fixed.
  - `declaration_evaluator`'s `EVAL_MISMATCH op=tensor:add (f32[], f32[]) … no instance for a
    call the kernel accepts` — 1 entry. The harness reports a rank-0 operand as the element
    (`f32[]` prints for rank 0) while the kernel boxes it, so the instance search
    (`sem::instantiate`, `include/camel/core/index_semantics.h:501`) finds none; that entry point
    is core, so this shape is a diagnosis for the core lane unless the harness can name the
    operand the way the kernel reads it.
- **Then the `var Tensor` edge invariant**: 40 reds, one root cause (a declared `Tensor`
  parameter against a concrete `f64[?, ?]` variable). A *different* lever: a compile-time
  invariant on a concrete value reaching a declared-unknown type, not value/type agreement at
  rank 0.
- **Then the 22 declarations** (`tensor:gt`, `:add`, `:sum`, `:numel`, `:show` written over
  `Tensor<T>` in the module): 47 failing entries name a `NoMatchingFunction` at case level, the
  22 among them.
- **Then the `Tensor<void>` VJP typing**: 3 reds, the `train_step.loss.*` family, whose element
  type is undetermined where the ruling says it is always determined.
- **Then the 75 print-form goldens** and the 5 new pins, as expectation updates that follow from
  the ruling that the printer prints by the type the value holds.
- **The nine SIGSEGVs are done** (`9670cd287`): zero signals, five of the nine tests pass, and
  the other four are on the rank-0 wall above. This lane's gate stays the whole tree, because
  five crash sites live outside smoke+feat.

Cost of landing now, in user-visible behaviour: `Tensor` is a numeric-array alias whose
unstated element type reads as f64, rank-0 values are the element type itself, tensor printing
is the array printer's, and `var Tensor` parameters produce a hard "defect of the compiler"
message. Recommendation: **do not land yet**; the integration has settled the element-type
question on the kernel side (`sum_to_f64` is now a real arm) and the crash round has taken the
signals to zero, so what is left is the rank-0 path and the items ranked above it, and one run
per slice measures the reds it moves.