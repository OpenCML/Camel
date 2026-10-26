# Full layer at `27ef118c4` (2026-10-27)

`-Plan all -Tier all -Jobs 8 -Check all` on `coord3-line` = `27ef118c4` (the author's line merged a second time,
the pass-parameter read landed, and the primer's two fixes taken at the author's own versions): `total 10176,
pass 10146, fail 24, skip 2, known 4`; the 8 static checks ran and none failed; the helper reports the plan
portion at 1647.3 s. The 10-26 run took 2875.8 s with two lanes gating beside it, and the layer has grown by 126
cases since.

## Against the 10-26 record

The same 24 rows and no new family, which is the useful reading: nothing this line merged since `e7a8b906d` adds a
red. The fast layer at this revision is green (`6931 / 6925 / 0 / 2 / 4`), so every row below sits outside the fast
layer by the tier it declares. Two rows moved the other way: of the four `small` entries, C6 (15 s) and D5 (17 s)
are green here, which is the load-sensitivity the 10-26 record warns about showing itself.

## The reds

| test (plan) | measured | attribution |
|---|---|---|
| `feat/opt/progen` `progen_check` | 32.95 s cpu of 150.00 s budget, 25.89 s wall | pre-existing: the 10-24 record has it red (82 wrong cells), and red at `07e81237f` |
| `feat/opt/progen` `progen_fixed` | 9.69 s | same family as `progen_check`, and not in the 10-24 record's short list |
| `feat/modules/autodiff` `benchmark_gradients_vs_torch` | 772.50 s cpu of 1600.00 s budget, 117.70 s wall | budget not the cause (under half); unattributed |
| `feat/modules/onnx` `pass_export_equals_export_model` | 3.34 s cpu of 75.00 s budget, 1.73 s wall | **not a margin: see below** |
| `feat/mm/gc` `gc.runtime_graph_constants.moving.nvm` / `.fvm` | 175.15 / 172.87 ms | unattributed (new since the 10-24 record) |
| `feat/mm/gc` `gc.macro_round_lifetime.moving_stress.nvm` / `.fvm` / `.jit` / `.prl` | 153.24 / 158.73 / 135.40 / 107.26 ms | unattributed (new since the 10-24 record) |
| `feat/compile/stream` `stream.tee_report` / `stream.tee_report_element_size_unknown` | 133.04 / 149.97 ms | unattributed (new since the 10-24 record) |
| `perf/startup` `startup.cost` | 10.64 s cpu of 13.00 s budget, 18.49 s wall | under budget but inside the margin rule's reach; the 10-24 record's whole `perf` group passed at a quieter host |
| `perf/training_steps` `mlp_step.nvm` / `.fvm` | 133.94 / 148.54 ms | **not a margin: a compile error, see below** |
| `perf/training_steps` `transformer_step.nvm` / `.profile` | 136.72 / 139.33 ms | **not a margin: the same compile error, see below** |
| `perf/gemm_threads` `curve` | 20.00 ms cpu of 14.00 s budget, 199.03 ms wall | budget not the cause; unattributed |
| `perf/gemm_canonical_shapes` `shapes` | 133.93 ms | unattributed (the two `feat` entries of that name ran green in the same period's fast layer) |
| `perf/reduce_layout` `reduce_layout.ops` | 50.00 ms cpu of 67.00 s budget, 242.17 ms wall | budget not the cause; unattributed |
| `perf/reduce_layout_counts` `reduce_layout.count` | 60.00 ms cpu of 15.00 s budget, 267.72 ms wall | budget not the cause; unattributed |
| `perf/showcase_c2` `small` | 860.00 ms cpu of 13.00 s budget, 855.48 ms wall | margin class (the bound is the case's own CPU time); load-sensitive |
| `perf/showcase_c1` `small` | 5.72 s cpu of 23.00 s budget, 9.14 s wall | margin class, same |
| `gpu/tiles_transfer` `device_parameter_tree_training` | 143.55 ms | pre-existing and independent of this line: the tiles lane reports it red on plain `develop` too (`device_tree.cml:11` `f32`/`float`) |

## Two rows taken out of the perf-margin class

`feat/modules/onnx` `pass_export_equals_export_model` was filed with the perf-margin family by the 10-24 record
("the margin rule reads CPU time and fails it"). This run reads otherwise: the measurement is under budget (3.34 s
cpu of 75.00 s) and the failure is a Python traceback, `onnxruntime` building a session for the model the *pass*
produced (`ort.InferenceSession(str(by_op))`, `test/cases/modules/onnx/pass_export.py:111`), on a model for which
`onnx.checker full_check` had just reported ok. It stands here as "the symptom does not match the 10-24
classification" and needs a second-look attribution, rather than the load caveat.

`perf/training_steps`' four rows are not margins either. Each fails in the schedule it drives:
`SemanticDiag::NoMatchingFunction: No matching function found for argument types: <> ({ w1: f32[784, 128], b1:
f32[128], w2: f32[128, 10], b2: f32[10] }, { w1: f32[..], b1: f32[..], w2: f32[..], b2: f32[..] }, OptimizerState,
float)` for `mlp_step`, the same shape with `g1/g2/wq/wk/wv/wo/...` for `transformer_step`; `transformer_step.profile`
then reports `stdout does not contain 'PROFILE camel'` / `'operators per step'` / `'tensor:matmul'` / `'wall per
step'` because the program never compiled, which is the "a message, not a margin" reading the 10-26 record gave
that arm, now with its cause named. The call passes a struct whose fields are concrete `f32[784, 128]`-style shapes
against a declaration writing `f32[..]`-style shapes, so this is a type-resolution defect and not a load artifact;
it is red in both records and still unattributed.

## What this run does not establish

- Attribution for the rows still marked unattributed, which needs the treatment the 10-24 record gave its rows
  (a quiet-host re-run, then a bisect against `origin/develop`). This run re-measured them and named their plans;
  the plan identities are in `docs/handover/batch-slow-2026-10-26.md`, "Plan identities".
- Whether the two rows the 10-26 record calls load-sensitive (`startup.cost`, the `small` margins) are only that:
  they are under budget in both runs and red by the margin rule in both.