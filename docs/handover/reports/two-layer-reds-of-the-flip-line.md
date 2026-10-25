# The flip line's reds, in both layers, at `tip=c5c506e44`

Readings only; no expectation, golden, tier or `known_fail` was changed to produce this page. The
fast layer is measured on this line; the slow layer was measured by the slow-layer measurement lane
at the same tip and the three entries marked below were re-run here twice.

## 1. Fast layer -- `fail 9`

`-Plan smoke,test/plans/feat -Tier fast -Jobs 8` at `tip=c5c506e44` reads
`total 6218, pass 6190, fail 9, skip 11, known failures: 8`.

| # | test | kind | owner |
|---|---|---|---|
| 1 | `lrit` | bare `Tensor` / unstated element type into `=` | open question, with the author |
| 2 | `nn.lrit` | same case through the `nn` screens plan | same |
| 3 | `mnist` | same family | same |
| 4 | `nn.mnist` | same family | same |
| 5 | `declaration_evaluator` | rank-0 array beside a float literal: kernel reads f64, the declaration evaluator f32 | open question (`rank-zero-array-and-a-float-literal.md`) |
| 6 | `print_and_recompile` | the value path of a `var` parameter | the printer half landed; this half is open |
| 7 | `specialize_growing_shape.fold_specialize.bounded` | `stats.graphs` reads 5 against a stale `> 5` floor | answered: the reading is the fix, the expectation is being re-recorded by its own lane |
| 8 | `profile.alloc_step` | allocator-call counts move when a type is interned | same |
| 9 | `fusion_group.profile.none` | its recorded report carries those counts | same |

Two of the previous tip's reds left without a new one: `export_fold` (10.03 s timeout with empty
stdout before, `PASS 343.89 ms` here, its onnx model written and parsed by the plan's own verify
script) and `rules.tensor_saturation_keeps_values` (`RewriteInvalid` before, `PASS 392.03 ms` here).

## 2. Slow layer -- 11

Whole-tree slow measurement at `tip=c5c506e44` (measurement lane), plus my own re-runs of the two
plans named below, twice each:

| # | test | where it comes from |
|---|---|---|
| 1 | `debugger.dap_protocol` | red on develop too |
| 2-4 | `poison:modules.rules:rules.library_verdicts_are_exactly_these`, `…reduced_element_type_instances_are_checked`, `…every_exported_rule_enters_the_check` | red on develop too |
| 5 | `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` | red on develop too |
| 6-8 | `poison:…nn:lrit`, `poison:…nn:mnist`, `poison:…index_semantics:declaration_evaluator` | the fast layer's own reds, seen again in the slow selection |
| 9 | `feat/trans/cpp :: agree_with_fvm` | **only this line** |
| 10-11 | `feat/mm/gc_pacing :: gc_pacing.training_400`, `.training_800` | **only this line** |

Two slow entries went green at this tip without a new one: `feat/modules/onnx :: pass_export_equals_export_model`
(two 120 s liveness kills and a cpu reading of 0 before; `PASS 25.50 s wall / 46.94 s cpu` here) and
`poison:…rules.rules.tensor_saturation_keeps_values` (`PASS 313.05 ms`).

## 3. The three that only this line has

**`feat/trans/cpp :: agree_with_fvm`** -- a real defect in the C++ backend, and **not** the tensor
alias flip's: three `int[]`-only programs are emitted with `var1.get()` on a plain
`rt::Array<int64_t>`, which has no such member, so they fail to build (112 of 115 programs still
agree; the interpreters are exact on all three backends). Two runs, `82.82 s` and `73.72 s`;
`test/plans/feat/trans/cpp.plan.toml:31`. Mechanism and the minimal difference are recorded in
`docs/handover/questions/cpp-backend-reads-a-non-escaping-var-through-get.md`: the declaration emits
a plain local, the read resolves to the append-in-place node whose frame entry dropped the
`Cell::Local` marking, and the emission differs from develop **only** by that suffix. It arrived with
the var-version machinery of the in-place planning merge on this line (`42d5ef8c0`, commit
`0b1be856d8`), which is not in `origin/develop` -- eleven other host trees were probed and only the
ones derived from this line emit any `.get()` at all.

**`feat/mm/gc_pacing :: gc_pacing.training_400` / `.training_800`** -- a real defect too, of the bare
`Tensor` family, and a compile-time refusal rather than a moving number: `gc_training_peak.cml:29`
calls `sgd(wf, g, 0.5)` with `(f32[?, 4], Tensor, float)`, and `nn:sgd` (its signature is printed
from `modules/nn/optim.cpp:567`) binds both `P` and `grad of P` to the first argument's type, so the
bare `Tensor` of the gradient does not unify with `f32[?, 4]`. Two runs each
(`188.00 ms`, `129.56 ms` and `180.22 ms`, `209.13 ms`); the same program with `steps=2 batch=64
width=64` prints its result line on develop (`tip=0a1a509f5`: `gc_training_peak base=18056 peak=28808
low=22664 released=0 loss=0.6931471805599647`) and is refused here. Recorded in
`docs/handover/questions/var-cell-widening-and-the-bare-tensor-return.md`, section 5: it is the
`lrit`/`mnist` family, so it is answered by whatever answers those four.

Neither of the three is host jitter (all of them red in two consecutive runs, with the failure text
of a compile, not of a measurement) and neither is a stale expectation (the expectations are `PASS`
strings and numeric bounds that no run reached).

## 4. Acceptance, and what this page did not do

The landing account stays "only fewer, never renamed, same targets": fast `fail 8` at the previous
tip to `fail 9` here is two greens and three entries that were not red before, and the three are the
two profile records and the stale graph floor that the author answered -- their own lane is
re-recording them, so this page leaves entries 7-9 exactly as they are. Nothing was weakened, no
`known_fail` was added or removed, no `tier` was moved, and `perf/**` and `feat/opt/progen` were not
run.