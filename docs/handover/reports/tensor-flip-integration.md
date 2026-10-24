# The tensor flip after integrating develop: 292 red, nine SIGSEGVs fixed, rank 0 done, the `var` cell widened, the element type stated at two entries

`origin/develop` (`ea624c980`) is merged into the flip's frozen tip (`c8e5e561d`) as
`5dad9d646`, a real merge commit. The flip's diff is untouched: no rebase, no rewrite.

**Read this note for the current state**: the ledger below has six measurement points — the
integration (`5dad9d646`, 494 red, nine SIGSEGVs), the crash round (`9670cd287`, 454 red, zero
signals), the first rank-0 slice (`1fc410091`, 372 red), the second (`4f9345c29`, 372 red, no test
moved), the `var`-cell widening (`332` red, the only slice that adds a case) and the unstated
element type (`292` red) — and the ranking at the end is written from the last one. Every number
says which target produced it:
`smoke,feat --tier fast --jobs 8` is what the integration round ran (through the remote helper's
`-Tier fast`); **`node scripts/test.js test/plans --tier fast --jobs 8` is this lane's gate from
now on**, whole tree, because five crash sites live in the `gpu.profile` plan, which smoke+feat
never selects. Rank 0 was this lane's largest lever and it is now spent: its remaining two shapes
are 2 tests and 1 test (the second is core's). The `var Tensor` edge invariant's 41 reds are spent
too — 40 green, and the one left is the `Tensor<void>` VJP typing item, not this root. The unstated
element type's two entries are spent as well: the class the next section defines is 66 reds by the
first diagnostic and 7 of them are left, 38 green. The next slice is the `(none)` family (191, of
which the print-form expectations are a part), after the 22 declarations the holes section lists
and the loop-pullback item the `Expected Tensor argument` section now narrows to 5.

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

## The measurements, all on the same host in the same window

| tree | command | tests | fail | skip | wall |
|---|---|---|---|---|---|
| flip tip `c8e5e561d` | `smoke,feat --tier fast --jobs 8` | 5900 | **489** | 14 | 194.1 s |
| integrated `5dad9d646` | same | 5966 | **494** | 11 | 223.0 s |
| `origin/develop` `ea624c980` | same (same window, `-NoBuild`) | 5956 | **0** | 11 | 172.6 s |
| integrated + crash round `9670cd287` | `node scripts/test.js test/plans --tier fast --jobs 8` | 5972 | **454** | 11 | 282.7 s |
| + rank-0 shape 1 `1fc410091` | same | 5972 | **372** | 11 | 291.8 s |
| + rank-0 shape 2 (a) `4f9345c29` | same | 5972 | **372** | 11 | 215.5 s |
| + `var`-cell widening (h395) | `node scripts/test.js test/plans --tier fast --jobs 8` | 5973 | **332** | 11 | 300.8 s |
| + unstated element type (h406) | `node scripts/test.js test/plans --tier fast --jobs 8` | 5973 | **292** | 11 | 363.1 s |

The last column is this slice: the same command, the same host, the same window as the row above
it. `5973` is `5972 + 1`: the slice adds one case, `shape_inference/var_argument_non_numeric.cml`,
which is the negative half of the fix and **passes** (it is the only test in the row that is new,
so the rows still compare test for test on the 5972). **40 reds green, 0 new red, 0 signals**
(`got null` occurrences: 0; set difference over `notes/h395-full-base.log` and
`notes/h395-full-final.log`). The baseline row was re-measured on this same host in this same
window (`h388-flip-staging`, `5972 / 372 / 11`, 248.6 s) rather than carried over, so the two
columns are one measurement pair.

The h406 row is the same pair again, one slice later and with no new case (`5973` on both sides):
**40 reds green, 0 new red, 0 signals**, by set difference over `notes/h406-base-reds.txt` and
`notes/h406-after-reds.txt` (the runner's own logs: `notes/h406-base-layer.log`,
`notes/h406-after-layer.log`). Its two commits are `743bde936` and `106dc54f2`; the section below
says which of the two each green came from.

## The `var`-cell widening (h395): which fix, and why this one

**The fix chosen: widen at the call site.** The caller's normalized argument cell is given the
declared parameter type when the parameter's content is the transitional unstated tensor
(`ArrayType::numericUnknown()`) and `assignableFrom` accepts the argument's value; everything else
is left to the invariant. The insertion point is the one the ruling pinned:
`src/compile/gir/builder_operators.cpp`, `Builder::visitLinkNode`, immediately after
`normInputTypes` is filled. The predicate is the safe rule the previous lane recorded, verbatim.

Why not the two alternatives:

- **Not in `TupleType::assignableFrom`** — the rule there (a cell's contents must be equal) is what
  makes a variable one thing to every reference to it; widening it would let two references to the
  same cell disagree with the cell's own operation types. It is untouched.
- **Not by rejecting the call** — the programs are develop's own and are green there, so refusing
  them would lose develop's behaviour. The one place where refusing is right is a cell the
  parameter genuinely cannot take, and that is now pinned by a test (below).

**What the argument actually is, measured.** The probe round that settled the guard found that
`normInputTypes[i]` holds the argument's *value* type, not its cell: for `var w: Tensor`'s
argument, `valueTypeOf(normInputTypes[0]) == normInputTypes[0]` and both print `f32[?, ?]`. The
first attempted version tested that value for being a cell, so it never fired (identical 37 reds,
twice). The version that lands reads the cell from the argument node (`nodeTypeOf`) and states the
widening on the node's type, keeping `normInputTypes[i]` as the parameter's content type so the
rest of the call binding sees the declared type. This is why the fix is at the call site and not
in the invariant: what disagreed was the spelling the caller carried, not the cell rule.

**The negative half.** `test/cases/modules/tensor/shape_inference/var_argument_non_numeric.cml`
passes a `string[]` variable to a `var Tensor` parameter. `Tensor` accepts numeric elements only,
so `assignableFrom` refuses the cell's content, the widening does not apply, and the call is
refused: `No matching function found for argument types: <> (string[], float)` at `17:17`, exit 1.
Pinned by `si_var_argument_non_numeric` in `shape_inference.plan.toml`. (It asserts the message and
the position rather than `expect_diagnostic_name`: the short error format puts the name and code
after the wrapped message, on a continuation line, where the runner's per-line diagnostic pattern
cannot see it.)

The 40 greens are the family and its neighbours: 32 `var_tensor.*`, all four `var_tensor_read.*`,
the poison alias `poison:linear.var_reference:var_tensor.default`, and three tests whose first wall
was this invariant (`lr`, `nn.lr`, `si_var_argument_plain` — the last moved from the internal error
to its own expectation, `ArgumentNeedsConversion`; the other two now pass). **Nothing else moved**:
`shape_inference` was measured separately on both trees and went 14 red → 13 (the one green is
`si_var_argument_plain`; `si_var_argument` is red on both, with the same `NoMatchingFunction` — it
asks for the `ArgumentNeedsConversion` diagnostic that its own test never produced).

**The one red left in the family is not this root**: `var_tensor_read.macro_simplify_jit`
(`std::macro std::opt std::jit`). On the flip tip it dies on this same edge invariant; with the
widening `std::opt` and `std::opt std::jit` pass, and under `std::macro std::opt` the optimizer
folds `sum(acc)` against the widened cell's unstated element type and reports
`sum: Unsupported Tensor dtype` (`RuntimeDiag::RuntimeError`) with the warning "This constant
expression faults whenever it is evaluated (RuntimeError); it is not folded". That is the
`Tensor<void>` VJP typing item already on the list below, reached by a new route — the cell now
says what the parameter says, and what the parameter says has no element type yet.
**Taken, and the route corrected, by the h406 slice** (its own section above): the expression is
`sum(vg.1.p0)` at `29:79`, the gradient of the `with` parameter, and what refused was the *clone of
the folded constant*, not the cell; the test is green there, and the item on the list below is 5
reds of the loop pullback, not this one.

Wall time moves with the host's load (another lane ran `test/plans/perf` in the same window);
the fail set is what is compared, never the clock.

The last two rows are the whole tree, not smoke+feat: they select six more tests than the row
above, `gpu.profile/{host_loop.nvm,host_loop.fvm,host_loop.jit,json.nvm,off.nvm}` and its
neighbour, and all six pass after the crash round and **all of them crash on `5dad9d646`**
(cause C below). So the rows compare test for test: the integration's **494** becomes **454**,
then **372** — forty reds green in the crash round and **82 more** in the first rank-0 slice,
none new either time, and the nine `expected exit 0, got null` entries are **zero** in both
(`notes/h388-fast-fixed2.log`, `notes/h388-fast-s1-clean.log`; case-level per-test output under
`test/results/log/20261024_021832_2719996/` and `…/20261024_024048_851820/`).

Logs: `notes/h388-fast-integrated.log`, `notes/h388-fast-develop-base.log`, the flip's own
`h366-flip7.log`, and the crash round's `notes/h388-fast-fixed2.log`; per-test class lists in
`notes/h388-{baseline,integrated}-reds.log`.

Develop is green in this window, so **not one red on the integrated tree is environmental or
develop's**. The three CUDA-device tests (`device_startup.available`, `device_startup.one_device_named_missing`,
`gpu_preference.named_device`) were *skipped* at the flip's baseline and *pass* on both trees
now: the cards were available, and there is no device red to explain away.

## The unstated element type (h406): 66 reds in class → 7, and `var_tensor_read.macro_simplify_jit` green

**The class, defined before the fix.** The first diagnostic in the failing block names the unstated
or unsupported element type that tensor machinery read: `Unsupported Tensor dtype`, `Unsupported
Tensor element type`, `Expected Tensor argument`, `Tensor argument cannot be null`. Classified by
`diagnostics[0].name` **and** the first `[Error]` line of the captured stdout, the same classifier
on both sides of the slice: **66 of the 332** (`notes/h406-base-first-diag.txt`, membership list
`notes/h406-base-voidtensor-class.txt`). It is the entry at both places this slice was pointed at —
constant folding and VJP typing — and the turn asked for, `var_tensor_read.macro_simplify_jit`, is
in it.

**Red before, verbatim.** `camel test/cases/modules/autodiff/var_tensor_read.cml std::macro std::opt
std::jit`:

```
(var_tensor_read)…/test/cases/modules/autodiff/var_tensor_read.cml:29:79: [Error]: An error has occurred: sum: Unsupported Tensor dtype. …
(var_tensor_read)…/test/cases/modules/autodiff/var_tensor_read.cml:29:79: [Warn]: This constant expression faults whenever it is evaluated (RuntimeError); it is not folded.
```

`29:79` is `sum(vg.1.p0)` — **the gradient of the `with` parameter, not `sum(acc)`**, which is what
the h395 section above says. The correction matters: the fault is in the VJP-typed constant that
`std::opt` tries to fold, not in the read of the `var` cell, and the cell's widening is why the
optimizer reaches it at all.

**Two entries, two commits.**

- `743bde936` — *a cloned static value keeps the element type its value has*
  (`src/runtime/draft_session.cpp`, `cloneStaticSlot`). A static value crossing into a session was
  cloned under the type its slot is declared with, and `Tensor` — the alias's transitional spelling
  — states no element type: its element is `Void`, the "numeric, not fixed yet" marker. The clone
  was therefore an array of slots with no element type, `Array::create` giving it eight bytes an
  element, and the bytes of a compact f32 value read back one element at a time through
  `get<slot_t>`. A kernel reading it by its dtype refused (`sum:`, then `matmul:`, `subtract:`,
  `pow:`), and a reader asking for the width of an element got one the value has not (`to_bytes`
  printed 8 bytes an f32 element). A compact array is pure data, so its clone now takes the element
  type and the extents the value has, through `Array::clone`, which reads both from the value; a
  slot array — a container of references — is still rebuilt element by element with the type each
  element states. This is the constant-folding entry: it is what a folded constant or a
  macro-expanded constant *is* once it lands in a session. Its greens are
  `var_tensor_read.macro_simplify_jit` and the three `tensor_grad_check` (`simplify.jit`,
  `simplify.fvm`, `simplified.nvm`); the clone alone — the probe build used to bisect the two
  causes — is also what let `grad_function_values_arms.macro.simplify` run to its end, where its
  numbers are right and it is still red on the print-form golden (`[0.7692267, …]` where the test
  wants `tensor([ 0.7692, …], dtype=float32)`): one of the 28 that walked out of the class.
- `106dc54f2` — *a tensor is one trainable leaf, not an array of its elements*
  (`modules/nn/optim.cpp`, `walk`). `walk` decided by the type's code alone whether a trainable leaf
  is an array to walk element by element, and after the flip a `Tensor` field carries the code of a
  native array too: a model's `Tensor` was taken for an array of tensors and the *elements of the
  leaf* were walked as if each were one. `walkArray` then built the result with
  `Array::create(mm::autoSpace(), length)` — the slot-array convenience, which states no element
  type — so `sgd`/`adam` returned a model whose next `matmul` refused with `Unsupported Tensor
  dtype` and whose `to_bytes` read 8 bytes an element. Probed directly: `model_loss<model>(x,
  target)` alone printed `0.6931…`, and the next line, `sgd(model, vg.1, 0.5)` followed by printing
  `stepped.head.weight`, printed `[null, null]`. The tensor test now comes first, the same question
  `saveLeaves` in that file already asks in that order, so a numeric array in either spelling
  (`Tensor`, `Tensor<f32, [4, 4]>`, `f32[m, n]`, `f32[]`) is one trainable leaf and only an array of
  non-numeric elements is walked element by element. This is the VJP/trainer entry: the value whose
  type states no element type is read by the trainer, and it left the trainer without one. Measured
  with the clone already in — the `optim` plan on its own and the nn+autodiff+examples plans, both
  of which kept every one of these red — this commit greened the `optim.*` native-leaf family (16),
  the `residual_mlp` family (6) and the `.opt`/`.devirtualize_inline` variants of `autoencoder`,
  `siamese_shared_encoder` and `tiny_rnn`. The rest of the `matmul` family (`gru_lite*`,
  `matrix_factorization`, `train_linear*`, `softmax_classifier*`, `autograd_linear_demo`,
  `graph_autograd_multi_param_demo`, `with_context_step_demo`) left the class the same way but not
  the red set: they run now and fail on a later expectation, which is the 28 in the arithmetic
  below.

**Green after.** `[SUMMARY] total: 5973, pass: 5670, fail: 292, skip: 11`,
`[SPLIT] build 23.9 s, test 339.2 s, total 363.1 s`, `RUN_EXIT=1`
(`notes/h406-after-layer.log`). **332 → 292: 40 greens, 0 new red, 0 signals**, by set difference
over `notes/h406-base-reds.txt` and `notes/h406-after-reds.txt`. The in-class greens (38) are the
whole `optim.*` native-leaf family (16, `trajectory_matches_native`, `training_loop_matches_native`,
`native_tensor_leaf_keeps_its_element_type`, `unstated_tensor_leaf_follows_its_value`, four backends
each), the `residual_mlp` family (6) and the `.opt`/`.devirtualize_inline` variants of `autoencoder`,
`siamese_shared_encoder` and `tiny_rnn` (9), `poison:modules.optim:optim.trajectory_matches_native.nvm`,
`06_loop_carried_state`, `export_train_step_adam`, `tensor_grad_check` (3) and
`var_tensor_read.macro_simplify_jit`. Two greens were outside the class:
`ort_gated_gru_step` and `ort_train_step_dynamic`, which had the same wall behind another first
diagnostic.

**The class arithmetic, exactly.** 66 → 7. Of the 66: **38 green**, **28 still red but out of the
class** — and 21 of those 28 now report **no diagnostic at all** in their first block, because the
wall is gone and what is left is a stdout or exit expectation. That is the `(none)` family growing
169 → 191 under the same first-diagnostic classifier (`RuntimeDiag::RuntimeError` falls 101 → 40).
`softmax_classifier` is the clean example: its four behavioural asserts (`before > 0`, `step ==
before` within 1e-9, `after < before`, `trained < after`) now hold, and it fails on
`match(/softmax_classifier weight=tensor/)` alone — the print form, which is the class the next
slice is pointed at, not this one.

**The 7 left, and what they are not.** Five are `Expected Tensor argument`
(`loop_grad_iterable_tensors.{nvm,fvm}`, `train_step.loss.{nvm,fvm,jit}`) and two are `Tensor
argument cannot be null` (`loops.fault_tensor_rank0.{nvm,fvm}`, a fault test red for its own
expectation). The five are **not** the element type, and this slice's evidence says so: both cases
fail on the same construct, `loop h = x for b in m.blocks` inside a differentiated function, and in
the loop case's `std::macro std::gir` the refusing operand is fully stated —
`tensor:zeros_like` takes a closure capture port of type `Block[]` and its result type is
`{ w: f32[], b: f32[] }[]`, so `tensorArg` refuses an array of records (not a tensor, and not a
scalar) rather than one whose element type is unstated. The item is the loop pullback's zero
tangent for an array of records. `train_step.loss`'s pullback is not in its graph at `std::macro`,
so that mechanism is verified for the loop case and only shared by construct and message for
`train_step`. The `Tensor<void>` section below reads these five as one element-type gap; the GIR
says otherwise, and the next lane should start from the loop, not from the spelling.

**Not done.** The ONNX shape-2 pair (`export_grad_matmul_vector`,
`pass_export_grad_matmul_vector`) — the "instantiate at the call site" item — is untouched: both
still fail, `onnx.export_model: tensor:matmul_grad_rhs: the rank of an op …` and its
`TranslationRefused` twin, the same first diagnostic as before. It is a type-system change whose
acceptance is a whole-feature criterion (the export back to develop's behaviour, the 38 `var
Tensor` cases green, no `f32[..]` site left in the export), not a patch this slice could land
alongside its own two, so it is left for its own lane. Also untouched: the 22 declarations the
holes section lists, and the print-form class the `(none)` family now hides.

**One ledger correction.** This section's predecessor says the optimizer "folds `sum(acc)` against
the widened cell's unstated element type": the expression is `sum(vg.1.p0)`, at `29:79`, and it is
the gradient of the `with` parameter, as the verbatim block above shows.

## Every red, by class, before → after

Bucketing is the attribution report's, applied by one classifier to both logs: the first
diagnostic in the block, with the diagnostic classes taking precedence over the runner's text.

| class | flip tip | integrated | + crash round | delta, integrated → now | h395, `var` cell widened | delta, now → h395 | h406, unstated element type | delta, h395 → h406 |
|---|---|---|---|---|---|---|---|---|
| kernel element-type refusal | 162 | 3 | 0 | 0 | 0 | 0 | — | — |
| `NoMatchingFunction` hole | 49 | 49 | 7 | 7 | 7 | 0 | — | — |
| internal error | 43 | 43 | 40 | 38 | 38 | 0 | — | — |
| exit-code catch-all | 88 | 243 | 250 | 119 | 119 | 0 | — | — |
| print form | 59 | 63 | 75 | 92 | 92 | 0 | — | — |
| runtime error | 22 | 26 | 15 | 32 | 32 | 0 | — | — |
| other text | 66 | 67 | 67 | 84 | 84 | 0 | — | — |
| **total** | **489** | **494** | **454** | **372** | **332** | **−40** | **292** | **−40** |
| of which `expected exit N, got null` (a signal) | — | **9** | **0** | **0** | **0** | **0** | **0** | **0** |

**The h406 column's class rows are left as `—` on purpose.** The bucketing is the attribution
report's classifier applied to a log; the h395 column carries the previous slice's figures because
its note says so, and this slice measured its own class with a different classifier (the first
diagnostic naming the unstated or unsupported element type the tensor machinery read: 66 reds → 7,
in the section above). Putting that classifier's numbers into these rows would misreport the
method, and carrying the previous column's figures here would report a measurement nobody made, so
the rows are `—`. The two numbers in the column that *are* this slice's own measurement are the
total and the delta beside it. What moved is exact and is in the section above — **40 greens, 0 new
red, 0 signals**, by set difference over the two logs.

**The h395 column is the measured total; its class buckets are deliberately not filled in.** The
bucketing is the attribution report's classifier applied to a log, and putting a second
classifier's numbers into the author's rows would misreport the method. What moved is known exactly
and is in the section below — **40 greens, 0 new red, 0 signals**, by set difference over the two
logs — so the class rows carry the previous slice's figures with a zero delta rather than a guess.

The last two columns are the two rank-0 slices; shape 2 (a) moved **no** test, which is the
point of its entry below: it changes what the exporter can express, not what the suite measures.
Its red set is the shape-1 one exactly — `green=0, new=0`, signals 0, verified by set difference
over the two runs (`notes/h388-fast-s1-clean.log`, `notes/h388-fast-s2-clean.log`).

**Read the `NoMatchingFunction` row with care**: 49 → 7 is a property of the *block* the
classifier reads, not of the flip. The hole is still there in every one of those cases — running
`test/cases/trans/annotated/boundaries.cml std::agir` on `9670cd287` still prints
`error[SemanticDiag::NoMatchingFunction]: … operator '>' is not defined for these operand
types`, exit 1 — and at case level **47 of the 372 failing entries still name a
`SemanticDiag::NoMatchingFunction`** in their captured output (47 of 454 before this slice, 49
before the crash round, by block). What moved is which part of a failing snapshot diff lands
inside the block. Other reds moved class the same way, twice over: a crash or an earlier wall no
longer stops them first, so they now report the next expectation that fails. They are still red;
the numbers above count them once each.

The transition from the integration to now is otherwise exact: **122 reds green, none new**, and
the green ones are the crash round's families — the nine signal tests, the `tiles.*.source`
gradient lowering (9), the `export_train_step` family (15), `grad_through_as.*.nvm` (3),
`train_step*` (4) — plus the first rank-0 slice's 82, listed below.

## The rank-0 slice, shape 1 (`1fc410091`): 82 reds, and the wall's size

The acceptance is the ruling's sentence — rank 0 is the element type itself — and the first shape
was the wall the f64 arm uncovered: `sum_to: cannot broadcast [N] to []`, which **158 of the 454
failing entries named** (and with it `matmul does not accept rank-0 tensors`, 4, and
`gelu_grad`'s shape check, 2).

The cause was in the *backward* graph's types, not in `sum_to`: `statesTensorElementType`
(`modules/tensor/ops/vjp.cpp`) accepted any type whose innermost element is a numeric scalar, and
a scalar's innermost element is itself, so `float64` passed. `meanVjp` scales the gradient by the
count through `:op/div_d`, which answers `float64`, and names that node among the inputs its new
node is typed from — so both of `multiplyVjp`'s products were declared **`float64`, a rank-zero
scalar, while their value was the `[3, 2]` tensor**. `sum_to` reads its operand by that declared
type, took it for a scalar tensor, boxed what it read as one number, and refused: the shapes in
the message are the like's `[3, 2]` against the boxed `[]` — the gradient was never the wrong
shape, its type was.

The predicate now requires an array — at a rank, or with none fixed (a bare `Tensor`) — and falls
through to the undetermined array otherwise, which every tensor kernel reads as a tensor. The
scaled value is untouched: the count division stays in f64 and `broadcast_like` fills at the
tensor's own element type, so a reduction still accumulates in f64 and rounds once
(reduced-precision ruling (d)).

Measured after the slice, on the same run and by the same `grep -rl` over `…/fail`:

| lever | before | after |
|---|---|---|
| `sum_to: cannot broadcast [N] to []` | 158 entries | **0** |
| `matmul does not accept rank-0 tensors` | 4 | **0** |
| `gelu_grad` shape check | 2 | **0** |
| `tensor:sum_to: the rank of an operand is not known statically` (ONNX export) | 1 | 2 |
| `EVAL_MISMATCH` (declaration evaluator) | 1 | 1 |
| `Expected Tensor argument` (`Tensor<void>` VJP typing) | 3 | 5 |
| `breaks its edge invariants` (`var Tensor`) | 43 | 41 |

Repro exit codes, before → after (`std::fvm`, `$R=~/camel-work/h388-flip-staging`): the crash
round's `min_i2.cml` (mean over all), `min_i3.cml` (sum over all), `min_j1.cml` (matmul @ vector),
`min_j3.cml` (vector @ vector) and `min_i1.cml` (mean_axis with a gradient) were `1, 1, 1, 1, 0`
with the wall named and are **`0`** now.

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

## The `Tensor<void>` refusals: the refusal is gone, the gap is the element type (5 reds now)

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

## The `var Tensor` family (38 reds): the cell rule is strict, and the two spellings are the defect

**38 tests** fail with one message, counted by test name over the shape-2 run
(`notes/h388-fast-s2-clean.log`), and every one of them is the same pair of types:

```
camel: internal error: Runtime graph 'main' breaks its edge invariants (2 violation(s)):
  graph 'main' node 5 (10): passes a var f32[?, ?] as ordinary argument 0 of 'step', which takes a var Tensor
  graph 'main' node 9 (10): passes a var f32[?, ?] as ordinary argument 0 of 'twice', which takes a var Tensor
```

Repro: `test/cases/modules/autodiff/var_tensor_read.cml` (`main` has `var acc = reshape([0, 0], [2])`
and calls `loss(x: Tensor, var acc: Tensor)`), plus `var_tensor*.{default,fvm,jit,opt,inline,cse,dce,fold,…}`
and `poison:linear.var_reference:var_tensor.default` — one family, 38 entries.

**The rule that fires** is deliberate and correct: `ArrayType::assignableFrom` accepts this pair
(`numericUnknown()` takes any numeric element, `src/core/type/composite/array.cpp`), but the
argument is a **cell**, and `TupleType::assignableFrom` refuses anything but equality for a cell:

```cpp
if (cell_) {
    // A cell is written through every reference to it: the content types must be equal.
    return typesPtr_[0]->equals(other.typesPtr_[0]);
}
```
(`src/core/type/composite/tuple.cpp`.) The reason is sound: a `var` parameter may **write** any
`Tensor` through the caller's cell, so a caller whose variable is `f32[?, ?]` would end up holding
something outside its own declared type. Relaxing the invariant would hide exactly that.

**So the defect is the spelling, not the check**: the variable's cell holds `f32[?, ?]` (an array
whose element is stated and whose extents are not) and the parameter's cell holds `Tensor`
(`ArrayType::numericUnknown()`, element unstated, rank open). On develop these are one type —
`reshape` answers a `TensorType` and so does the parameter — so the cell contents are equal and
all 38 are green. The flip spells "a tensor whose shape is not known" two ways in two places, and
the cell equality rule cannot see them as one.

**Chosen: widen at the call** (ruled next round; recorded here so the choice is not re-argued).
Reason: (i) the acceptance settles it — 38 programs develop compiles and runs must keep doing so,
so rejecting the call is out on the evidence alone; (ii) it is sound rather than a relaxation —
the cell invariant says a cell is written through every reference to it, so the cell must admit
every value the callee may store, and the declared parameter type *is* that bound; (iii) if the
author rules instantiation for (b), this is the same rule seen from the caller's side, and the two
mechanisms converge instead of being built twice. Two conditions on it: `TupleType::assignableFrom`
stays **exactly as it is**, and a cell whose element type genuinely cannot be written by the callee
must still be refused — the widening must not swallow a non-numeric-to-numeric mismatch.

**Insertion point, pinned** (so the next attempt does not re-search): call arguments go through
`Builder::coerceTo` (`src/compile/gir/builder_control.cpp`), reached from the call path whose own
comment says "A value the type cannot hold is reported by coerceTo" (`builder_calls.cpp`), and a
**cell** argument is where the declared parameter type has to be taken. Safe rule to implement:
ascribe the caller's cell to the parameter's type only when the parameter's content is the
transitional unstated tensor (`ArrayType::numericUnknown()`) and `assignableFrom` accepts the
argument — which by construction refuses a non-numeric element, the second condition above — and
leave everything else to the invariant, which keeps firing for genuine mismatches.

**Where the fix belongs**: make the two spellings agree at the call — in the compiler's
`var`-argument path (`src/compile/gir/**`), where the caller's cell should take the declared
parameter type at the call — **not** in the invariant, and **not** by rejecting the call: the
programs are develop's own and are green there, so the acceptance "develop's behaviour is not
lost" rules out turning them into compile errors. **This family and the ONNX shape-2 pair share
one root** — a declared `Tensor` spelling living beside the actual spelling of the same value —
which is why the `var Tensor` slice is also evidence for the (b) question
(`docs/handover/questions/bare-tensor-parameter-instantiation.md`): the exporter meets the same
disagreement as data, this family meets it as a cell.

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

**h395 closes them** (and with them the invariant): the same log's `breaks its edge invariants`
occurrences go **79 → 0**, and 0 of the 332 failing entries in the final run name it. The three that
had already moved at the crash round (`nn.lr`, `lr`, `si_var_argument_plain`) now **pass** — they
were the invariant's last wall before their own expectations. The two `…macro.simplify`
PassInternalError entries are not in this set; they were gone by the previous slice. What is left of
the family is one test, `var_tensor_read.macro_simplify_jit`, and it is the `Tensor<void>` VJP
typing item, not this root: `std::opt` and `std::opt std::jit` pass, and `std::macro std::opt` folds
`sum(acc)` against the widened cell's unstated element type (`sum: Unsupported Tensor dtype`).

The crash nine, its two causes, its five extra sites and the fix are in **The crash round**
above. Their record: all nine ended `exit=None signal=SIGSEGV` on both the flip's baseline and
the integrated tree, over five cases (`compile/numeric/gemm_library.cml`,
`modules/tensor/binary_roundtrip.cml`, `modules/onnx/{export_grad_mean,export_grad_matmul_vector}.cml`,
`modules/tensor/index_semantics.cml`), and all nine pass on develop. Five of the nine pass on
`9670cd287`; four fail on the rank-0 wall below, without a signal.

## What has to be decided

Landing the flip now moves develop from 0 to 332 red, none of them a signal. The rest is known
and ranked, from the ledger above (gate: `node scripts/test.js test/plans --tier fast --jobs 8`,
5973 tests / 332 fail / 11 skip, `h395`). The acceptance for the first item is the ruling's
own sentence: **rank 0 is the element type itself**.

- **The rank-0 representation.** Three shapes, the first done (entry counts from `grep -rl` over
  the run's `fail/`):
  - **Done** (`1fc410091`, 82 reds): `sum_to: cannot broadcast [N] to []` — 158 entries, the wall
    the f64 arm exposed at the integration, and with it `matmul`'s rank-0 refusal (4) and
    `gelu_grad`'s shape check (2). A gradient node of a tensor was declared with a *scalar* type,
    so `sum_to` read the tensor as a number. See the slice's own section above.
  - **Next**: `onnx::export_model`: "tensor:sum_to/g: the rank of an operand is not known
    statically" — **2 entries** (`export_grad_matmul_vector`, `pass_export_grad_matmul_vector`),
    and the two are the whole family: of the six failing tests that name ONNX, the other four
    (`export_branch`, `export_train_step_adam`, `export_loop_carried_array_field`,
    `export_loop_values_array`) fail at an exit code or in their own verification, not on this
    refusal. **Measured, and it is not where the ranking assumed.** The refusal names `/g`, the
    *gradient* operand, so the emitted node's type is `f32[..]` — element stated, **rank open**.
    A temporary print in `gradientTypeFor` (`modules/tensor/ops/vjp.cpp`, reverted) shows why no
    VJP rule can do better: **every** primal the rules are handed in that case is
    `array rank=0 open elem=5` (`ArrayType::numericUnknown()`, the bare `Tensor`) and
    `statesTensorElementType` is false for all of them — 30 sites on the runtime export path, 30
    on the pass path, and the same in the small `min_i2.cml`. The rank is not in the declared
    types at all: `loss`/`step` declare `Tensor` parameters, so the whole backward graph is
    open-rank, and copying the primal's type cannot recover a rank that was never written. The
    develop comparison says the same thing from the other side: `~/camel-work/Camel` on `develop`
    exports this case **green on both paths**, and at the same pipeline point (`std::macro
    std::opt std::gir`) develop labels those nodes `value_type="Tensor<float32>"` where the flip
    labels them `f32[..]`. The flip's own `tensorFactsOf` is where the exporter loses the shape:
    for an array whose rank is open it states no shape at all (`modules/tensor/type.cpp`, "an
    array whose rank is open states no shape at all"), and `staticShape` in
    `modules/onnx/lowering_grad.cpp` throws on exactly that. Whether develop's `Tensor<f32>`
    answered a rank or its lowering asked for one, develop's exporter is not stopped here; the
    flip's is, on 2 tests. So shape 2 has two loci, neither of them the pullback's typing: the
    export must lower a `sum_to` whose rank is not stated (dynamic `Shape`/`Range` axes in
    `modules/onnx/lowering_grad.cpp`, the flip's own module and in lane), or the generic body
    must be instantiated per call site so `Tensor` parameters take their arguments' ranks
    (compiler/core, not this lane). Two entries, so this is **not** a family that changes the
    order — it is a correctness question about what an exported model may assume.
    **Locus (a) is taken** (`4f9345c29`): `sum_to` now computes its axes in the model —
    `Shape` of both operands, the leading `rank(g) - rank(like)` axes, and every aligned axis
    `like` has as 1 and `g` does not (`Equal`/`Not`/`And`/`NonZero`), handed to `ReduceSum` as an
    input with `noop_with_empty_axes` (opset 13 on). It is the discipline `shapeOperand` already
    applies to an open extent. Measured effect on the two tests: the refusal moves from
    `tensor:sum_to/g` to **`tensor:matmul_grad_rhs`** on both paths, and **neither test turns
    green** — because the case's own verification counts `Squeeze >= 2 && Unsqueeze >= 4`, the
    *static* vector widening and narrowing, which an unstated rank cannot produce. Eight
    lowerings in that file ask for a stated rank; `sum_to` was one. So locus (b) is what these
    two tests need, and the acceptance for them — **"develop's behaviour is not lost", measured:
    develop exports this case green on both paths** — is *not* met by (a) alone. Whether a bare
    `Tensor` parameter must be instantiated per call site is asked, with these measurements, in
    `docs/handover/questions/bare-tensor-parameter-instantiation.md`; if the author rules yes,
    (a) is a compatibility path rather than the fix, and this entry says so either way.
    **(a) stays on this branch, unmerged, deliberately** — its own admission is the reason: no
    measurement reaches it while `matmul_grad_rhs` refuses, and code no measurement reaches is
    not what this project puts on develop. It is committed and safe here, it rides with the
    flip's landing, and (b)'s ruling decides whether it is the fix or a compatibility path. It is
    not forgotten code; it is code waiting for its precondition. The question itself was
    cherry-picked onto develop (`8d9095f5b`) so the author can see it — a question living only on
    an unmerged branch is invisible to him.

- **The rank-0 chapter, closed, and what waits on (b)**: `modules/onnx/lowering_grad.cpp` has
  **eight** lowerings that require a stated rank of an operand (`sum_to`, `expand_axis`, the two
  `matmul_grad_*`, the two conv gradients, and two more), and `sum_to` is now the one that no
  longer does. Eight, of which one is dynamic, is how much of the ONNX gradient path is waiting
  on the (b) ruling rather than on more engineering.
  - **Then**: `declaration_evaluator`'s `EVAL_MISMATCH op=tensor:add (f32[], f32[]) … no instance
    for a call the kernel accepts` — 1 entry. The harness reports a rank-0 operand as the element
    (`f32[]` prints for rank 0) while the kernel boxes it, so the instance search
    (`sem::instantiate`, `include/camel/core/index_semantics.h:501`) finds none; that entry point
    is core, so this shape is a diagnosis for the core lane unless the harness can name the
    operand the way the kernel reads it.
- **The `var Tensor` edge invariant — done** (`h395`): 41 reds, one root cause (a declared `Tensor`
  parameter against a concrete `f64[?, ?]` variable). A *different* lever: a compile-time invariant
  on a concrete value reaching a declared-unknown type, not value/type agreement at rank 0. Taken
  at the call site (the caller's cell is given the declared parameter type where the parameter's
  content is the unstated tensor and `assignableFrom` accepts the argument's value), never in
  `TupleType::assignableFrom` and never by rejecting the call. **Measured: 40 green, 0 new, 0
  signals**, whole tree, same window; the one red left of the family
  (`var_tensor_read.macro_simplify_jit`) was the entry the h406 slice took, and it is **green**
  there — the fold was refused by the *cloned constant's* missing element type, not by this root.
  The refusal that is genuinely right — a non-numeric cell against `var Tensor` — is pinned by
  `shape_inference/var_argument_non_numeric.cml`.
- **The unstated element type — the two entries taken, the class measured** (`h406`, 292 red): a
  static value cloned under a declared `Tensor` lost its element type on the way into a session
  (`743bde936`), and the trainer walked a `Tensor` leaf as an array of its elements (`106dc54f2`).
  **Measured: 40 green, 0 new, 0 signals**, and the class the first diagnostic defines — the
  unstated or unsupported element type read by tensor machinery — goes **66 → 7**, 38 of it green
  and 21 more reporting a later expectation. It was the last wall in front of the whole `optim.*`
  native-leaf family, the `residual_mlp` family and the `.opt` variants of `autoencoder`,
  `siamese_shared_encoder` and `tiny_rnn`. The 7 left are the loop-pullback item below.
- **Then the 22 declarations** (`tensor:gt`, `:add`, `:sum`, `:numel`, `:show` written over
  `Tensor<T>` in the module): 47 failing entries name a `NoMatchingFunction` at case level, the
  22 among them, and the count has not moved across either slice.
- **Then the `Tensor<void>` VJP typing**: 5 reds, the `train_step.loss.*` family and
  `loop_grad_iterable_tensors.*`. The h406 slice's GIR narrows the item: the refusing operand is
  **not** an undetermined element type but a fully stated array of records — `tensor:zeros_like`
  over a `Block[]` capture port, in the loop pullback — so the item is the zero tangent of an array
  of records, and the element-type spelling of the message is a coincidence of `tensorArg`'s two
  refusals. Start from the loop, not from the spelling.
- **Then the 92 print-form goldens** and the 5 new pins, as expectation updates that follow from
  the ruling that the printer prints by the type the value holds. This is the class that grew
  most as the walls came down (63 at the integration, 92 now), so it is the *last* item only
  because the ruling that fixes it is the author's to make.
- **The nine SIGSEGVs are done** (`9670cd287`): zero signals, five of the nine tests pass, and
  the other four are on the rank-0 shapes above. This lane's gate stays the whole tree, because
  five crash sites live outside smoke+feat.

Cost of landing now, in user-visible behaviour: `Tensor` is a numeric-array alias whose
unstated element type reads as f64, rank-0 values are the element type itself, tensor printing
is the array printer's, and `var Tensor` parameters no longer produce a hard "defect of the
compiler" message (40 reds green, 0 new, 0 signals). Recommendation: **do not land yet**; the
integration has settled the element-type question on the kernel side (`sum_to_f64` is now a real
arm), the crash round has taken the signals to zero, the first rank-0 shape has taken 82 reds off
the wall, the `var`-cell widening 40 more and the unstated element type 40 more again (its class
66 → 7), so what is left is rank 0's other two shapes, the loop pullback's 5 and the items ranked
above them, and one run per slice measures the reds it moves.