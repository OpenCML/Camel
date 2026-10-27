# 快层读不到的 30 条计划：批次点方案与两条待裁（2026-10-27）

基线 `origin/develop` = **`75c095089`**（300 条计划）。全部读数来自工作树
`D:\Projects\Camel-wt\h-neverread`（同一提交、干净树），经 `camel-remote.ps1` 在主机 `88zjwei` 上跑，
`-Jobs 8`。日志留在树外的 `D:\Projects\Camel-wt\_metrics\`。

## 1. 我量到的

- **快层读不到 30 条**。`-Plan all -Tier fast -Jobs 8`：`[INFO] discovered plans: 270`，日志里
  `[PLAN]` 行 270，`[SUMMARY] total: 7448, pass: 7430, fail: 12, skip: 2, known failures: 4`，墙钟 266.3 s。
  上一基线（`f3e0594f2`，278 条）读 248；"244 被读 / 34 从不被读" 是更早的读数，已过期。
- 30 条的构成：**22 条 `tier = "milestone"`**（就是 `scripts/plan-inventory.mjs` 的 `FORBIDDEN`
  整文件条目）+ **7 条 `tier = "slow"`**（`SLOW_PLANS` 按文件读，属发布闸门的慢层）+
  **1 条 `tier = "debug"`**。**任何例行闸门都不读的是 23 条**（22 + 那 1 条 debug）。
- 为什么把 debug 计划塞进 `-Tier all` 一定假红：`test/tools/run-tests.mjs:2020` 的 `selectTier`
  先看"这个计划是否在命令行上被点名"——**点名的计划整文件跑，不看 tier**。所以
  `-Plan test/plans/debug/view_escape.plan.toml -Tier all` 会在 **Release** 二进制上跑那两条断言。
  实测（本基线，`-NoBuild`）：`[SUMMARY] total: 2, pass: 0, fail: 2`，两条都是
  `ended with exit 0, not the abort` 与 `VIEW_ESCAPE not-caught: the store guard is compiled out of this build`。
- **只有 `debug/view_escape.plan.toml` 必须 Debug**：全树 `test/plans/**` 中只有它提到
  `CAMEL_ASSERT_NOT_VIEW`，而 `include/camel/core/rtdata/view.h:98` 在 `#if defined(NDEBUG)`（第 97 行）
  下把该宏定义成 `((void)0)`；Release 里断言不存在，程序正常退出。
  实测（本基线，`-Config Debug -Tier debug`）：`[SUMMARY] total: 2, pass: 2, fail: 0`，构建 316.8 s、跑 2.6 s。
- **新基线自己的快层不绿**：12 红，都不在本文这 30 条里——
  `docs.planted_page_passes_the_page_rule`（`FAIL docs/cli.md:234: a pass name followed by '(' is the removed form`）、
  `docs.word_form_only`、`docs.tutorial_images.current`、`registry.signatures_match`、`registry.docs_complete`、
  `registry.migrations_stay_migrated`、`registry.reference_is_generated`、`declaration_evaluator`、
  `new_values.peek_loop.nvm/.fvm/.jit/.prl`（这 4 条是"用了 3000 ms CPU 对 10000 ms 超时，低于 4x 余量"的
  界规则红，不是语义红）。

## 2. 两组批次点命令（请确认这个安排）

Release 组（29 条 = 22 milestone + 7 slow；本基线实测 `[SUMMARY] total: 216, pass: 206, fail: 10`，1333.3 s ≈ 22 min）：

```powershell
$plans = @(
  'test/plans/feat/mm/gc_collections.plan.toml', 'test/plans/feat/mm/gc_elder_exhaustion.plan.toml'
  'test/plans/feat/opt/progen.plan.toml',       'test/plans/gpu/tiles_regions.plan.toml'
  'test/plans/perf/collections_fuzz.plan.toml', 'test/plans/perf/fib.plan.toml'
  'test/plans/perf/gc.plan.toml',               'test/plans/perf/gc_tools.plan.toml'
  'test/plans/perf/gemm_canonical_shapes.plan.toml', 'test/plans/perf/gemm_threads.plan.toml'
  'test/plans/perf/instruction_counts.plan.toml', 'test/plans/perf/instruction_counts_load.plan.toml'
  'test/plans/perf/para.plan.toml',             'test/plans/perf/reduce_layout.plan.toml'
  'test/plans/perf/showcase_c1.plan.toml',      'test/plans/perf/showcase_c2.plan.toml'
  'test/plans/perf/showcase_c6.plan.toml',      'test/plans/perf/showcase_d4.plan.toml'
  'test/plans/perf/showcase_d5.plan.toml',      'test/plans/perf/startup.plan.toml'
  'test/plans/perf/step_budget.plan.toml',      'test/plans/perf/training_steps.plan.toml'
  'test/plans/feat/mm/gc_pacing.plan.toml',     'test/plans/feat/mm/gc_soak.plan.toml'
  'test/plans/gpu/tiles_rows.plan.toml',        'test/plans/gpu/tiles_transfer.plan.toml'
  'test/plans/perf/para_speedup.plan.toml',     'test/plans/perf/step_counts.plan.toml'
  'test/plans/perf/reduce_layout_counts.plan.toml'
) -join ','
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path <工作树> -Plan $plans -Tier all -Jobs 8
```

Debug 组（1 条；本基线实测 2/2 pass，构建 316.8 s ≈ 6 min）：

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path <工作树> -Config Debug -Tier debug -Jobs 8 -Plan test/plans/debug/view_escape.plan.toml
```

**安排本身要你确认**：这两组属于**里程碑批次点**（release 前由协调者在静默机上跑一次），
**不进每次合并闸门**。理由：29 条里 21 条是 `perf/**`（基准、曲线、soak），读数只与整批对照时有意义，
且 Release 组单跑 ~22 min。

## 3. 过期用例：5 红 / 3 个用例（请你裁：这些用例你改还是我们改）

同一根因：`float`（64 位）与 `f32`（32 位）之间没有隐式转换，规则原文
`docs/tutorial/02_values_and_let.en.md:131`：把 `int` 放到期望 `float` 的位置，编译器不转换，
"it names both types"。三条用例把 f32 的计算结果声明成 `float`，或把 `int[..]` 传进 `f32[..]` 的形参。

| 测试（计划） | 最小复现（整行） |
|---|---|
| `mlp_step.nvm`、`mlp_step.fvm`（`perf/training_steps`） | `[FAIL] mlp_step.nvm` → `the timed pass failed in iteration 1: SemanticDiag::NoMatchingFunction: No matching function found for argument types: <> ({ w1: f32[784, 128], b1: f32[128], w2: f32[128, 10], b2: f32[10] }, { w1: f32[..], b1: f32[..], w2: f32[..], b2: f32[..] }, OptimizerState, float).`（`test/cases/perf/mlp_step.cml:55:28` `let next = adam(m, gradient, opt, lr)`，`<nn:adam>: (params: P, grads: grad of P, state: OptimizerState, lr: float, …)`） |
| `transformer_step.nvm`、`transformer_step.profile`（同计划） | `[FAIL] transformer_step.profile` → `(transformer_step)/…/test/cases/perf/transformer_step.cml:90:28: [Error]: No matching function found for argument types: <> ({ g1: f32[8], g2: f32[8], wq: f32[8, 8], … }, { g1: f32[..], … }, OptimizerState, float).` |
| `device_parameter_tree_training`（`gpu/tiles_transfer`） | `[FAIL] device_parameter_tree_training` → `(device_tree)/…/test/cases/modules/tiles/device_tree.cml:10:22: [Error]: Return type 'f32' does not match the function's declared return type 'float'.` |

三条文件（`test/cases/perf/mlp_step.cml`、`test/cases/perf/transformer_step.cml`、
`test/cases/modules/tiles/device_tree.cml`）在 `75c095089` 上**没有改动**。
对照：上一基线同族的 4 红（`perf/gemm_threads` `curve`、`perf/gemm_canonical_shapes` `shapes`、
`perf/showcase_c1/c2` 的 `small`）在本基线**已经绿了**——新树把 `s + sum(product)` 改成
`s + float(sum(product))`、`mean(...)` 改成 `float(mean(...))`，与这里的判据一致。

## 4. 真实缺陷：1 条（另有 3 条新红待归因）

- **`progen_check`（`feat/opt/progen`，milestone）仍然红，但症状换了**：上一基线是
  `seed 19 [opt+opt@nvm]: FRONTEND: Cannot insert data of type 'float' into a(n) array with element type 'int'.`，
  本基线是 `seed 24 [macro+simplify@nvm]: RUN_ERROR: An error has occurred: Compile encode encountered an unmapped live node ref 164..`
  （同一 seed 24 在 `macro+inline@nvm`、`simplify+simplify@nvm` 等管线重复），
  `[FAIL] progen_check 119.19 s cpu of 150.00 s budget, 68.96 s wall`。
  **归因未裁**：是前端/编译编码的缺陷，还是生成器写出了不该写的程序（工具按"25 programs x 41 pipelines
  = 1025 cells (0 located refusals)"算它合法）。
- 本基线**新红 3 条**（上一基线同计划皆绿），都未归因：
  - `perf/startup` `startup.cost`：`[FAIL] startup.cost 15.25 s cpu of 13.00 s budget`，输出里两条子项不过：
    `startup.cost imports_all  cpu_median=91.7 ms … ceiling=90 ms result=FAIL`、
    `declaration.cost declarations=278 … per_declaration_us=154.6 … ceiling=60 us total_ceiling=20 ms result=FAIL`
    （同一次输出里 `host load=81.3/84.1/86.3`；CPU 时间应不随负载漂，故 154.6 µs 对 60 µs 不像纯负载）。
  - `perf/para` `merge_sort.nvm_timeit` / `merge_sort.prl_timeit`：
    `[FAIL] merge_sort.nvm_timeit no reading cpu of 87.00 s budget, 180.54 s wall` +
    `killed at its 180.00 s liveness bound after 180.54 s`；同计划 `monte_carlo_pi.*` 同期绿。
  - `perf/reduce_layout_counts` `reduce_layout.count`：`[FAIL] reduce_layout.count 8.68 s cpu of 15.00 s budget`，
    `[stdout] FAIL: sum_to_leading: the reduction moved:`。

上一基线的 12 红 → 本基线：**消失 4**（上面第 3 节的 gemm×2 与 showcase×2）、
**留存 8**（`progen_check` 症状换新、`mlp_step`×2、`transformer_step`×2、`device_parameter_tree_training`，
再加 2 条 build-tier 假红：`view_escape` 在 Release 被点名）、**新增 4**（`startup.cost`、
`merge_sort`×2、`reduce_layout.count`）。那 2 条假红只在 Release 存在，Debug 档下 2/2 绿。

## 5. 没量到的

- 没量 12 条新基线既有红（docs/registry/peek_loop）的归因，也没在新基线上跑过 `-Check all`；
  它们不在本文这 30 条里，但要"快层全绿"得由引入它们的分支解释。
- 没量 `-Jobs 8` 以外并发下的同一批（`-Jobs 12` 已知会让 GPU 偏好用例假红，未复测）。
- 没量新基线新增 22 条计划的行为，只量了"是否被读"（结论：都在快层或慢层名单里，未扩大这 30 条）。
- 没有重跑 `scripts/plan-inventory.mjs`；30 条的划分是按其表（`FAST_TARGETS`、`SLOW_PLANS`、`FORBIDDEN`）
  加 `testTier` 规则静态复算，并用快层的 `discovered plans: 270` 对过（一致）。
- 未改任何计划、用例、断言、tier；本页只是记录。