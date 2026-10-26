# Slow, perf and gpu layers at the batch point `e7a8b906d`

Measured revision **`e7a8b906d`** (the merge lane's line: `origin/develop 0dc342a77` plus the six
question pages it landed), through `camel-remote.ps1 -Plan all -Tier all -Jobs 8 -Check all`, on the
32-core EPYC host while two other lanes were gating their own fast layers.

    [SUMMARY] total: 10050, pass: 10020, fail: 24, skip: 2, known failures: 4
    [INFO] wall: 2875.8 s, jobs: 8, tier: all
    [camel-remote] checks: 8 run, 0 failed (0 s)

The fast layer of the same line reads `6834 / 6828 / fail 0 / skip 2 / known 4` (gate at
`0dc342a77`): **no red below is in the fast tier.** All 24 are slow, perf or gpu entries. Nothing was
skipped (skip 2, known 4 are the layer's own, unchanged from the fast gate).

Prev: `batch-slow-2026-10-24.md` (`test/plans -Tier slow` = `2947 / 2938 / fail 9`, and
`test/plans/perf -Tier all` = `45 / 45 / fail 0`). That record carried a five-row red table with
per-item attribution and bisect logs; the rows it named and this run shares are marked below.

## The reds

| test | measured | attribution |
|---|---|---|
| `opt.progen/progen_check` | `44.18 s cpu of 150.00 s budget`, 57.28 s wall, red | pre-existing: the 10-24 record already had it red (82 wrong cells), red at `07e81237f` |
| `progen_fixed` | 15.94 s, red | not in the 10-24 record's short list; same `progen` family |
| `benchmark_gradients_vs_torch` | `761.76 s cpu of 1600.00 s budget`, 183.24 s wall, red | budget not the cause (under half); unattributed |
| `modules.onnx/pass_export_equals_export_model` | `2.91 s cpu of 75.00 s budget`, 1.75 s wall, red | the 10-24 record: the margin rule (`d0278f224`) reads CPU time and fails it; green at `07e81237f` |
| `gc.runtime_graph_constants.moving.nvm` / `.fvm` | 158–166 ms, red | unattributed (new since the 10-24 record) |
| `gc.macro_round_lifetime.moving_stress.nvm` / `.fvm` / `.jit` / `.prl` | 210–266 ms, red | unattributed (new since the 10-24 record) |
| `poison:compile.stream:stream.tee_report` / `…_element_size_unknown` | 250 / 173 ms, red | unattributed (new since the 10-24 record) |
| `startup.cost` | `10.78 s cpu of 13.00 s budget`, 27.49 s wall, red | budget/margin under host load — the 10-24 record's whole `perf` group passed then, at a quieter host |
| `mlp_step.nvm` / `.fvm` | 165 / 168 ms, red | same load/margin reading; unattributed |
| `transformer_step.nvm` / `.profile` | 156 / 217 ms, red; the `.profile` arm reports `stdout does not contain 'wall per step'` | the profile arm's message is not a margin: unattributed |
| `curve` | `10.00 ms cpu of 14.00 s budget`, 349 ms wall, red | budget not the cause; unattributed |
| `shapes` | 273 ms, red | unattributed |
| `reduce_layout.ops` / `.count` | `60.00 ms of 67.00 s` / `70.00 ms of 15.00 s`, red | budgets not the cause; unattributed |
| `small` ×4 | budgets 13 s / 23 s / 15 s / 17 s, 860 ms–6.00 s cpu, red | four perf entries share the name `small`; the capturing filter kept the entry name without its plan, so they are recorded by budget |
| `device_parameter_tree_training` | 224 ms, red | pre-existing and independent of this line: the tiles lane reports it red on plain `develop` too (`device_tree.cml:11` `f32`/`float`) |

### Plan identities

The capture kept an entry's name without its plan, and this is what each row above is, read from this
tree's `test/plans/`: `progen_check` and `progen_fixed` are `feat/opt/progen`; `benchmark_gradients_vs_torch`
is `feat/modules/autodiff`; `pass_export_equals_export_model` is `feat/modules/onnx`;
`gc.runtime_graph_constants.moving.nvm` / `.fvm` and `gc.macro_round_lifetime.moving_stress.*` are
`feat/mm/gc`; `stream.tee_report` and `stream.tee_report_element_size_unknown` are `feat/compile/stream`;
`startup.cost` is `perf/startup`; `mlp_step.nvm` and `transformer_step.nvm` are `perf/training_steps`;
`curve` is `perf/gemm_threads`; `reduce_layout.ops` is `perf/reduce_layout` and `reduce_layout.count` is
`perf/reduce_layout_counts`; `device_parameter_tree_training` is `gpu/tiles_transfer`. The four `small`
entries are told apart by the budget their plan declares: `perf/showcase_c2` (`cpu_budget_ms = 13000`,
C2 MLP/MNIST), `perf/showcase_c1` (23000, C1 linear+logistic), `perf/showcase_c6` (15000, C6 VAE) and
`perf/showcase_d5` (17000, D5 Tree-LSTM); a fifth entry of the same name, `perf/showcase_d4` (6000,
D4 MCTS), is not among them. `shapes` is the one name three plans carry (`feat/compile/enum`,
`feat/para/handoff`, `perf/gemm_canonical_shapes`); the two `feat` entries of that name ran green in the
fast layer of the same period, so this row is the `perf/gemm_canonical_shapes` one.

## What this run does not establish

- Attribution for the twelve rows marked unattributed: they need the same treatment the 10-24 record
  gave its rows (a quiet-host re-run, then a bisect against `origin/develop`), which this run did not do.
- The whole layer ran under a loaded host (two lanes gating, `-Jobs 8`): the CPU-time margin rules the
  10-24 record discusses are exactly the ones that flip under load, so the perf rows above are the
  least trustworthy part of this table.

The layer's 8 static checks were green on this revision, and the five merges that follow it
(`gate-without-a-wait`, `bench-tool-anchors`, `captured-write-tuple`, `arrow-composite-literal-typing`,
the question pages) were each gated on the fast layer before being pushed.