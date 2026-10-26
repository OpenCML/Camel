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
are green here, which is the load-sensitivity the 10-26 record warns about showing itself - those two compare numbers
against their PyTorch counterparts, and the second-look reading below puts no other row in this table in that class.

## The reds

The `attribution` column's "unattributed" entries are settled family by family at the end of this record, by dating the
symptom against the history rather than against the host.

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

## What each row actually says (second-look reading of the same revision)

Each row was then re-run on its own plan with the failure text kept, because a row that shows only a time is a row that
can hide. What follows moves all but one of them out of the "the host was loaded" explanation.

- `progen_check` / `progen_fixed`: `expected exit 0, got 1`, `stdout does not contain '0 wrong'` / `'PASS'` - the progen
  check itself fails (the 10-24 record counts 82 wrong cells), which is a defect and not a budget.
- `benchmark_gradients_vs_torch`: `expected exit 0, got 1`, missing `'== transformer_big (camel_opt_nvm)'` and
  `'gradient check: PASS'` - the gradient check fails; its 758.15 s cpu is under half the budget and beside the point.
- `pass_export_equals_export_model`: `expected exit 0, got 1`, missing `'onnx::export equiv: PASS'` - the same failure
  the row was taken out of the perf family for.
- `gc.runtime_graph_constants.moving.nvm` / `.fvm`: exit 0, but the pinned numbers are not printed
  (`loss=0.3579342999437358 gqw=0.19846567139029503 gkw=0.0764192040078342` and the `loss2` / `gqw2` / `gkw2` line) -
  a value difference, not a time one.
- `gc.macro_round_lifetime.moving_stress.*`: `expected exit 0, got 1`, missing `'-12.0 -12.0 24.0'` and
  `'[2, 3, 2] 12.0 120.0'`.
- `stream.tee_report` / `stream.tee_report_element_size_unknown`: exit 0, but the report lines are not printed
  (`STREAM_TEE {"tee":1,"elem":"int","elements":20,"peak":20,...}` and the `"elem":"string"` one) - a report-shape
  difference.
- `perf/training_steps`' four rows: `NoMatchingFunction` at the optimizer step, in the section above.
- `curve`: `expected exit 0, got 1` and `verification output: Error: expected 84 points (6 thread counts x 7 shapes x 2
  paths), got 0` - the benchmark produced no points at all.
- `shapes`: `expected exit 0, got 1` and `verification output: Error: expected 3 reference rounds, got 0`.
- `reduce_layout.ops` / `.count`: `expected exit 0, got 1`, missing `'reduce layout ops: PASS'`, `'sum_to'`, `'ns/call'`,
  and `'reduce layout counts: PASS'`.
- `perf/showcase_c1` / `perf/showcase_c2` `small`: `expected exit 0, got 1`, missing `'c1 compare: PASS'` /
  `'c2 compare: PASS'` - the case's own comparison bound, with its CPU reading (5.27 s of 23 s, 1.14 s of 13 s) inside
  the plan's budget.
- `device_parameter_tree_training`: `expected exit 0, got 1`, pre-existing and red on plain `develop` per the tiles lane.
- `startup.cost` is the one row that reads as a bound: `expected exit 0, got 1` with no missing string, so the case's
  own cost check is what failed, and the 10-26 record's CPU-time-margin caveat belongs to it alone.

## Per-family bisect: the symptom, the introducing commit, the owner

Each family was dated against the history rather than against the clock. The window opens at `bd20f2200` (the 10-24
batch point) and closes at `e7a8b906d` (the 10-26 record), and the two records bracket every family: each one below is
outside the 10-24 record's red list and inside the 10-26 record's, except where a cell says otherwise. The introducing
commit is then named by pickaxe (`git log -S` over `test/plans`, plus the path history of the case and of the engine
files the symptom runs through), so each answer is a commit and not a range.

