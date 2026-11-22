# Slow layer at the batch point `bd20f2200`

Measured revision **`bd20f2200`**, the `origin/develop` the batch point names; the same layer was then re-run on the lane's new tip. Both runs went through `camel-remote.ps1` with `-Jobs 8`, on the 32-core EPYC host (load average 69-96 throughout).

    [SUMMARY] total: 45, pass: 45, fail: 0
    [INFO] wall: 881.6 s, jobs: 8, tier: all
    [SUMMARY] total: 2922, pass: 2914, fail: 8
    [INFO] wall: 1953.4 s, jobs: 8, tier: slow

and on the new tip `65a977991` (`bd20f2200` merged with `origin/develop`, whose tip was `717e98f75` when the merge was taken), target `test/plans -Tier slow`:

    [SUMMARY] total: 2947, pass: 2938, fail: 9
    [INFO] wall: 2191.6 s, jobs: 8, tier: slow

| run | target | plans | entries | wall (with build) | skipped |
|---|---|---|---|---|---|
| 1 | `test/plans/perf`, `-Tier all` | 18 | 45 | 881.6 s (901 s) | 0 |
| 2 | `test/plans`, `-Tier slow` | 38 | 2922 | 1953.4 s (1972 s) | 0 |
| 3 | `test/plans` at `65a977991`, `-Tier slow` | 38 | 2947 | 2191.6 s (2220 s) | 0 |
Logs `notes/batch-slow-perf.log`, `notes/batch-slow-slow.log`, `notes/batch-slow-newtip.log`, attribution runs `notes/batch-slow-bisect-*.log`. Nothing was skipped, so no workload hid behind a skip. Every `perf` entry ran inside its asserted CPU budget, and two runs at one revision read the same numbers (`collfuzz.gc` 67.4/67.9 s of 112 s, `gc_soak_long.mark_sweep` 101.6/100.5 s of 171 s, `step.budget` 36.9/36.2 s of 61 s): no time budget moved, and no red below is a budget.

## The reds

| test | the plan asserts | measured | attribution |
|---|---|---|---|
| `opt.progen/progen_check` | exit 0, `0 wrong`, `PASS` | exit 1, **82 wrong** of 1025 cells; each is `FRONTEND: No matching function found`, in seed 19 and seed 23 (41/41 cells, the pass-free `default` cell included) | pre-existing: red at `07e81237f` (10-23 23:57) |
| `modules.onnx/resnetbn_train_step_export` | `timeout_ms = 300000` as the cost bound | the work passes (exit 0, every check, 1125 nodes, `onnx.checker ok`); only the margin rule fails it: **111 990 ms of CPU**, 2.68x of 300 s, which wants 4x | `d0278f224` (10-24 02:39) reads the margin in CPU time; green at `07e81237f`, where the same rule read the wall (17.58 s) |
| `modules.onnx/pass_export_equals_export_model` | `timeout_ms = 120000` | exit 0, every program `array_equal`; margin rule: **40 770 ms of CPU**, 2.94x | same commit; green at `07e81237f` (wall 15.38 s) |
| `mm.gc_pacing/gc_pacing.churn_prl` | `peak` in [4.8e6, 8.0e6] B and `released >= 590` | peak **9 600 000 B**, `released 598`, `total 180000000`, `major=0 requested=299 deferred=299 by_other=299` - no collection ran | not a merge: the identical reading appears at `781431b3e` (01:08), and it passes at `bd20f2200` alone in this window, at `f14c3848e`, `e72302135` and 13 other runs; it fails only inside a loaded full-layer run, at both tips, and the entry's own comment records the load dependence (7.2 MB alone) |
| `mm.gc_poison_sweep/poison:modules.rules:rules.why_not_names_the_false_condition` | exit 0 and three output fragments | exit null, stdout and stderr empty, under `CAMEL_GC_POISON=1`; reproduced at `bd20f2200` in a second window (2645 tests, 1 red) | pre-existing: failed the same way at `07e81237f` |
| `gpu.tiles/runtime_scalars` | exit 0 and `std::nvm/fvm/jit: runtime scalar values` | exit 1, `No matching function found` at `runtime_scalars.cml:12`: `(Tensor<float32, [3]>, float)` where the callee declares `(x: Tensor<float32>, k: f32)` | **`3f8666d10`** (02:54, "refuse a scalar operand of another type than the element type"), the second commit of the `handover/h371-literal-element-type` lane, landed by the merge **`9b5e2b348`** (04:30): green at the lane's first commit `2c9c05e78`, red at `3f8666d10` |
| `gpu.tiles/loop_boundaries_faults_permissions` | exit 0, four `std::<vm>: LOOP CPU boundaries` lines and `neighboring GPU regions preserved PASS` | exit 1 (0.12 s), `PassInternalError` 0x0400000C: "Pass 'tiles::compile' failed with an internal error: Tiles region transfer has an unsupported aggregate leaf" | same commit and merge as the row above; also red at `e54e4e43a` (01:25) and green again by `9d5830492`, so that earlier reading was a mid-flight state |
| `gpu.tiles_transfer/tree_expansion_autodiff_and_device_update` | exit 0 and `"0.0\n"` five times | compile error at `tree_training.cml:8`: the value's type `{ weight: Tensor<float64, [2]>, nested: (Tensor<float64, [1]>, Tensor<float32, [0, 2]>) }` does not match the declared `Model` (its fields are `Tensor<f32>`) | pre-existing: red at `07e81237f` |