| family | symptom, verbatim | introduced by | owner |
|---|---|---|---|
| ① `progen_check` / `progen_fixed` | `expected exit 0, got 1`, `stdout does not contain '0 wrong'` / `'PASS'`, and 82 wrong cells of 1025, each `FRONTEND: No matching function found` | **earlier than the window**: `progen_check` is red at `07e81237f` (2026-10-23 23:57), the 10-24 record's own baseline. `progen_fixed`'s entry predates `bd20f2200` and is not in that record's short list, so it turned red inside `bd20f2200..e7a8b906d` | 待派 - a front-end resolution defect, older than every merge this line made |
| ② `benchmark_gradients_vs_torch` | `expected exit 0, got 1`, missing `'== transformer_big (camel_opt_nvm)'` and `'gradient check: PASS'`; the run stops in `benchmarks/train.py`, and the program's own text is `model.cml:45:48: [Error]: Invalid call to '__mul__' with argument types <> (f32[..], float): the scalar operand has type 'float', but the tensor's element type is 'f32'` | `f0cfc58cd` "State the element type in the benchmark models and probes" (10-2x), the benchmark member of the same spelling wave; the entry predates `bd20f2200` and the gradient check itself was green there | 作者侧 |
| ③ `pass_export_equals_export_model` | `expected exit 0, got 1`, missing `'onnx::export equiv: PASS'`; `onnxruntime` fails to build a session for a model `checker full_check` calls ok (`test/cases/modules/onnx/pass_export.py:111`) | the case is **untouched** since `bd20f2200` (0 lines of diff), so the model the pass emits changed under it; the exporter's own history in the window is the author's `with`-arity and declaration work (`42eb50c99`, `487ad3ea9`, `b8c0090b1`, `7b8021245`, `0fdf2049c`, and the `wip:` `08d1e5377` beside them) | 作者侧 |
| ④ `gc.runtime_graph_constants.moving.nvm` / `.fvm` | exit 0, but `loss=0.3579342999437358 gqw=0.19846567139029503 gkw=0.0764192040078342` and its `loss2` line are not printed | the cases were rewritten line for line (23 insertions, 23 deletions) in the element-type spelling wave: `e0c28d887` (10-25), `1757fe3dd` (10-26), `3602386ea` (10-26), while the plans' expectations did not move | 作者侧 |
| ⑤ `stream.tee_report` / `_element_size_unknown` | the three `STREAM_TEE {"tee":1,...}` lines are absent while the session's own `PROFILE camel` and its `streams - one line per tee` header print | `f1fa2bd23` (2026-10-26, the tee feature and its report) with `b10442bc2` (its case expectations); the tee engine files' history in the window has no later commit and no merge | 作者侧 |
| ⑥ `curve` / `shapes` | `expected exit 0, got 1` with `verification output: Error: expected 84 points (6 thread counts x 7 shapes x 2 paths), got 0` and `Error: expected 3 reference rounds, got 0`; `NoMatchingFunction` for `<tensor:add>: (lhs: float[..] \| number, rhs: float[..] \| number) => float[..]` | the two cases were rewritten by `1fadc2f9b` (10-25) and `3602386ea` (10-26) and by nothing else | 作者侧 |
| ⑦ `reduce_layout.ops` / `.count` | `expected exit 0, got 1`, the generated probe failing `Return type 'f32' does not match the function's declared return type 'float'` at `probe_sum_to_leading_profile_2048.cml:11:5` | `test/cases/perf/reduce_layout.py` was rewritten by exactly one commit, `3602386ea` (10-26) | 作者侧 |
| ⑧ `perf/showcase_c1` / `c2` `small` | `expected exit 0, got 1`, missing `'c1 compare: PASS'` / `'c2 compare: PASS'`; `train.cml:38:5 Return type 'f32' does not match ... 'float'` and `NoMatchingFunction ... (Mlp, f32[..], int[..])` | the element-type spelling in the four showcase training programs came from one commit, `ccd38065c` (2026-10-25) | 作者侧 |
| ⑨ `startup.cost` | `expected exit 0, got 1` with **no** missing string, while the case's own report reads `declaration.cost declarations=371 per_declaration_us=41.3 ... total_ms=15.3 total_ceiling=12 ms result=FAIL` at `host load=83.7/71.3/72.5` | **no introducing commit**: the check, its ceiling and its string all predate `bd20f2200`, and the window holds no commit that touches `declaration.cost` or `total_ceiling`. It is a bound row: 371 declarations at 41.3 us each, against a 12 ms total ceiling and a 60 us per-declaration ceiling it passes | 待派 - needs a quiet host, and the declaration count joined the perf-gate lane |

Four families - ④⑥⑦⑧ - are one root, and ② is its benchmark member: the element-type spelling wave of 10-24/26 turned
`Tensor`, and the scalar type in a return position, into spellings the engine no longer accepts (`f32` where the
declaration says `float`, or a `float` scalar against an `f32[..]` tensor), across the autodiff cases, the two perf
cases, the reduce-layout generator, the four showcase training programs and the benchmark models. Every one of those
rewrites is the author's own, they agree with the 10-24 record's `f32`/`float` note on `device_parameter_tree_training`,
and no engine change is needed to explain them.

Not one of the nine families is reachable below the full layer: each entry declares `milestone` or is a `poison` probe,
which is why no lane's gate - all of them fast - could see any of them land. The run that settles this record's tier
question was `-Tier slow` over `feat/mm`, `feat/compile` and `feat/modules/autodiff` at `62d9be3b9`: `total 266,
pass 265, fail 1`, and the single red is ②, because the milestone entries are not collected at all by that tier. That
is the structural reading of this record, and it is what the `poison` and `slow` bookkeeping should be read against.

## What this run does not establish

- How the host's load interacts with the nine rows other than `startup.cost`; the second-look reading above rules the
  loaded host out for the other eight, and `startup.cost`'s own numbers are printed here so that a quiet-host re-run can
  be compared against them.