Old tip against new tip, the same layer: the eight above are red at both, and exactly one entry differs.

| | entries | reds | that one entry |
|---|---|---|---|
| `bd20f2200` (batch point) | 2922 | 8 | `frontend.fuzz_lossless_tree` passes (1.94 s in the layer, 2.00 s re-run alone) |
| `65a977991` (new tip) | 2947 | 9 | it dies there in both the layer and a single-plan run (2.97 s / 1.74 s): `camel-parsecheck fuzz ended abnormally: exit code -11` |

The crash is reproducible (red in a layer run and a single-plan run at the tip, and at `f177b10db`; green in two runs at `bd20f2200` and at every earlier first-parent point), and it was introduced by **`f177b10db`** (14:56, "feat(stdlib): one reduction declaration per element type, and no run-time type switch"), which `origin/develop` brought after the batch point; the 25 entries that merge adds are green.

## Attribution

Re-run at `07e81237f` (10-23 23:57, the pre-today baseline), at `05a305e6f` (12:48, the earliest afternoon merge point), at `f14c3848e`/`e72302135` for the GC reading, and along the first-parent line for the compiler reds: `8b7ac648e` (00:26), `e54e4e43a` (01:25), `9d5830492` (03:32), `191330ee4` (04:18), `b128d3227` (04:22), `9b5e2b348` (04:30), `85328215e` (05:47), plus the h371 lane's own `2c9c05e78` (green) and `3f8666d10` (red). The four compiler reds are already red at the earliest afternoon merge point and the GC reading is green at all six, so re-running each afternoon point could only repeat that; the runs went to the morning window where the reds appear instead. Two runs were repeats: a `461eebf1d` point was void (its host checkout was removed mid-run, 20 `FileNotFoundError`s) and is quoted nowhere here, and the first `07e81237f` attempt was cut off and re-run without `perf`.

## What moved

Nothing the afternoon's seven merges did moves what a user feels. The perf layer reads the same at both ends of the afternoon with 20-40 % of every asserted CPU budget to spare, and no red is a time budget. The two compiler reds are front-end refusals from the **morning's** literal work: a literal beside a tensor no longer reaches an `f32` parameter, and a loop boundary's region now meets an aggregate leaf that `tiles::compile` refuses - a user writing either would feel it, but neither comes from the seven afternoon merges. The ONNX reds are the runner reading the entries' own CPU (a 112 s multi-threaded torch test against a 300 s liveness bound), not the exporter doing more work, and the `progen`, `tree_training` and poisoned-`rules` reds are the same at the pre-today baseline. The layer is in far better shape than at `07e81237f`, where the same plans showed 44 reds (42 poisoned entries, of which today's rule-set work fixed 41 and left the one crash). The one thing a user would feel at once is the new tip's parse-check crash, which `bd20f2200` does not have.