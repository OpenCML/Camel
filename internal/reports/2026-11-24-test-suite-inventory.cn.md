# 测试盘点：420 个计划 / 6,856 条用例 / 快层 9,916 条 / 261 条超过 2 秒

2026-11-24，负责人 D-1123-151 派工：**先盘点，读数交回**；压力与计时类移出快层、快层加单用例时间预算
是下一步，本页不动测试结构。依据 `docs/roadmap.cn.md` §0 第 7 项与
`internal/plans/2026-11-24-test-suite-refactor.cn.md`。

**本页只做盘点。**`test/` 下一个字节没改，`run-tests.mjs` 没改，`develop` 没动，没有推送。

## 一句话

计划树今天有 420 个计划、6,856 条 `[[tests]]` 条目，展开后 14,986 条用例，其中快层 9,916 条。
记录里最近一次全快层跑（2026-11-12，`-Jobs 28`）8,932 条、墙钟 443.2 s；把子进程墙钟按计划加起来是
6,212.7 s，而这 6,212.7 s 里有 **1,378.8 s（22.2%）压在 261 条用例上**——它们就是"超预算名单"的主体。
近一个月有记录的 324 次跑里，431 条真用例红过至少一次（另有 35 条是自测夹具的**故意**红）；
只取 13 次全快层的跑，则 **11 条每次都红**。按用途分，快层 9,916 条里 9,471 条是正确性，
**419 条是压力/计时/性能**，19 条是防回归，7 条归不了。

## 口径：一个计划里怎么数用例

口径来自 `test/tools/run-tests.mjs` 与 `scripts/test.js`（后者只是 `test/runner.mjs` 的入口），
以及已经把同一套读法固定下来的 `scripts/plan-inventory.mjs`。**同一个计划有四个数，本页四个都报，
不混着用**：

| 数 | 怎么来的 | 今天 |
|---|---|---:|
| `[[tests]]` 条目 | 计划文件里写了几条 | 6,856 |
| 展开后用例 | `loadPlanTests`：一条条目按 `backends` 展开成每后端一条（`expandBackends`） | 10,126 |
| 派生副本 | `expandSweeps`：`[[sweeps]]` 把别的计划的用例复制过来再跑一遍（GC 毒化就是这样） | 4,860 |
| 合计实例 | 10,126 + 4,860 | 14,986 |

层的归属不看计划落在哪个目录，看 `run-tests.mjs#testTier`：用例自己的 `tier`，否则计划的 `tier`，
否则 `fast`。`tier = "feat"` 这类旧标签按 `fast` 读。今天：**fast 9,916 / slow 164 / milestone 4,871 /
debug 35**。`scripts/plan-inventory.mjs` 用同一个 `loadPlanTests` + `expandSweeps` + `testTier`，
所以本页的数与闸门自己的一致性检查同源，不是另写一份解析。

**耗时的口径**：记录里 `summary.json` 的 `test_timings[]`（`slot_ms` 是占住一个槽的时间，
`child_ms` 是子进程墙钟）与 `meta.json`（每个用例一条，带 `plan_path`）。本页报的是
**子进程墙钟按计划求和**——它是"这一计划的用例一共花了多少时间"，不是"单独跑这张计划要多少秒"
（那是并发下的排队与争用问题，见 `docs/test-plans.md` 的 slot 一节）。

**红的口径**：`test_timings[].status === "fail"`。`skip` 不算红；`xfail`（`known_fail` 命中）不算红，
但它是"已知缺陷"，本页单列。

## 记录从哪来、样本有多大

- `D:\Projects\Camel\test\results\log\` 下 **1,089 个跑测输出目录**（`test/results` 是跑测工具自己写的，
  不进版本库）；其中 **1,067 个有 `summary.json`**，**324 个有逐用例记录**，日期 **2026-11-02 … 2026-11-21**。
- 全快层（目标 `test/plans/smoke` + `test/plans/feat`，或整棵 `test/plans`）的跑有 **13 次**：
  11-02 两次、11-09 六次、11-10 四次、11-12 一次，每条 8,329–8,932 条。
  **11-12 之后再没有一次全快层的记录**；11-21 那批记录的目标只有 `test/tools/selftest/**`
  （跑测工具自测），不是快层。
- `internal/reports/ci-failures.md` 里有 CI 侧的读数（`[INFO] tier: fast, tests: 5956, jobs: 4`），
  但那是 **2026-10-24** 的 run `40077350582`，超出"近一个月"，而且 CI 只在打标签时跑。本页没有把它
  当近月样本，只在需要说"CI 跑的就是快层"时引它一句。

## ① 每个计划的用例数

按目录滚动（全表见附录 A）：

| 目录 | 计划数 | `[[tests]]` 条目 | 展开后用例 | 快层用例 | 记录里跑到的用例 | 记录里的子进程墙钟(s) |
|---|---:|---:|---:|---:|---:|---:|
| `feat/mm` | 16 | 252 | 5121 | 320 | 297 | 209.0 |
| `feat/compile` | 108 | 2050 | 2601 | 2601 | 2308 | 1362.9 |
| `feat/modules` | 71 | 1379 | 1907 | 1897 | 1829 | 1214.8 |
| `feat/opt` | 41 | 904 | 1638 | 1636 | 1181 | 1015.3 |
| `feat/consistency` | 26 | 479 | 1292 | 1292 | 1278 | 763.0 |
| `feat/linear` | 29 | 694 | 876 | 869 | 882 | 642.4 |
| `feat` | 10 | 166 | 445 | 444 | 432 | 193.3 |
| `feat/modules/tensor` | 9 | 145 | 188 | 188 | 188 | 81.6 |
| `gpu` | 6 | 125 | 146 | 8 | 0 | 0.0 |
| `feat/parse` | 7 | 106 | 128 | 127 | 115 | 57.5 |
| `feat/trans` | 17 | 114 | 114 | 113 | 101 | 298.5 |
| `feat/para` | 3 | 64 | 104 | 104 | 63 | 37.9 |
| `feat/compile/array_shape` | 1 | 87 | 88 | 88 | 88 | 32.3 |
| `feat/tools` | 26 | 82 | 82 | 77 | 59 | 207.7 |
| `feat/transforms` | 4 | 48 | 71 | 71 | 48 | 28.7 |
| `perf` | 22 | 69 | 69 | 0 | 0 | 0.0 |
| `probe` | 4 | 21 | 45 | 13 | 0 | 0.0 |
| `feat/format` | 3 | 32 | 32 | 32 | 31 | 25.1 |
| `feat/runtime` | 7 | 12 | 12 | 12 | 9 | 5.9 |
| `smoke` | 1 | 10 | 10 | 10 | 10 | 4.2 |
| `feat/effects` | 2 | 7 | 7 | 7 | 0 | 0.0 |
| `debug` | 2 | 3 | 3 | 0 | 0 | 0.0 |
| `feat/gate` | 3 | 3 | 3 | 3 | 3 | 21.0 |
| `feat/bindings` | 1 | 2 | 2 | 2 | 2 | 8.6 |
| `feat/docs` | 1 | 2 | 2 | 2 | 2 | 3.0 |
| **合计** | **420** | **6856** | **14986** | **9916** | **8926** | **6212.7** |

`feat/mm` 那一行是全树唯一有派生副本的目录：`feat/mm/gc_poison_sweep.plan.toml` 一条 `[[sweeps]]`
复制了 4,860 条，其中 `fast_sample` 只留 237 条在快层、其余 4,623 条是 `milestone`。所以它
"展开 5,121 条 / 快层 320 条"两个数差得远，不是算错。

一个计划里用例最多的是 `feat/modules/autodiff`（条目 234、展开 290）、`feat/opt/e9`（67 → 268，
十二条后端展开）、`feat/tutorial_examples`（68 → 244）。用例最少的是 `smoke/` 之外的 `feat/docs`、
`feat/bindings`：各 2 条。

## ② 快层每个计划的耗时

**样本**：`test/results/log/20261112_145724_34504`，`tier: fast`、`jobs: 28`、目标
`test/plans/smoke` + `test/plans/feat`，8,932 条，`pass 8864 / fail 18 / skip 18 / xfail 32`，
墙钟 **443.2 s**，平均并发 **14.02 / 28**。这是记录里**最近一次**全快层读数。

8,932 条里 **8,926 条记录**（8,925 个不同的「计划/用例」键，有两个计划各有一条同名用例）
**落到当前树的 331 个计划文件**上（6 条落不上：4 条属于 11-12 之后已删掉的
`opt.keep_node`，2 条改名了）。子进程墙钟求和 **6,212.7 s**，slot 求和 6,458.5 s。

前 20（全表见附录 A）：

| # | 计划 | 快层用例 | 记录用例 | 子进程墙钟合计(s) | 占记录总墙钟 | 最慢一条(s) | 最慢用例 |
|---:|---|---:|---:|---:|---:|---:|---|
| 1 | `feat/modules/onnx.plan.toml` | 155 | 155 | 277.9 | 4.5% | 14.55 | `ort_entry_export` |
| 2 | `feat/linear/var_reference.plan.toml` | 226 | 258 | 275.1 | 4.4% | 2.14 | `var_tensor_read.opt_macro_nvm` |
| 3 | `feat/trans/cpp.plan.toml` | 56 | 55 | 258.4 | 4.2% | 81.34 | `agree_with_fvm` |
| 4 | `feat/opt/algebra.plan.toml` | 192 | 192 | 251.5 | 4.0% | 2.72 | `gradient.false` |
| 5 | `feat/consistency/attempt.plan.toml` | 148 | 148 | 245.1 | 3.9% | 6.44 | `attempt.resources.isolated.nvm` |
| 6 | `feat/modules/autodiff.plan.toml` | 285 | 278 | 209.4 | 3.4% | 2.95 | `grad_structures` |
| 7 | `feat/opt/generic.plan.toml` | 118 | 121 | 132.2 | 2.1% | 2.31 | `optimization_namespace` |
| 8 | `feat/mm/gc_poison_sweep.plan.toml` | 237 | 215 | 99.9 | 1.6% | 1.89 | `poison:linear.jit_framed:framed.operators.nvm` |
| 9 | `feat/tools/debugger.plan.toml` | 10 | 10 | 80.3 | 1.3% | 60.79 | `debugger.dap_fusion_group` |
| 10 | `feat/modules/rules.plan.toml` | 174 | 174 | 79.7 | 1.3% | 4.52 | `rules.the_coverage_check_refuses_every_refuted_law` |
| 11 | `feat/tutorial_examples.plan.toml` | 244 | 244 | 76.4 | 1.2% | 1.19 | `tutorial.examples.01_hello.nvm` |
| 12 | `feat/compile/transforms.plan.toml` | 216 | 198 | 73.3 | 1.2% | 1.63 | `vjp_api.the_two_tables_are_compared` |
| 13 | `feat/compile/collections.plan.toml` | 143 | 143 | 68.3 | 1.1% | 3.20 | `collfuzz.quick` |
| 14 | `feat/consistency/faults_optimized.plan.toml` | 176 | 176 | 68.2 | 1.1% | 1.04 | `fault.in_tail_round.nvm` |
| 15 | `feat/opt/gate_value.plan.toml` | 60 | 60 | 67.2 | 1.1% | 2.21 | `covered_value.graph.true` |
| 16 | `feat/modules/nn.plan.toml` | 75 | 68 | 65.0 | 1.0% | 2.19 | `residual_mlp_inline_large` |
| 17 | `feat/compile/generics.plan.toml` | 65 | 65 | 64.7 | 1.0% | 2.02 | `dependent_names` |
| 18 | `feat/modules/tiles.plan.toml` | 109 | 109 | 64.1 | 1.0% | 2.92 | `gpu_scope.placement_with_a_device` |
| 19 | `feat/compile/literal_context.plan.toml` | 95 | 95 | 62.1 | 1.0% | 1.27 | `context.data_literal.jit` |
| 20 | `feat/opt/inline.plan.toml` | 69 | 62 | 61.4 | 1.0% | 2.01 | `arm_call_then_effect.opt.nvm` |

**为什么是这两个数并排**：443.2 s 是这个层在一次并发跑里的墙钟；6,212.7 s 是它"独占一台机器串行跑"
的等效代价。两者差 14 倍，就是并发度。争用对某些计划是放大的——`docs/test-plans.md` 记过
`gc_pacing` + `gc_soak` 37 条 6.7 s 让整闸墙钟涨 47.8 s 的先例，所以"一张计划的耗时"只能说
"它自己的用例花了多少"，不能说"它让闸门慢了多少"。

**这张表读不到的部分，明说**：

- **56 个今天有快层用例的计划在记录里根本没有读数**（648 条，占快层 9,916 的 6.5%）。最大的是
  `feat/opt/e9.plan.toml`（268 条快层用例）、`feat/compile/ref.plan.toml`（79 条）、
  `feat/compile/var.plan.toml`（41 条）。它们大多是 11-12 之后新增或改名的，**没有读数就是没有读数，
  本页不给它们编一个数**。
- 今天 smoke+feat 的快层是 9,895 条，那次记录是 8,932 条：差 963 条 = 648 条（无记录的计划）
  + 315 条（有记录的计划在 11-12 之后新加或改名）。
- **CPU 时间一条都没有。** 抽了 905 个跑、3,187 个 `meta.json`，`cpu_ms` 全是 `null`：
  这台主机的内核不报子进程 CPU（跑测工具自己会打 `timeout margin skipped: this host reports no
  CPU time`）。所以本页的耗时**只能是墙钟**，而墙钟这台机器几分钟内就有 ~10% 漂移
  （`internal/reports/assert-release-hole.md`）。**要拿它当预算，得先有 CPU 读数**，见 ⑤。

## ③ 近一个月红过几次

**样本一（主样本，全快层）**：上面那 13 次全快层跑，2026-11-02 … 2026-11-12，每次 8,329–8,932 条，
合起来覆盖 **8,962 个不同的用例**。结果：**142 条红过至少一次**，`fail` 状态合计 **374 次**。

**每次都红的 11 条**（在它出现过的每一次里都红）：

| 用例 | 红/出现 |
|---|---|
| `modules.rules` / `rules.accuracy_check_compares_error` | 13/13 |
| `modules.rules` / `rules.accuracy_library_passes_the_check` | 13/13 |
| `modules.rules` / `rules.every_exported_rule_enters_the_check` | 13/13 |
| `tools.load_class` / `load_class.shared_place` | 13/13 |
| `tools.load_class` / `load_class.explain_under_load` | 13/13 |
| `exemplars` / `08_structured_control_flow` | 13/13 |
| `tools.debugger` / `debugger.dap_fusion_group` | 13/13 |
| `docs.links` / `docs.snippets_compile` | 13/13 |
| `modules.unicode_paths` / `unicode.paths` | 13/13 |
| `feat.tools.exit_reporting` / `runner.exit_reporting_names_the_signal` | 12/12 |
| `modules.tensor` / `erfc_grad.f64` | 11/11 |

**样本二（更大，但混了子集跑）**：324 个有逐用例记录的跑，2026-11-02 … 2026-11-21，
9,231 个不同的用例。红的 **466** 条；其中 **35 条是自测夹具的故意红**
（`test/tools/selftest/**` 的 `selftest.*`、`probe.*`、`<invalid plan>`——它们的期望就是失败/非法计划），
扣掉后 **431 条真用例红过至少一次**，`fail` 状态 **772 次**，落在 **69 个计划**上。前 18：

| 计划 | 红过的用例数 | 红的次数合计 | 单条最高（红/出现） |
|---|---:|---:|---:|
| `feat/compile/transforms` | 81 | 104 | 3 |
| `opt.screens` | 73 | 75 | 3 |
| `modules.rules` | 7 | 55 | 17 |
| `opt.generic` | 37 | 49 | 6 |
| `docs.links` | 4 | 38 | 14 |
| `compile.stream` | 13 | 36 | 8 |
| `opt.algebra` | 28 | 28 | 1 |
| `tools.load_class` | 2 | 26 | 13 |
| `modules.stdlib_registry` | 6 | 25 | 6 |
| `exemplars` | 2 | 23 | 13 |
| `tools.debugger` | 10 | 23 | 14 |
| `consistency.new_values` | 10 | 18 | 9 |
| `consistency.runner` | 1 | 15 | 15 |
| `opt.camel_passes` | 4 | 13 | 5 |
| `modules.unicode_paths` | 1 | 13 | 13 |
| `feat.tools.exit_reporting` | 1 | 12 | 12 |
| `opt.fault_preservation` | 12 | 12 | 1 |
| `opt.sync_chain` | 12 | 12 | 1 |

两类红要分开读，混在一起就会得出错的结论：

- **常红**（`modules.rules` 17/17、`tools.load_class` 13/13、`tools.debugger` 14/14、`docs.links` 14/15、
  `consistency.runner` 15/16、`exemplars`、`modules.unicode_paths`、`erfc_grad.f64`）：一条用例稳定地红，
  它是**一个没修的缺陷**，不是噪声。这几条同时也是 ②的表里最慢的一批（`debugger.dap_fusion_group`
  60.8 s、`runner.backend_matrix` 33.3 s、`docs.snippets_compile` 19.7 s）。
- **抖**（`feat/compile/transforms` 81 条各红 1–3 次、`opt.algebra` 28 条各红 1 次、
  `opt.screens` 73 条、`opt.fault_preservation` 12 条、`opt.sync_chain` 12 条）：同一批用例不是稳定红，
  是**时不时红**。这正是重构计划第 5 条说的"不稳定的用例单列"那一类，而本页只交读数、不修。

## ④ 按用途归类

### 判据（先用哪个字段，命中就归哪一类）

一个用例只归一个类，顺序如下；判据全部是**计划文件与用例定义里的字段**，不猜名字的含义：

1. **计时**：计划 `time_sensitive = true`；或用例 `args` 含 `--runs`（重复跑取时）；
   或 `perf/**` 里带 `--timeit` 的；或用例名说 `wall_clock` / `elapsed`。
2. **性能**：计划在 `test/plans/perf/**`；或 `kind = "benchmark"`；或有 `perf_budget_mean_ms`；
   或有 `cpu_budget_ms`（成本上界，`docs/test-plans.md` 明写"判的是 CPU 时间，不是墙钟"）。
3. **防回归**：**按身份判，不按跑法判**——计划文件名整段是提交号（`feat/opt/adb67e081.plan.toml`）；
   或用例名以一个提交号开头；或计划在 `probe/**`。这一条排在压力之前：一个探针用它探的手段
   （`--gc-check`）跑，不改变它是一个探针。
4. **压力**：计划是 `feat/mm/` 下的 GC 计划（`gc`、`gc_collections`、`gc_pacing`、`gc_soak`、
   `gc_stress`、`gc_root_window`、`capacity`）；或 `gc_poison_sweep` 的派生副本（`--gc-check poison`）；
   或用例 `args` 含 `--gc-stress` / `--gc-check`。
5. **正确性**：其余有断言的（`behavior` / `negative` / `snapshot` / `differential` / `positive`）。
6. **未归类**：没有 `kind`、没有 `golden`，也没有上面任一标记。

**写这条判据时踩到的一个坑，记在这里**：第一版把"用例名里出现 8 位以上十六进制"当提交号，
结果 `feat/opt/algebra` 的 24 条边界用例（`wrapping.9223372036854775807.nvm`、
`boundary32.1.401298464324817e-45.false.nvm`）被判成防回归——**那是数值，不是提交号**。
改成"整段是提交号"或"名字以提交号开头"后，这 24 条回到正确性。上面表里的数是改正之后的。

### 计数

| 用途 | 全树（14,986） | 快层（9,916） |
|---|---:|---:|
| 正确性 | 9,594 | 9,471 |
| 压力 | 5,216 | 414 |
| 性能 | 103 | 4 |
| 计时 | 15 | 1 |
| 防回归 | 51 | 19 |
| 未归类 | 7 | 7 |

两个与"用途"正交、但重构要用到的属性，单独报，不塞进上面五类：

- **跨后端矩阵**：快层里 **4,832 条**（48.7%）是由 `backends` / `backends_agree` / `compare_to`
  展开出来的多副本——同一个程序跑多个后端。按重构计划第 3 条，快层里这些要收到"每类程序一个代表"，
  但**它们仍然是正确性用例**，不是另一类用途。
- **金标（整段输出比对）**：快层里 **998 条**是 `kind = "snapshot"` 或带 `golden`。

### 那 51 条"防回归"是什么，以及它不全都叫回归

- **6 条**：`feat/opt/adb67e081.plan.toml`，计划名就是提交号、用例名也以它开头
  （`adb67e081.rc03_seed30_std_opt`）。这是重构计划第 2 条点名要"并进它所属特性的计划、说明里
  写来历"的那一类。
- **45 条**：`probe/**` 四个计划（`dpack` 3、`dpack5` 5、`dpack5ok` 5、`refleak` 32）。读正文，
  前三个说的是 `...` 打包写法迁移前后的拒绝面与接受面，**是迁移探针，不是按提交号钉的回归**；
  `refleak` 探的是引用泄漏。我把 `probe/**` 归到防回归，依据是重构计划里"回归探针"这个词；
  **读过正文之后，更准确的说法是"临时探针/迁移探针"**。这不影响"它该被处理"的判断，但影响
  "它该并到哪里"——**这一条需要负责人定，本页不硬塞**。

### 快层里那 419 条非正确性用例（就是下一步要移出去的）

| 判据 | 条数（今天的快层） | 记录里这些用例的子进程墙钟 |
|---|---:|---:|
| `gc_poison_sweep` 的派生副本（`--gc-check poison`） | 237 | 99.9 s |
| `args` 含 `--gc-stress`（普通计划里的用例，压力模式跑行为断言） | 111 | 73.1 s |
| `args` 含 `--gc-check`（GC 校验模式） | 44 | 19.6 s |
| GC 计划（`feat/mm/gc_*`、`capacity`） | 22 | 68.9 s |
| `cpu_budget_ms` 成本上界 | 4 | 3.1 s |
| `--runs` 重复跑取时 | 1 | 0.0 s |

按计划看（只列有非正确性用例的 22 个计划；"记录墙钟"是这些用例的子进程墙钟，
`gc_poison_sweep` 那一行取的是整张计划）：

| 计划 | 快层用例 | 压力 | 计时 | 性能 | 非正确性合计 | 记录墙钟(s) |
|---|---:|---:|---:|---:|---:|---:|
| `feat/mm/gc_poison_sweep.plan.toml` | 237 | 237 | 0 | 0 | 237 | 99.9 |
| `feat/modules/tiles.plan.toml` | 109 | 38 | 0 | 0 | 38 | 64.1 |
| `feat/compile/ref.plan.toml` | 79 | 33 | 0 | 0 | 33 | — |
| `feat/mm/capacity.plan.toml` | 18 | 18 | 0 | 0 | 18 | 40.2 |
| `feat/modules/json_value.plan.toml` | 43 | 15 | 0 | 0 | 15 | 9.3 |
| `feat/linear/jit_framed.plan.toml` | 51 | 14 | 0 | 0 | 14 | 39.3 |
| `feat/consistency/new_values.plan.toml` | 58 | 11 | 0 | 0 | 11 | 24.3 |
| `feat/para/core.plan.toml` | 48 | 10 | 0 | 0 | 10 | 27.4 |
| `feat/consistency/attempt.plan.toml` | 148 | 9 | 0 | 0 | 9 | 245.1 |
| `feat/linear/loops.plan.toml` | 160 | 8 | 0 | 0 | 8 | 40.6 |
| `feat/modules/tensor.plan.toml` | 121 | 4 | 0 | 0 | 4 | 57.5 |
| `feat/opt/splice_place.plan.toml` | 45 | 0 | 0 | 4 | 4 | 21.3 |
| `feat/modules/nn.plan.toml` | 75 | 3 | 0 | 0 | 3 | 65.0 |
| `feat/compile/recursive_struct.plan.toml` | 6 | 2 | 0 | 0 | 2 | 1.2 |
| `feat/mm/gc_root_window.plan.toml` | 2 | 2 | 0 | 0 | 2 | 27.9 |
| `feat/mm/gc.plan.toml` | 2 | 2 | 0 | 0 | 2 | 0.5 |
| `feat/modules/autodiff.plan.toml` | 285 | 2 | 0 | 0 | 2 | 209.4 |
| `feat/modules/graph.plan.toml` | 80 | 2 | 0 | 0 | 2 | 37.9 |
| `feat/para/handoff.plan.toml` | 25 | 2 | 0 | 0 | 2 | 10.5 |
| `feat/linear/ref_handle_capture.plan.toml` | 2 | 1 | 0 | 0 | 1 | — |
| `feat/linear/var_reference.plan.toml` | 226 | 1 | 0 | 0 | 1 | 275.1 |
| `feat/opt/writings.plan.toml` | 2 | 0 | 1 | 0 | 1 | — |

**这里有一件事必须先说清楚，否则"移出去"会移错**：`--gc-stress` 与 `--gc-check` 的那 155 条
（111 + 44），加上 `gc_poison_sweep` 的 237 条派生副本，都是**把别人的程序在压力模式下再跑一遍**。
`feat/modules/tiles` 那条 `compiled_region_fresh_inputs.nvm` 的 `args` 是
`--gc-stress safepoint=1,kind=minor --gc-check verify=both`——程序的输出是它断言的东西，
压力只是让它更容易暴露。把它们移出快层，**丢掉的不是速度，是"普通改动也会在压力下跑一遍"这件事**。
本页不替负责人决定这一条：可选的是"移到压力层、每天跑一次"，代价是这类缺陷的发现从每次提交
推到每天。**需要裁定，本页只列出名字。**

### 归不了的 7 条（没有硬塞）

| 计划 | 用例 | 为什么归不了 |
|---|---|---|
| `feat/compile/tags.plan.toml` | `argument_rule_binding` | 没有 `kind`、没有 `golden`，名字与 `desc` 都不说钉什么 |
| `feat/modules/autodiff_value_with.plan.toml` | `value_with.same_as_grad` | 同上 |
| `feat/modules/naive_in_expert_out.plan.toml` | `train_step.loss.nvm` / `.fvm` / `.jit` | 同上 |
| `feat/trans/annotated_dump.plan.toml` | `boundaries.tooltip_slots` | 同上 |
| `feat/trans/macro.plan.toml` | `export_loop.reading` | 同上 |

这 7 条是 `kind` 缺省的条目。缺 `kind` 在跑测工具里是合法的（默认按 `behavior` 跑），但**它让
"按用途归类"没有字段可读**——这正是重构计划第 2 条"一个用例一个用途，名字说它钉什么"要补的地方。

## ⑤ 超预算名单

### 建议的阈值，与理由

**建议：快层单用例 2,000 ms CPU（不是墙钟）。** 理由，按可核查的顺序：

1. **它比现在的隐含线只紧一点点，不是新发明。** 跑测工具的默认 `timeout_ms` 是 10,000，而
   `TIMEOUT_MARGIN = 4`：一条用默认超时的用例，**用掉 2,500 ms CPU 就已经被判红**
   （`run-tests.mjs#timeoutMarginVerdict`）。2,000 ms 就在这条线下面，所以它不改变"什么算快"，
   只是把线说出来、并把它挪到快层这一层。
2. **它抓住的正是拖慢快层的那一小撮。** 记录里 8,925 条中 **261 条（2.9%）超过 2 s**，
   它们合计 **1,378.8 s，占全层 6,212.7 s 的 22.2%**。移出这 261 条，快层的活少掉五分之一。
3. **单位必须是 CPU，不能是墙钟。** `docs/test-plans.md` 写死了这条：墙钟在这台共享主机上
   "量的是别人在跑什么"，CPU 才是用例自己的活；`internal/reports/compile-census.md` 的判据是
   「**预算类断言的量纲是「单跑 CPU 秒」。用并行下的 wall 或别人的机器写预算，等于给未来每一次失败
   预先埋一个假因。**」本页表里的秒数全是墙钟，
   **它们是"谁该被检查"的名单，不是"谁超了"的判决**。
4. **2 s 偏松也可接受，偏紧不行。** 记录里 1 s 以上的有 1,507 条（16.9%）——按 1 s 划线会把
   正常的中等用例也扫进去，闸门会天天要求改计划。2 s 是"明显不属于快层"的那条线。

**一条前提，必须先满足**：这台主机**读不到 CPU 时间**（③ 里那句）。所以"跑测工具强制"这件事
在今天的记录上**无法生效**——`TIMEOUT_MARGIN` 规则在这里本来就是 `skipped`。
要么让 `test/tools/child-cpu.mjs` 在 Windows 上拿到读数（`GetProcessTimes` / Job Object），
要么这条预算只能在有 `/proc/self/stat` 的主机上强制。**不把这一点写进设计，加进去的预算就是一条
永远不会红的断言**——和 `internal/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md`
里记的 `cpu_ms: null` 是同一类事。

### 名单：超过 2 s 的 261 条，按计划归并（64 个计划）

| 计划 | 超 2 s 的用例数 | 这些用例墙钟合计(s) | 最慢一条(s) | 最慢用例 |
|---|---:|---:|---:|---|
| `feat/trans/cpp.plan.toml` | 11 | 233.3 | 81.34 | `agree_with_fvm` |
| `feat/consistency/attempt.plan.toml` | 48 | 187.9 | 6.44 | `attempt.resources.isolated.nvm` |
| `feat/modules/onnx.plan.toml` | 24 | 152.0 | 14.55 | `ort_entry_export` |
| `feat/opt/algebra.plan.toml` | 39 | 89.6 | 2.72 | `gradient.false` |
| `feat/tools/debugger.plan.toml` | 7 | 75.4 | 60.79 | `debugger.dap_fusion_group` |
| `feat/consistency/surface.plan.toml` | 5 | 33.7 | 6.84 | `surface.minus.nvm` |
| `feat/consistency/runner.plan.toml` | 1 | 33.3 | 33.27 | `runner.backend_matrix` |
| `feat/opt/inline_use.plan.toml` | 5 | 33.0 | 6.67 | `inline_keeps_stdout.write.nvm` |
| `feat/mm/capacity.plan.toml` | 6 | 31.6 | 9.67 | `capacity.growth_simplify_gc_stress` |
| `feat/docs_links.plan.toml` | 4 | 28.1 | 19.73 | `docs.snippets_compile` |
| `feat/mm/gc_root_window.plan.toml` | 2 | 27.9 | 25.36 | `root_window.residual_mlp.moving_stress` |
| `feat/compile/type_text.plan.toml` | 1 | 24.1 | 24.11 | `type_text.roundtrip` |
| `feat/modules/stdlib_registry.plan.toml` | 2 | 23.3 | 20.32 | `registry.docs_complete` |
| `feat/tools/cli.plan.toml` | 2 | 22.7 | 11.86 | `cli.plan_arg_domain_refused` |
| `feat/compile/diag_catalog.plan.toml` | 3 | 21.6 | 14.95 | `catalog.doc_is_generated` |
| `feat/compile/unused_values.plan.toml` | 7 | 21.5 | 4.84 | `unused_values.one_input_one_answer` |
| `feat/opt/generic.plan.toml` | 9 | 19.4 | 2.31 | `optimization_namespace` |
| `feat/tools/find_fix.plan.toml` | 1 | 18.2 | 18.19 | `tools.find_fix_asks_who_already_fixed_it` |
| `feat/tools/load_class.plan.toml` | 3 | 17.3 | 12.82 | `load_class.shared_place` |
| `feat/tools/layer_accounting.plan.toml` | 1 | 14.0 | 13.97 | `runner.tier_declared_against_started` |
| `feat/consistency/values.plan.toml` | 4 | 12.6 | 4.96 | `value.deep_enum_chain.prl` |
| `feat/linear/var_reference.plan.toml` | 6 | 12.3 | 2.14 | `var_tensor_read.opt_macro_nvm` |
| `feat/parse/frontend.plan.toml` | 2 | 12.1 | 8.93 | `frontend.format_keeps_ast` |
| `feat/modules/import_cost.plan.toml` | 1 | 11.8 | 11.79 | `import.audit` |
| `feat/compile/type_identity.plan.toml` | 1 | 11.7 | 11.68 | `type_identity.corpus` |
| `feat/tools/autodiff_migration_ledger.plan.toml` | 1 | 11.5 | 11.47 | `tools.autodiff_migration_ledger.ratchet` |
| `feat/modules/autodiff.plan.toml` | 5 | 11.4 | 2.95 | `grad_structures` |
| `feat/modules/rules_libraries.plan.toml` | 4 | 10.7 | 2.84 | `rules.accuracy_scalar_every_backend.jit` |
| `feat/modules/nn.plan.toml` | 5 | 10.4 | 2.19 | `residual_mlp_inline_large` |
| `feat/opt/control_data.plan.toml` | 5 | 10.4 | 2.16 | `control_data.unused_argument.reference.prl` |
| `feat/trans/cml.plan.toml` | 2 | 10.3 | 8.08 | `print_and_recompile` |
| `feat/opt/gate_value.plan.toml` | 4 | 8.4 | 2.21 | `covered_value.graph.true` |
| `feat/modules/tiles.plan.toml` | 3 | 8.1 | 2.92 | `gpu_scope.placement_with_a_device` |
| `feat/consistency/match.plan.toml` | 3 | 8.0 | 2.72 | `match.complete.prl` |
| `feat/gate/conformance.plan.toml` | 1 | 7.8 | 7.83 | `gate.conformance_matrix` |
| `feat/modules/checkpoints.plan.toml` | 1 | 7.5 | 7.55 | `native_state_checkpoints` |
| `feat/bindings/python_binding.plan.toml` | 1 | 7.3 | 7.35 | `python_binding.session` |
| `feat/tools/exit_reporting.plan.toml` | 1 | 7.1 | 7.07 | `runner.exit_reporting_names_the_signal` |
| `feat/mm/process_state.plan.toml` | 2 | 6.8 | 3.81 | `process_state.stw_give_up_escalates` |
| `feat/gate/plan_inventory.plan.toml` | 1 | 6.7 | 6.67 | `gate.plan_inventory` |
| `feat/gate/case_ledger.plan.toml` | 1 | 6.5 | 6.52 | `gate.case_ledger` |
| `feat/tools/provenance.plan.toml` | 1 | 6.1 | 6.11 | `runner.provenance_line_names_the_artifact` |
| `feat/opt/effects_kept.plan.toml` | 2 | 5.4 | 2.79 | `pass_written.inventory.nvm` |
| `feat/compile/numeric_permissions.plan.toml` | 2 | 4.7 | 2.37 | `library_gemm.tolerance` |
| `feat/modules/rules.plan.toml` | 1 | 4.5 | 4.52 | `rules.the_coverage_check_refuses_every_refuted_law` |
| `feat/opt/progen_simplify.plan.toml` | 2 | 4.1 | 2.09 | `simplify_arm_write_lost.simplify_fvm` |
| `feat/trans/canonical_gir.plan.toml` | 1 | 3.9 | 3.87 | `canonical_document_and_equivalence` |
| `feat/compile/return_type.plan.toml` | 1 | 3.7 | 3.68 | `nested_return.compile_time` |
| `feat/tools/build_stamp.plan.toml` | 1 | 3.5 | 3.51 | `build_stamp.options_change` |
| `feat/tools/portability.plan.toml` | 1 | 3.5 | 3.48 | `portability.std_includes` |
| `feat/compile/collections.plan.toml` | 1 | 3.2 | 3.20 | `collfuzz.quick` |
| `feat/compile/diag_fixes.plan.toml` | 1 | 2.7 | 2.68 | `fixes_apply` |
| `feat/opt/inline_cycle.plan.toml` | 1 | 2.5 | 2.50 | `inline_cycle.splice_place` |
| `feat/opt/defun.plan.toml` | 1 | 2.5 | 2.46 | `defun.four_backends` |
| `feat/opt/control.plan.toml` | 1 | 2.4 | 2.37 | `control.pipeline_contract` |
| `feat/compile/diag_options.plan.toml` | 1 | 2.3 | 2.33 | `warn.werror_pass_warning` |
| `feat/mm/graph_space.plan.toml` | 1 | 2.3 | 2.29 | `graph_space.function_value_keeps_program` |
| `feat/tools/clean.plan.toml` | 1 | 2.3 | 2.26 | `clean.tiers` |
| `feat/opt/pass_params.plan.toml` | 1 | 2.1 | 2.12 | `docs.word_form_only` |
| `feat/opt/graph_faces.plan.toml` | 1 | 2.1 | 2.10 | `graph_faces.readings_absolute` |
| `feat/exemplars.plan.toml` | 1 | 2.0 | 2.05 | `01_scalar_boundary` |
| `feat/compile/generics.plan.toml` | 1 | 2.0 | 2.02 | `dependent_names` |
| `feat/opt/inline.plan.toml` | 1 | 2.0 | 2.01 | `arm_call_then_effect.opt.nvm` |
| `feat/compile/with_defaults.plan.toml` | 1 | 2.0 | 2.00 | `with_defaults.applied.prl` |

### 名单：最慢的 25 条，逐条

| # | 用例 | 计划 | 子进程墙钟(s) |
|---:|---|---|---:|
| 1 | `agree_with_fvm` | `feat/trans/cpp.plan.toml` | 81.34 |
| 2 | `every_expect_program_links_no_runtime` | `feat/trans/cpp.plan.toml` | 78.64 |
| 3 | `debugger.dap_fusion_group` | `feat/tools/debugger.plan.toml` | 60.79 |
| 4 | `runner.backend_matrix` | `feat/consistency/runner.plan.toml` | 33.27 |
| 5 | `every_fault_program_says_what_fvm_says` | `feat/trans/cpp.plan.toml` | 32.53 |
| 6 | `root_window.residual_mlp.moving_stress` | `feat/mm/gc_root_window.plan.toml` | 25.36 |
| 7 | `type_text.roundtrip` | `feat/compile/type_text.plan.toml` | 24.11 |
| 8 | `registry.docs_complete` | `feat/modules/stdlib_registry.plan.toml` | 20.32 |
| 9 | `docs.snippets_compile` | `feat/docs_links.plan.toml` | 19.73 |
| 10 | `tools.find_fix_asks_who_already_fixed_it` | `feat/tools/find_fix.plan.toml` | 18.19 |
| 11 | `narrow_reductions_needs_no_runtime` | `feat/trans/cpp.plan.toml` | 15.60 |
| 12 | `catalog.doc_is_generated` | `feat/compile/diag_catalog.plan.toml` | 14.95 |
| 13 | `ort_entry_export` | `feat/modules/onnx.plan.toml` | 14.55 |
| 14 | `ort_time_axis` | `feat/modules/onnx.plan.toml` | 14.36 |
| 15 | `runner.tier_declared_against_started` | `feat/tools/layer_accounting.plan.toml` | 13.97 |
| 16 | `load_class.shared_place` | `feat/tools/load_class.plan.toml` | 12.82 |
| 17 | `ort_core_slice` | `feat/modules/onnx.plan.toml` | 12.66 |
| 18 | `cli.plan_arg_domain_refused` | `feat/tools/cli.plan.toml` | 11.86 |
| 19 | `import.audit` | `feat/modules/import_cost.plan.toml` | 11.79 |
| 20 | `type_identity.corpus` | `feat/compile/type_identity.plan.toml` | 11.68 |
| 21 | `tools.autodiff_migration_ledger.ratchet` | `feat/tools/autodiff_migration_ledger.plan.toml` | 11.47 |
| 22 | `cli.plan_stdout_matches` | `feat/tools/cli.plan.toml` | 10.81 |
| 23 | `ort_loop_values` | `feat/modules/onnx.plan.toml` | 10.06 |
| 24 | `ort_struct_loop` | `feat/modules/onnx.plan.toml` | 9.69 |
| 25 | `capacity.growth_simplify_gc_stress` | `feat/mm/capacity.plan.toml` | 9.67 |

（全部 261 条逐条见**附录 B**。）

## ⑥ 读不到的，明说

1. **CPU 时间：一条都没有。** 905 个跑、3,187 个 `meta.json` 里 `cpu_ms` 全为 `null`。本页所有秒数都是墙钟。
2. **11-12 之后没有全快层读数。** 近 12 天没有一次 `tier=fast` 打全 `smoke` + `feat` 的记录；
   今天 648 条快层用例（6.5%）在记录里没有任何耗时。
3. **CI 侧只有 2026-10-24 的一次**（`ci-failures.md`，run `40077350582`，`tests: 5956`），
   超出近月窗口，且 CI 只在打标签时跑，不能当"近月红次"的样本。
4. **`summary.json` 丢了 303 条用例的计划身份。** 今天有 **14 个计划文件**没有 `id`（它们名下 369 条
   用例），而记录写的是 `plan: entry.plan.id`，`JSON.stringify` 把 `undefined` 丢了——于是那 303 条
   在 `summary.json` 里没有 `plan` 键，只能靠 `meta.json` 的 `plan_path` 认回来。本页认回来了。
   303 是 11-12 那一次的读数，369 是今天的：中间树长大了（`transforms` 那一个计划今天就占 216 条）。
   同一处还有第二个后果：这些用例的日志目录叫 `undefined/`，`logPathOf` 拿到的是同一个 `undefined`。
5. **派生副本的 `meta.json` 里 `plan_path` 与 `plan_id` 指向两个不同的计划。** `plan_id` 是
   `sweep` 那张计划、`plan_path` 是它复制的来源计划（`test.__planPath` 被 `...test` 带过来）。
   两处都是"计划的身份"，却是两个答案——按 `plan_path` 统计会把副本的成本记到来源计划头上
   （本页改用 `summary.json` 的 `plan` 字段就是为了避开这个）。这是一个记录缺陷，不属于本页范围。
6. **6 条记录落不到今天的树上**：4 条属于已删掉的 `opt.keep_node`，2 条改名了。

## ⑦ 没做的事

- **没有改 `test/` 下任何东西**，没有改 `run-tests.mjs`，没有移任何用例出快层，没有加时间预算。
- **没有改任何 `tier`。** §⑨ 的分诊只是表：三档的去向等 ① 的 CPU 仪器到位后由合并位走。
- **没有跑测试。** 本页的每个秒数都来自记录；没有"现在跑一遍"冒充历史读数。盘点用的解析脚本
  放在 `%TEMP%` 下、只读，用完即弃，没有进仓库。
- **没有在本机构建**（本机构建会蓝屏），因此也没有产生新的跑测记录。
- **没有推任何分支**，`develop` 未动（铁律 13）。
- 用途归类里 7 条没有硬塞（④），`--gc-check` / `--gc-stress` 那 155 条的取舍留给负责人裁定。
- 那 11 条常年红的用例**没有修**，只按 `D-1124-3` 的 ④ 在 `internal/backlog.md` 开了 11 行
  （`C-1124-57` … `C-1124-67`），把每题的证据、能不能修、缺什么写在那里。

## ⑧ 补：按提交号命名的计划，与"像提交号"的判断

这是 `D-1124-3` 的 ⑥，补上一轮自我批评里提到的那一版误判。

**答案：计划文件名里含"像提交号"的十六进制串的，全树 1 个。**

| 计划 | 串 | 长度 | 占满整段名字 | 是提交吗（git 判） |
|---|---|---:|---|---|
| `test/plans/feat/opt/adb67e081.plan.toml` | `adb67e081` | 9 | 是 | **是** —— `adb67e081241` |

**判据：正则只提议候选，git 判是不是提交。**

1. 正则 `[0-9a-f]{7,40}` 只负责**提出候选**（7 是 git 自己的默认缩写长度）。
2. **判据是 `git rev-parse --verify --quiet <token>^{commit}`。** 解得出一笔提交的才算提交号。
   这是唯一能把 `adb67e081`（解得出 `adb67e081241`）与 `9223372036854775807`（解不出）分开的判据
   ——两者的形状一模一样。
3. 只靠正则会把三类东西误当成提交号，上面那条判据把它们排掉：
   - **纯十进制**：`1000000`（轮数上限）、`9223372036854775807` 与 `9223372036854775808`
     （int64 边界值）全由 0–9 组成，不是 id；
   - **嵌在更长名字里的尾数**：`boundary32.1.401298464324817e-45` 里的 `401298464324817e`
     是一个浮点字面量的一部分；
   - **带点的版本号**：`1.2.3` 每段都不到 7 位，正则根本不提它，所以这一类不需要判据。

**用例名里的同一件事**，一并报（上一轮那 24 条就是这么来的）：命中 39 处、6 个不同的串、4 个计划。

| 串 | 命中 | 长度 | 全是数字 | 是提交 |
|---|---:|---:|---|---|
| `adb67e081` | 9 | 9 | 否 | **是** |
| `401298464324817e` | 8 | 16 | 否 | 否（`1.401298464324817e-45` 的尾数） |
| `006492321624085e` | 8 | 16 | 否 | 否（`7.006492321624085e-45` 的尾数） |
| `1000000` | 6 | 7 | 是 | 否（轮数） |
| `9223372036854775807` | 4 | 19 | 是 | 否（int64 边界） |
| `9223372036854775808` | 4 | 19 | 是 | 否（int64 边界） |

所以"名字里带提交号"的用例是 **9 条、2 个计划**：

- `feat/opt/adb67e081.plan.toml` 的 6 条（`adb67e081.rc03_seed30_*`、`adb67e081.rc04_seed72_*`）；
- `feat/compile/fold_arms.plan.toml` 的 3 条（`fold_arms.adb67e081_reference` / `_fold` / `_std_opt`）
  ——这是重构计划第 2 条说的那种情况：一笔提交的读数被并进了它所属特性的计划。

`feat/opt/algebra` 的 24 条（8 + 8 + 4 + 4）**全部**是上表里"不是提交"的那四个串，正是上一轮误归成
"防回归"的那 24 条；改正后它们是正确性用例，已体现在 §④ 的计数里。

## ⑨ 261 条超 2 s 墙钟的逐条分诊

**这一节不改任何 `tier`。** 分诊只是表；改动等 ① 的 CPU 仪器到位后由合并位走——否则我们会拿墙钟
当 CPU 用。分诊分的是"**这条用例的时间花在什么上**"，不是"它该不该存在"。

### 三档的定义（先定定义，再看数）

| 档 | 定义 | 怎么处置 |
|---|---|---|
| **A 留快层** | 秒数来自**同一份用例在本层的多副本**（跨后端 / 跨遍展开）或**重复跑的轮数**，不是这条用例自己的活 | 按重构计划第 3 条把副本收成每类一个代表，其余留矩阵层 |
| **B 进 slow** | 成本是**环境或语料规模**：要起 C++ 编译器、要起 onnxruntime 会话、要再起一次跑测（嵌套）、要走整棵语料或整份文档、或场景本身按时间做（`millis=` / `rounds=` / 压力模式） | 移到 slow，判据与断言都不动 |
| **C 说不清** | 单程序、单遍、无驱动 | **缺一条 CPU 读数**（或 `--jobs 1` 的单跑读数）才能判，本页不猜 |

### 汇总

| 去向 | 条数 | 墙钟合计 | 占 1,378.8 s |
|---|---:|---:|---:|
| A 留快层（收代表） | 112 | 363.2 s | 26.3% |
| B 进 slow | 106 | 906.3 s | 65.7% |
| C 说不清 | 43 | 109.3 s | 7.9% |
| 合计 | 261 | 1,378.7 s | 100% |

### A：112 条落在 40 个"用例族"上，收代表后 363.2 s → 149.1 s（省 214.1 s）

| 族（计划 / 用例文件） | 副本数 | 现在 | 一个代表 |
|---|---:|---:|---:|
| `feat/opt/algebra` / `algebra_boundary.cml` | 16 | 37.9 s | 2.6 s |
| `feat/consistency/attempt` / `pipelines.cml` | 6 | 31.4 s | 6.1 s |
| `feat/consistency/attempt` / `resource_release.cml` | 4 | 25.4 s | 6.4 s |
| `feat/opt/algebra` / `algebra_boundary32.cml` | 11 | 23.4 s | 2.3 s |
| `feat/opt/algebra` / `algebra.cml` | 10 | 23.1 s | 2.6 s |
| `feat/consistency/attempt` / `fault_in_argument.cml` | 4 | 20.4 s | 6.1 s |
| `feat/opt/inline_use` / `inline_keeps_stdout.cml` | 3 | 19.9 s | 6.7 s |
| `feat/consistency/values` / `deep_enum_chain.cml` | 4 | 12.6 s | 5.0 s |
| `feat/consistency/attempt` / `many.cml` | 3 | 11.4 s | 4.2 s |
| `feat/consistency/attempt` / `siblings.cml` | 3 | 8.4 s | 3.5 s |
| `feat/opt/control_data` / `control_unused_argument.cml` | 4 | 8.3 s | 2.2 s |
| `feat/consistency/match` / `complete_matches.cml` | 3 | 8.0 s | 2.7 s |

这 12 族占 A 的 363.2 s 里的 241.2 s。**注意收代表之后多数仍在 2 s 上下**（2.2–6.7 s），所以收代表
不是"变快"，是"不再把同一份程序的 16 个副本都算进快层的账"；留下的那个代表本身可能仍然超预算
——那是 C 的问题，要 CPU 读数。

### B：106 条压着 906.3 s（三分之二），只来自 43 个计划

- 按工具：**python 驱动 48 条 + node 驱动 44 条**，真正跑 `camel` 的只有 14 条。
- 最贵的四条：`feat/trans/cpp` 的 `agree_with_fvm` 81.34 s（每条要 C++ 编译 + 链接）、
  `every_expect_program_links_no_runtime` 78.64 s（同上，还带 `--standalone`）、
  `feat/tools/debugger` 的 `debugger.dap_fusion_group` 60.79 s（起一个真的 DAP 会话）、
  `feat/consistency/runner` 的 `runner.backend_matrix` 33.27 s（自己再跑一次整个后端矩阵）。
- 整条 `feat/modules/onnx`（24 条、152.0 s）都在 B：每条都要起一个 onnxruntime 会话。
- **这条读数与"快层慢在引擎"的直觉相反**：快层的时间大头是那些**替整棵树做事的驱动测试**
  （文档片段、诊断目录、注册表、迁移账本、可移植性、clean、层记账），不是某个慢的程序。
  这也解释了为什么 A 的"提速"空间只有 363 s，而 B 的 906 s 只能靠换层或降频次。

### C：43 条（109.3 s）说不清，缺的是 CPU

43 条都是 camel 单程序、单遍、无驱动。它们的 `queue_ms` 从 0 到 350 s 都有，**但这个字段只说明
它什么时候开始，不说明它跑的时候有几个在跑**，所以它不能替我判这一档。要判它们只需要一件事：
一条 `cpu_ms`（或同一棵树 `--jobs 1` 的单跑墙钟）。

261 条逐条见**附录 B**：计划、用例、子进程墙钟、去向、依据。

## 附录 A：全表（420 个计划）

列：`条目` = `[[tests]]` 数；`展开` = 后端展开后的用例数（含 `sweeps` 派生副本）；
`快层` = 今天是快层的用例数；`记录用例` = 2026-11-12 那次跑里跑到的条数；`墙钟(s)` = 子进程墙钟求和；
`slot(s)` = 占槽时间求和；`最慢(s)` = 该计划最慢一条；`用途（快层内）` = 该计划快层用例按 ④ 归类的分布，
形如 `正确性192`；`—` = 记录里没有读数。

| 计划 | 条目 | 展开 | 快层 | 记录用例 | 墙钟(s) | slot(s) | 最慢(s) | 用途（快层内） |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `debug/camel_pass_commit_checks.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `debug/view_escape.plan.toml` | 2 | 2 | 0 | 0 | — | — | — |  |
| `feat/bindings/python_binding.plan.toml` | 2 | 2 | 2 | 2 | 8.6 | 8.7 | 7.35 | 正确性2 |
| `feat/compile/array_alias.plan.toml` | 6 | 6 | 6 | 6 | 6.6 | 6.9 | 1.27 | 正确性6 |
| `feat/compile/array_shape/array_shape.plan.toml` | 87 | 88 | 88 | 88 | 32.3 | 34.0 | 1.17 | 正确性88 |
| `feat/compile/branch_exit.plan.toml` | 4 | 10 | 10 | 10 | 6.9 | 7.1 | 1.06 | 正确性10 |
| `feat/compile/broadcast_product_gradient.plan.toml` | 2 | 2 | 2 | 2 | 1.8 | 1.9 | 1.14 | 正确性2 |
| `feat/compile/call_syntax.plan.toml` | 1 | 4 | 4 | 4 | 4.0 | 4.2 | 1.14 | 正确性4 |
| `feat/compile/capture_order.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/compile/casts.plan.toml` | 17 | 17 | 17 | 17 | 8.6 | 8.9 | 0.82 | 正确性17 |
| `feat/compile/chain.plan.toml` | 4 | 4 | 4 | 0 | — | — | — | 正确性4 |
| `feat/compile/check_command.plan.toml` | 12 | 12 | 12 | 12 | 8.0 | 8.2 | 0.86 | 正确性12 |
| `feat/compile/collections.plan.toml` | 86 | 143 | 143 | 143 | 68.3 | 70.5 | 3.20 | 正确性143 |
| `feat/compile/comprehension_context.plan.toml` | 1 | 3 | 3 | 3 | 1.1 | 1.1 | 0.46 | 正确性3 |
| `feat/compile/constas.plan.toml` | 22 | 22 | 22 | 22 | 13.7 | 13.9 | 1.70 | 正确性22 |
| `feat/compile/contextual_empty_array.plan.toml` | 3 | 3 | 3 | 3 | 3.6 | 3.8 | 1.29 | 正确性3 |
| `feat/compile/conversions.plan.toml` | 2 | 4 | 4 | 4 | 1.4 | 1.4 | 0.39 | 正确性4 |
| `feat/compile/core.plan.toml` | 10 | 10 | 10 | 10 | 3.8 | 3.9 | 0.58 | 正确性10 |
| `feat/compile/decorators.plan.toml` | 10 | 16 | 16 | 16 | 16.6 | 17.3 | 1.87 | 正确性16 |
| `feat/compile/diag_catalog.plan.toml` | 10 | 10 | 10 | 10 | 31.1 | 31.5 | 14.95 | 正确性10 |
| `feat/compile/diag_fixes.plan.toml` | 1 | 1 | 1 | 1 | 2.7 | 2.8 | 2.68 | 正确性1 |
| `feat/compile/diag_json.plan.toml` | 24 | 24 | 24 | 24 | 20.9 | 21.7 | 1.67 | 正确性24 |
| `feat/compile/diag_options.plan.toml` | 38 | 38 | 38 | 38 | 32.5 | 33.9 | 2.33 | 正确性38 |
| `feat/compile/diag_render.plan.toml` | 37 | 37 | 37 | 37 | 25.6 | 26.6 | 1.49 | 正确性37 |
| `feat/compile/effects_syntax.plan.toml` | 59 | 87 | 87 | 68 | 37.7 | 38.9 | 1.36 | 正确性87 |
| `feat/compile/entry_args.plan.toml` | 83 | 83 | 83 | 83 | 34.8 | 36.1 | 0.93 | 正确性83 |
| `feat/compile/enum.plan.toml` | 23 | 32 | 32 | 32 | 24.0 | 24.7 | 1.43 | 正确性32 |
| `feat/compile/equality.plan.toml` | 12 | 20 | 20 | 20 | 5.8 | 6.0 | 0.52 | 正确性20 |
| `feat/compile/escapes.plan.toml` | 8 | 8 | 8 | 8 | 6.9 | 7.1 | 1.16 | 正确性8 |
| `feat/compile/expected_type_loop.plan.toml` | 1 | 2 | 2 | 2 | 0.6 | 0.6 | 0.40 | 正确性2 |
| `feat/compile/expected_type_through_calls.plan.toml` | 5 | 14 | 14 | 14 | 4.7 | 5.0 | 0.73 | 正确性14 |
| `feat/compile/extent_expressions.plan.toml` | 9 | 21 | 21 | 21 | 7.1 | 7.5 | 0.70 | 正确性21 |
| `feat/compile/extent_of.plan.toml` | 7 | 11 | 11 | 11 | 7.9 | 8.1 | 1.45 | 正确性11 |
| `feat/compile/fold_arms.plan.toml` | 5 | 5 | 5 | 5 | 1.8 | 1.8 | 0.68 | 正确性5 |
| `feat/compile/fold_calls.plan.toml` | 9 | 11 | 11 | 11 | 7.6 | 8.1 | 1.06 | 正确性11 |
| `feat/compile/fstring.plan.toml` | 13 | 31 | 31 | 31 | 25.1 | 25.8 | 1.49 | 正确性31 |
| `feat/compile/function_value_positions.plan.toml` | 5 | 8 | 8 | 8 | 2.3 | 2.6 | 0.52 | 正确性8 |
| `feat/compile/function_values.plan.toml` | 19 | 19 | 19 | 19 | 12.9 | 13.1 | 1.76 | 正确性19 |
| `feat/compile/generic_head.plan.toml` | 8 | 8 | 8 | 8 | 5.0 | 5.3 | 0.80 | 正确性8 |
| `feat/compile/generic_join.plan.toml` | 1 | 2 | 2 | 2 | 0.6 | 0.6 | 0.50 | 正确性2 |
| `feat/compile/generics_rank_zero.plan.toml` | 6 | 6 | 6 | 6 | 5.4 | 5.7 | 1.07 | 正确性6 |
| `feat/compile/generics.plan.toml` | 61 | 65 | 65 | 65 | 64.7 | 66.0 | 2.02 | 正确性65 |
| `feat/compile/gir.plan.toml` | 4 | 4 | 4 | 4 | 2.1 | 2.1 | 0.61 | 正确性4 |
| `feat/compile/graph_read.plan.toml` | 1 | 1 | 1 | 1 | 0.7 | 0.7 | 0.67 | 正确性1 |
| `feat/compile/higher_order_clause.plan.toml` | 4 | 4 | 4 | 0 | — | — | — | 正确性4 |
| `feat/compile/impl.plan.toml` | 37 | 76 | 76 | 76 | 52.1 | 53.9 | 1.45 | 正确性76 |
| `feat/compile/inferred_resources.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 正确性3 |
| `feat/compile/lambda_inference.plan.toml` | 15 | 15 | 15 | 15 | 14.6 | 15.0 | 1.59 | 正确性15 |
| `feat/compile/lambda_tuple_param.plan.toml` | 4 | 7 | 7 | 7 | 4.3 | 4.5 | 0.84 | 正确性7 |
| `feat/compile/literal_context.plan.toml` | 56 | 95 | 95 | 95 | 62.1 | 64.3 | 1.27 | 正确性95 |
| `feat/compile/literals.plan.toml` | 23 | 32 | 32 | 32 | 23.4 | 24.3 | 1.62 | 正确性32 |
| `feat/compile/match_guard.plan.toml` | 8 | 11 | 11 | 11 | 7.7 | 8.1 | 0.98 | 正确性11 |
| `feat/compile/match_type_array.plan.toml` | 3 | 5 | 5 | 5 | 1.7 | 1.8 | 0.40 | 正确性5 |
| `feat/compile/match_type_guard.plan.toml` | 6 | 10 | 10 | 0 | — | — | — | 正确性10 |
| `feat/compile/module_imports.plan.toml` | 52 | 52 | 52 | 52 | 13.9 | 14.5 | 0.52 | 正确性52 |
| `feat/compile/module_load.plan.toml` | 14 | 14 | 14 | 14 | 4.1 | 4.3 | 0.49 | 正确性14 |
| `feat/compile/module_operators.plan.toml` | 10 | 10 | 10 | 10 | 2.9 | 3.0 | 0.79 | 正确性10 |
| `feat/compile/module_root.plan.toml` | 18 | 18 | 18 | 18 | 5.9 | 6.1 | 0.57 | 正确性18 |
| `feat/compile/modules_213.plan.toml` | 6 | 9 | 9 | 9 | 3.3 | 3.6 | 1.10 | 正确性9 |
| `feat/compile/namespace_rules.plan.toml` | 36 | 36 | 36 | 36 | 10.5 | 10.9 | 0.57 | 正确性36 |
| `feat/compile/namespace.plan.toml` | 9 | 9 | 9 | 9 | 4.7 | 4.9 | 0.83 | 正确性9 |
| `feat/compile/native_declarations.plan.toml` | 119 | 119 | 119 | 119 | 41.0 | 42.5 | 0.72 | 正确性119 |
| `feat/compile/native_types.plan.toml` | 4 | 4 | 4 | 4 | 0.9 | 1.0 | 0.29 | 正确性4 |
| `feat/compile/not_supported.plan.toml` | 4 | 4 | 4 | 4 | 1.3 | 1.4 | 0.42 | 正确性4 |
| `feat/compile/nullable_assign.plan.toml` | 2 | 6 | 6 | 6 | 2.1 | 2.2 | 0.55 | 正确性6 |
| `feat/compile/numeric_permissions.plan.toml` | 20 | 23 | 23 | 23 | 27.8 | 30.5 | 2.37 | 正确性23 |
| `feat/compile/operators.plan.toml` | 17 | 17 | 17 | 17 | 16.3 | 17.0 | 1.71 | 正确性17 |
| `feat/compile/overloads.plan.toml` | 21 | 24 | 24 | 20 | 13.9 | 14.5 | 1.27 | 正确性24 |
| `feat/compile/param_defaults.plan.toml` | 22 | 31 | 31 | 31 | 14.7 | 15.4 | 1.14 | 正确性31 |
| `feat/compile/pipe_propagation.plan.toml` | 6 | 9 | 9 | 9 | 4.2 | 4.4 | 0.65 | 正确性9 |
| `feat/compile/precedence.plan.toml` | 6 | 6 | 6 | 6 | 5.3 | 5.6 | 1.07 | 正确性6 |
| `feat/compile/prelude_signatures.plan.toml` | 5 | 5 | 5 | 5 | 7.8 | 7.8 | 1.64 | 正确性5 |
| `feat/compile/range_functions.plan.toml` | 5 | 5 | 5 | 0 | — | — | — | 正确性5 |
| `feat/compile/recursive_struct.plan.toml` | 3 | 6 | 6 | 6 | 1.2 | 1.2 | 0.26 | 正确性4 压力2 |
| `feat/compile/ref.plan.toml` | 28 | 79 | 79 | 0 | — | — | — | 正确性46 压力33 |
| `feat/compile/refwrite.plan.toml` | 3 | 9 | 9 | 0 | — | — | — | 正确性9 |
| `feat/compile/region_roots.plan.toml` | 11 | 26 | 26 | 0 | — | — | — | 正确性26 |
| `feat/compile/rest_params.plan.toml` | 28 | 31 | 31 | 26 | 9.5 | 10.0 | 0.63 | 正确性31 |
| `feat/compile/return_after.plan.toml` | 5 | 5 | 5 | 5 | 5.1 | 5.2 | 1.23 | 正确性5 |
| `feat/compile/return_type.plan.toml` | 7 | 7 | 7 | 7 | 5.2 | 12.0 | 3.68 | 正确性7 |
| `feat/compile/rules_angle_args.plan.toml` | 10 | 19 | 19 | 8 | 3.9 | 4.1 | 0.69 | 正确性19 |
| `feat/compile/rulesets.plan.toml` | 38 | 76 | 76 | 67 | 37.2 | 38.7 | 1.17 | 正确性76 |
| `feat/compile/state_escape.plan.toml` | 12 | 12 | 12 | 0 | — | — | — | 正确性12 |
| `feat/compile/stated_extents.plan.toml` | 23 | 23 | 23 | 20 | 11.8 | 12.7 | 0.99 | 正确性23 |
| `feat/compile/static_expansion.plan.toml` | 85 | 88 | 88 | 83 | 43.9 | 45.5 | 1.00 | 正确性88 |
| `feat/compile/static_field_tags.plan.toml` | 5 | 5 | 5 | 5 | 1.7 | 1.8 | 0.41 | 正确性5 |
| `feat/compile/stream.plan.toml` | 112 | 157 | 157 | 147 | 42.7 | 44.2 | 0.62 | 正确性157 |
| `feat/compile/struct_entry.plan.toml` | 1 | 2 | 2 | 2 | 1.3 | 1.3 | 0.66 | 正确性2 |
| `feat/compile/struct_join.plan.toml` | 3 | 5 | 5 | 5 | 1.8 | 1.9 | 0.48 | 正确性5 |
| `feat/compile/surface.plan.toml` | 24 | 24 | 24 | 24 | 13.2 | 13.9 | 0.92 | 正确性24 |
| `feat/compile/tag_permissions.plan.toml` | 5 | 7 | 7 | 7 | 5.1 | 5.1 | 1.41 | 正确性7 |
| `feat/compile/tagged.plan.toml` | 63 | 66 | 66 | 63 | 60.4 | 62.8 | 1.84 | 正确性66 |
| `feat/compile/tags.plan.toml` | 42 | 49 | 49 | 49 | 34.3 | 35.2 | 1.55 | 正确性48 未归类1 |
| `feat/compile/transforms.plan.toml` | 191 | 216 | 216 | 198 | 73.3 | 76.1 | 1.63 | 正确性216 |
| `feat/compile/transpose_array_product.plan.toml` | 2 | 2 | 2 | 2 | 0.6 | 0.7 | 0.34 | 正确性2 |
| `feat/compile/transpose_product_fixed_shape.plan.toml` | 1 | 1 | 1 | 1 | 0.3 | 0.3 | 0.25 | 正确性1 |
| `feat/compile/type_array.plan.toml` | 2 | 2 | 2 | 2 | 2.9 | 3.0 | 1.48 | 正确性2 |
| `feat/compile/type_identity.plan.toml` | 10 | 10 | 10 | 10 | 17.9 | 18.4 | 11.68 | 正确性10 |
| `feat/compile/type_packs.plan.toml` | 13 | 25 | 25 | 25 | 8.8 | 9.3 | 0.67 | 正确性25 |
| `feat/compile/type_text.plan.toml` | 1 | 1 | 1 | 1 | 24.1 | 24.2 | 24.11 | 正确性1 |
| `feat/compile/typing_any.plan.toml` | 11 | 11 | 11 | 11 | 15.5 | 27.2 | 1.54 | 正确性11 |
| `feat/compile/unused_values.plan.toml` | 17 | 17 | 17 | 17 | 25.7 | 26.0 | 4.84 | 正确性17 |
| `feat/compile/utf8_strings.plan.toml` | 5 | 8 | 8 | 8 | 3.9 | 6.6 | 0.74 | 正确性8 |
| `feat/compile/var_closure.plan.toml` | 12 | 12 | 12 | 0 | — | — | — | 正确性12 |
| `feat/compile/var.plan.toml` | 35 | 41 | 41 | 0 | — | — | — | 正确性41 |
| `feat/compile/when_arity.plan.toml` | 2 | 4 | 4 | 4 | 1.1 | 1.1 | 0.36 | 正确性4 |
| `feat/compile/when_import.plan.toml` | 9 | 9 | 9 | 9 | 7.8 | 8.1 | 1.10 | 正确性9 |
| `feat/compile/when_overloads.plan.toml` | 9 | 13 | 13 | 13 | 3.7 | 3.9 | 0.68 | 正确性13 |
| `feat/compile/when.plan.toml` | 34 | 35 | 35 | 35 | 16.2 | 16.9 | 1.10 | 正确性35 |
| `feat/compile/with_arguments.plan.toml` | 2 | 4 | 4 | 4 | 1.9 | 1.9 | 0.68 | 正确性4 |
| `feat/compile/with_defaults.plan.toml` | 19 | 26 | 26 | 26 | 17.7 | 18.3 | 2.00 | 正确性26 |
| `feat/compile/with_overloads.plan.toml` | 2 | 4 | 4 | 4 | 1.8 | 1.9 | 0.76 | 正确性4 |
| `feat/consistency/attempt.plan.toml` | 44 | 148 | 148 | 148 | 245.1 | 255.5 | 6.44 | 正确性139 压力9 |
| `feat/consistency/cleanup.plan.toml` | 9 | 36 | 36 | 36 | 12.9 | 13.4 | 0.66 | 正确性36 |
| `feat/consistency/comprehension.plan.toml` | 12 | 18 | 18 | 18 | 4.5 | 4.6 | 0.40 | 正确性18 |
| `feat/consistency/declared_type.plan.toml` | 2 | 5 | 5 | 5 | 2.4 | 2.5 | 0.73 | 正确性5 |
| `feat/consistency/decorated_overloads_export.plan.toml` | 4 | 13 | 13 | 13 | 6.1 | 6.4 | 1.08 | 正确性13 |
| `feat/consistency/entering.plan.toml` | 44 | 86 | 86 | 86 | 25.0 | 26.1 | 0.59 | 正确性86 |
| `feat/consistency/entry.plan.toml` | 13 | 46 | 46 | 46 | 33.6 | 34.8 | 1.46 | 正确性46 |
| `feat/consistency/fail_never.plan.toml` | 20 | 23 | 23 | 23 | 9.7 | 10.0 | 0.85 | 正确性23 |
| `feat/consistency/faults_optimized.plan.toml` | 44 | 176 | 176 | 176 | 68.2 | 70.9 | 1.04 | 正确性176 |
| `feat/consistency/faults.plan.toml` | 44 | 176 | 176 | 176 | 53.4 | 55.3 | 0.62 | 正确性176 |
| `feat/consistency/generic_values.plan.toml` | 17 | 26 | 26 | 26 | 10.3 | 10.6 | 0.95 | 正确性26 |
| `feat/consistency/if_without_else.plan.toml` | 11 | 17 | 17 | 17 | 10.5 | 11.1 | 1.06 | 正确性17 |
| `feat/consistency/imported_value.plan.toml` | 2 | 8 | 8 | 8 | 4.1 | 4.4 | 0.79 | 正确性8 |
| `feat/consistency/limits.plan.toml` | 48 | 144 | 144 | 144 | 57.6 | 59.9 | 0.89 | 正确性144 |
| `feat/consistency/logical_branch.plan.toml` | 9 | 15 | 15 | 15 | 7.5 | 8.0 | 1.00 | 正确性15 |
| `feat/consistency/match.plan.toml` | 5 | 17 | 17 | 17 | 15.9 | 16.2 | 2.72 | 正确性17 |
| `feat/consistency/narrowing.plan.toml` | 15 | 24 | 24 | 24 | 9.0 | 9.4 | 0.81 | 正确性24 |
| `feat/consistency/new_values.plan.toml` | 39 | 58 | 58 | 51 | 24.3 | 25.2 | 1.11 | 正确性47 压力11 |
| `feat/consistency/operator_overloads.plan.toml` | 28 | 43 | 43 | 43 | 18.1 | 18.7 | 0.89 | 正确性43 |
| `feat/consistency/partial_application.plan.toml` | 18 | 27 | 27 | 33 | 11.4 | 11.7 | 0.61 | 正确性27 |
| `feat/consistency/print_formats.plan.toml` | 1 | 4 | 4 | 4 | 3.5 | 3.5 | 1.02 | 正确性4 |
| `feat/consistency/runner.plan.toml` | 1 | 1 | 1 | 1 | 33.3 | 33.3 | 33.27 | 正确性1 |
| `feat/consistency/snapshot_state.plan.toml` | 2 | 5 | 5 | 0 | — | — | — | 正确性5 |
| `feat/consistency/surface.plan.toml` | 5 | 20 | 20 | 20 | 40.0 | 40.4 | 6.84 | 正确性20 |
| `feat/consistency/unused_fallible.plan.toml` | 6 | 12 | 12 | 12 | 3.7 | 3.9 | 0.74 | 正确性12 |
| `feat/consistency/values.plan.toml` | 36 | 144 | 144 | 136 | 53.1 | 54.6 | 4.96 | 正确性144 |
| `feat/docs_ai_reference.plan.toml` | 9 | 9 | 9 | 9 | 4.6 | 5.3 | 0.85 | 正确性9 |
| `feat/docs_headers.plan.toml` | 2 | 2 | 2 | 2 | 1.6 | 1.8 | 1.47 | 正确性2 |
| `feat/docs_links.plan.toml` | 21 | 21 | 21 | 21 | 39.4 | 50.0 | 19.73 | 正确性21 |
| `feat/docs/tutorial_images.plan.toml` | 2 | 2 | 2 | 2 | 3.0 | 3.1 | 1.52 | 正确性2 |
| `feat/effects/commit_tier_inplace.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/effects/masking_transition.plan.toml` | 6 | 6 | 6 | 0 | — | — | — | 正确性6 |
| `feat/examples.plan.toml` | 34 | 124 | 124 | 112 | 38.3 | 39.8 | 1.26 | 正确性124 |
| `feat/exemplars.plan.toml` | 8 | 8 | 8 | 8 | 13.1 | 13.9 | 2.05 | 正确性8 |
| `feat/format/comments.plan.toml` | 6 | 6 | 6 | 6 | 7.2 | 7.5 | 1.46 | 正确性6 |
| `feat/format/core.plan.toml` | 24 | 24 | 24 | 23 | 17.5 | 18.4 | 1.26 | 正确性24 |
| `feat/format/tags.plan.toml` | 2 | 2 | 2 | 2 | 0.4 | 0.5 | 0.28 | 正确性2 |
| `feat/gate/case_ledger.plan.toml` | 1 | 1 | 1 | 1 | 6.5 | 6.6 | 6.52 | 正确性1 |
| `feat/gate/conformance.plan.toml` | 1 | 1 | 1 | 1 | 7.8 | 7.9 | 7.83 | 正确性1 |
| `feat/gate/plan_inventory.plan.toml` | 1 | 1 | 1 | 1 | 6.7 | 6.7 | 6.67 | 正确性1 |
| `feat/linear/basic_runtime.plan.toml` | 2 | 2 | 2 | 2 | 0.3 | 0.4 | 0.18 | 正确性2 |
| `feat/linear/captured_writes.plan.toml` | 52 | 52 | 52 | 52 | 46.6 | 47.9 | 1.65 | 正确性52 |
| `feat/linear/closures_and_state.plan.toml` | 24 | 27 | 27 | 27 | 26.2 | 26.7 | 1.56 | 正确性27 |
| `feat/linear/collections_and_structs.plan.toml` | 13 | 13 | 13 | 13 | 9.2 | 9.8 | 1.31 | 正确性13 |
| `feat/linear/control_flow_and_ops.plan.toml` | 8 | 8 | 8 | 8 | 10.8 | 11.1 | 1.88 | 正确性8 |
| `feat/linear/effects_model_edges.plan.toml` | 4 | 4 | 4 | 4 | 1.5 | 1.5 | 0.43 | 正确性4 |
| `feat/linear/faults.plan.toml` | 44 | 44 | 44 | 44 | 19.3 | 20.2 | 0.71 | 正确性44 |
| `feat/linear/hof_declared.plan.toml` | 5 | 8 | 8 | 8 | 2.7 | 2.7 | 0.47 | 正确性8 |
| `feat/linear/jit_framed.plan.toml` | 20 | 57 | 51 | 51 | 39.3 | 40.9 | 1.60 | 正确性37 压力14 |
| `feat/linear/jit_native.plan.toml` | 18 | 18 | 18 | 18 | 4.5 | 4.7 | 0.45 | 正确性18 |
| `feat/linear/jit_without_codegen.plan.toml` | 2 | 2 | 2 | 2 | 0.0 | 0.0 | 0.00 | 正确性2 |
| `feat/linear/loop_collect.plan.toml` | 5 | 17 | 17 | 17 | 2.0 | 2.2 | 0.20 | 正确性17 |
| `feat/linear/loops_in_arms.plan.toml` | 7 | 23 | 23 | 23 | 6.3 | 6.5 | 0.56 | 正确性23 |
| `feat/linear/loops.plan.toml` | 69 | 160 | 160 | 144 | 40.6 | 42.5 | 1.06 | 正确性152 压力8 |
| `feat/linear/match_patterns.plan.toml` | 18 | 18 | 18 | 18 | 14.5 | 14.8 | 1.77 | 正确性18 |
| `feat/linear/operators.plan.toml` | 24 | 24 | 24 | 24 | 20.9 | 21.4 | 1.53 | 正确性24 |
| `feat/linear/order_edges_regions.plan.toml` | 6 | 6 | 6 | 6 | 5.3 | 5.4 | 1.00 | 正确性6 |
| `feat/linear/order_edges.plan.toml` | 4 | 4 | 4 | 4 | 2.1 | 2.2 | 0.93 | 正确性4 |
| `feat/linear/recursion_and_calls.plan.toml` | 14 | 14 | 14 | 14 | 6.7 | 6.9 | 1.13 | 正确性14 |
| `feat/linear/ref_handle_capture.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性1 压力1 |
| `feat/linear/runtime_negative.plan.toml` | 16 | 16 | 16 | 16 | 8.5 | 8.7 | 1.17 | 正确性16 |
| `feat/linear/same_resource_order.plan.toml` | 8 | 8 | 8 | 8 | 4.9 | 5.1 | 1.71 | 正确性8 |
| `feat/linear/subscript.plan.toml` | 17 | 30 | 30 | 30 | 21.8 | 22.1 | 1.43 | 正确性30 |
| `feat/linear/sync_order.plan.toml` | 33 | 33 | 33 | 33 | 26.9 | 27.6 | 1.75 | 正确性33 |
| `feat/linear/toplevel_var.plan.toml` | 34 | 34 | 34 | 34 | 28.5 | 29.4 | 1.56 | 正确性34 |
| `feat/linear/var_in_sync.plan.toml` | 13 | 13 | 13 | 13 | 10.9 | 11.0 | 1.29 | 正确性13 |
| `feat/linear/var_read_before_region.plan.toml` | 5 | 5 | 5 | 5 | 3.9 | 3.9 | 0.91 | 正确性5 |
| `feat/linear/var_read_before_write.plan.toml` | 7 | 7 | 7 | 6 | 3.1 | 3.2 | 0.90 | 正确性7 |
| `feat/linear/var_reference.plan.toml` | 220 | 227 | 226 | 258 | 275.1 | 282.3 | 2.14 | 正确性225 压力1 |
| `feat/mm/blocks.plan.toml` | 3 | 3 | 3 | 3 | 1.3 | 1.4 | 0.92 | 正确性3 |
| `feat/mm/capacity.plan.toml` | 18 | 18 | 18 | 18 | 40.2 | 40.9 | 9.67 | 压力18 |
| `feat/mm/complex.plan.toml` | 4 | 4 | 4 | 4 | 1.9 | 2.9 | 0.50 | 正确性4 |
| `feat/mm/context_lifetime.plan.toml` | 2 | 2 | 2 | 2 | 0.9 | 1.0 | 0.52 | 正确性2 |
| `feat/mm/gc_collections.plan.toml` | 73 | 73 | 0 | 0 | — | — | — |  |
| `feat/mm/gc_elder_exhaustion.plan.toml` | 22 | 22 | 0 | 0 | — | — | — |  |
| `feat/mm/gc_pacing.plan.toml` | 15 | 15 | 0 | 0 | — | — | — |  |
| `feat/mm/gc_poison_sweep.plan.toml` | 0 | 4860 | 237 | 215 | 99.9 | 104.3 | 1.89 | 压力237 |
| `feat/mm/gc_root_window.plan.toml` | 4 | 4 | 2 | 2 | 27.9 | 27.9 | 25.36 | 压力2 |
| `feat/mm/gc_soak.plan.toml` | 22 | 22 | 0 | 0 | — | — | — |  |
| `feat/mm/gc.plan.toml` | 46 | 46 | 2 | 2 | 0.5 | 0.6 | 0.28 | 压力2 |
| `feat/mm/graph_space.plan.toml` | 5 | 5 | 5 | 5 | 6.2 | 6.2 | 2.29 | 正确性5 |
| `feat/mm/mem_blocks.plan.toml` | 3 | 6 | 6 | 6 | 1.8 | 1.9 | 0.33 | 正确性6 |
| `feat/mm/process_state.plan.toml` | 5 | 5 | 5 | 5 | 8.0 | 9.3 | 3.81 | 正确性5 |
| `feat/mm/retype.plan.toml` | 2 | 5 | 5 | 5 | 1.0 | 1.0 | 0.28 | 正确性5 |
| `feat/mm/writings.plan.toml` | 28 | 31 | 31 | 30 | 19.3 | 20.4 | 1.19 | 正确性31 |
| `feat/modules/array_conversion.plan.toml` | 1 | 4 | 4 | 4 | 2.8 | 3.0 | 0.91 | 正确性4 |
| `feat/modules/array_kernels.plan.toml` | 5 | 8 | 8 | 8 | 2.0 | 2.2 | 0.44 | 正确性8 |
| `feat/modules/array_merge_kernels.plan.toml` | 3 | 6 | 6 | 6 | 3.5 | 3.6 | 0.71 | 正确性6 |
| `feat/modules/array_reduction_kernels.plan.toml` | 6 | 15 | 15 | 15 | 7.2 | 7.4 | 1.35 | 正确性15 |
| `feat/modules/array_sort_kernels.plan.toml` | 5 | 8 | 8 | 8 | 6.6 | 6.8 | 1.02 | 正确性8 |
| `feat/modules/array_sum_rounding.plan.toml` | 1 | 4 | 4 | 4 | 2.0 | 2.1 | 0.56 | 正确性4 |
| `feat/modules/array_whole.plan.toml` | 1 | 4 | 4 | 4 | 1.2 | 1.4 | 0.45 | 正确性4 |
| `feat/modules/autodiff_inspection.plan.toml` | 14 | 39 | 39 | 39 | 33.7 | 34.8 | 1.85 | 正确性39 |
| `feat/modules/autodiff_value_with.plan.toml` | 7 | 7 | 7 | 7 | 2.9 | 5.1 | 1.87 | 正确性6 未归类1 |
| `feat/modules/autodiff.plan.toml` | 234 | 290 | 285 | 278 | 209.4 | 216.9 | 2.95 | 正确性283 压力2 |
| `feat/modules/bytes.plan.toml` | 5 | 5 | 5 | 5 | 1.8 | 1.8 | 0.42 | 正确性5 |
| `feat/modules/checkpoints.plan.toml` | 4 | 4 | 4 | 4 | 9.2 | 9.4 | 7.55 | 正确性4 |
| `feat/modules/ckpt.plan.toml` | 4 | 4 | 4 | 4 | 1.7 | 1.7 | 0.81 | 正确性4 |
| `feat/modules/ddpm_primitives.plan.toml` | 7 | 21 | 21 | 21 | 11.8 | 12.2 | 0.84 | 正确性21 |
| `feat/modules/definitions.plan.toml` | 11 | 23 | 23 | 23 | 12.4 | 13.0 | 1.70 | 正确性23 |
| `feat/modules/fusion_group.plan.toml` | 13 | 17 | 17 | 17 | 10.3 | 10.6 | 1.00 | 正确性17 |
| `feat/modules/graph_edits.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 正确性3 |
| `feat/modules/graph_entry_graph.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/modules/graph_facts.plan.toml` | 9 | 9 | 9 | 8 | 4.9 | 7.3 | 0.81 | 正确性9 |
| `feat/modules/graph_function.plan.toml` | 3 | 3 | 3 | 3 | 0.7 | 0.8 | 0.29 | 正确性3 |
| `feat/modules/graph_inline.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/modules/graph_outline.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/modules/graph_pass_function.plan.toml` | 3 | 3 | 3 | 3 | 1.5 | 1.5 | 0.72 | 正确性3 |
| `feat/modules/graph_values.plan.toml` | 16 | 47 | 47 | 32 | 13.1 | 13.4 | 1.31 | 正确性47 |
| `feat/modules/graph.plan.toml` | 71 | 80 | 80 | 75 | 37.9 | 39.4 | 1.24 | 正确性78 压力2 |
| `feat/modules/import_cost.plan.toml` | 3 | 3 | 3 | 3 | 12.5 | 12.5 | 11.79 | 正确性3 |
| `feat/modules/index_semantics.plan.toml` | 5 | 5 | 5 | 5 | 2.9 | 2.9 | 0.67 | 正确性5 |
| `feat/modules/interpreter_first_use.plan.toml` | 2 | 2 | 2 | 2 | 1.5 | 1.6 | 0.76 | 正确性2 |
| `feat/modules/io.plan.toml` | 6 | 9 | 9 | 9 | 3.2 | 3.4 | 0.84 | 正确性9 |
| `feat/modules/jax_alignment.plan.toml` | 14 | 34 | 34 | 34 | 16.2 | 16.8 | 0.85 | 正确性34 |
| `feat/modules/json_accessor_error.plan.toml` | 3 | 9 | 9 | 9 | 3.1 | 3.2 | 0.58 | 正确性9 |
| `feat/modules/json_typed.plan.toml` | 5 | 8 | 8 | 8 | 2.8 | 2.9 | 1.09 | 正确性8 |
| `feat/modules/json_value.plan.toml` | 34 | 43 | 43 | 31 | 9.3 | 9.7 | 1.05 | 正确性28 压力15 |
| `feat/modules/math_kernels.plan.toml` | 9 | 14 | 14 | 14 | 4.4 | 4.6 | 0.57 | 正确性14 |
| `feat/modules/module_resolution.plan.toml` | 12 | 12 | 12 | 12 | 9.5 | 10.0 | 1.21 | 正确性12 |
| `feat/modules/naive_in_expert_out.plan.toml` | 1 | 3 | 3 | 3 | 1.6 | 1.7 | 0.70 | 未归类3 |
| `feat/modules/nn.plan.toml` | 63 | 75 | 75 | 68 | 65.0 | 67.0 | 2.19 | 正确性72 压力3 |
| `feat/modules/onnx_with_forms.plan.toml` | 3 | 3 | 3 | 3 | 1.7 | 1.7 | 0.90 | 正确性3 |
| `feat/modules/onnx.plan.toml` | 159 | 159 | 155 | 155 | 277.9 | 282.7 | 14.55 | 正确性155 |
| `feat/modules/op_record.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/modules/operator_faults.plan.toml` | 4 | 4 | 4 | 4 | 2.8 | 2.8 | 0.90 | 正确性4 |
| `feat/modules/operator_semantics.plan.toml` | 4 | 4 | 4 | 4 | 4.2 | 4.3 | 1.31 | 正确性4 |
| `feat/modules/optim.plan.toml` | 9 | 27 | 27 | 27 | 15.8 | 16.4 | 1.09 | 正确性27 |
| `feat/modules/pass_not_found.plan.toml` | 17 | 17 | 17 | 17 | 9.3 | 9.5 | 1.15 | 正确性17 |
| `feat/modules/random_reference.plan.toml` | 2 | 5 | 5 | 5 | 2.6 | 2.7 | 1.50 | 正确性5 |
| `feat/modules/random_shuffle_kernels.plan.toml` | 2 | 5 | 5 | 5 | 1.4 | 1.5 | 0.36 | 正确性5 |
| `feat/modules/random.plan.toml` | 4 | 13 | 12 | 12 | 5.5 | 5.6 | 0.71 | 正确性12 |
| `feat/modules/remat.plan.toml` | 3 | 6 | 6 | 6 | 2.2 | 2.3 | 0.61 | 正确性6 |
| `feat/modules/rename_identity.plan.toml` | 2 | 2 | 2 | 2 | 1.6 | 1.6 | 0.85 | 正确性2 |
| `feat/modules/rename.plan.toml` | 6 | 6 | 6 | 6 | 2.7 | 2.8 | 0.56 | 正确性6 |
| `feat/modules/resources.plan.toml` | 6 | 6 | 6 | 6 | 5.9 | 6.0 | 1.90 | 正确性6 |
| `feat/modules/rewrite_function.plan.toml` | 5 | 5 | 5 | 5 | 2.0 | 2.0 | 0.49 | 正确性5 |
| `feat/modules/rules_conditions.plan.toml` | 10 | 10 | 10 | 10 | 2.9 | 3.0 | 0.43 | 正确性10 |
| `feat/modules/rules_differential.plan.toml` | 1 | 1 | 1 | 1 | 0.7 | 0.7 | 0.69 | 正确性1 |
| `feat/modules/rules_libraries.plan.toml` | 12 | 16 | 16 | 16 | 14.4 | 14.7 | 2.84 | 正确性16 |
| `feat/modules/rules_proofs.plan.toml` | 1 | 1 | 1 | 1 | 0.0 | 0.0 | 0.00 | 正确性1 |
| `feat/modules/rules.plan.toml` | 158 | 174 | 174 | 174 | 79.7 | 82.5 | 4.52 | 正确性174 |
| `feat/modules/selected_protection.plan.toml` | 58 | 58 | 58 | 48 | 13.6 | 14.3 | 0.52 | 正确性58 |
| `feat/modules/std.plan.toml` | 30 | 30 | 30 | 30 | 12.8 | 13.2 | 0.89 | 正确性30 |
| `feat/modules/stdlib_faults.plan.toml` | 40 | 112 | 112 | 112 | 29.3 | 30.5 | 0.49 | 正确性112 |
| `feat/modules/stdlib_reduced.plan.toml` | 15 | 48 | 48 | 48 | 16.2 | 16.9 | 0.65 | 正确性48 |
| `feat/modules/stdlib_registry.plan.toml` | 14 | 14 | 14 | 14 | 30.6 | 31.7 | 20.32 | 正确性14 |
| `feat/modules/stdlib_values.plan.toml` | 29 | 59 | 59 | 59 | 20.4 | 21.1 | 0.83 | 正确性59 |
| `feat/modules/stream_library.plan.toml` | 1 | 4 | 4 | 4 | 1.8 | 1.8 | 0.69 | 正确性4 |
| `feat/modules/tensor_int_division.plan.toml` | 3 | 12 | 12 | 12 | 5.1 | 5.2 | 0.58 | 正确性12 |
| `feat/modules/tensor.plan.toml` | 73 | 121 | 121 | 121 | 57.5 | 59.7 | 1.21 | 正确性117 压力4 |
| `feat/modules/tensor/bounds.plan.toml` | 8 | 8 | 8 | 8 | 4.7 | 4.8 | 0.84 | 正确性8 |
| `feat/modules/tensor/comparison.plan.toml` | 2 | 3 | 3 | 3 | 1.5 | 1.5 | 0.63 | 正确性3 |
| `feat/modules/tensor/compound_assign.plan.toml` | 3 | 6 | 6 | 6 | 2.8 | 3.0 | 0.75 | 正确性6 |
| `feat/modules/tensor/dtype.plan.toml` | 32 | 58 | 58 | 58 | 18.9 | 19.7 | 0.60 | 正确性58 |
| `feat/modules/tensor/named_dims.plan.toml` | 14 | 27 | 27 | 27 | 15.2 | 15.7 | 1.14 | 正确性27 |
| `feat/modules/tensor/shape_diagnostics.plan.toml` | 17 | 17 | 17 | 17 | 6.1 | 6.2 | 0.52 | 正确性17 |
| `feat/modules/tensor/shape_inference.plan.toml` | 26 | 26 | 26 | 26 | 11.0 | 11.5 | 0.74 | 正确性26 |
| `feat/modules/tensor/shape_types.plan.toml` | 16 | 16 | 16 | 16 | 5.4 | 5.7 | 0.56 | 正确性16 |
| `feat/modules/tensor/subscript.plan.toml` | 27 | 27 | 27 | 27 | 15.9 | 16.7 | 1.19 | 正确性27 |
| `feat/modules/threads.plan.toml` | 21 | 21 | 21 | 21 | 11.7 | 12.1 | 1.11 | 正确性21 |
| `feat/modules/tiles.plan.toml` | 85 | 109 | 109 | 109 | 64.1 | 66.2 | 2.92 | 正确性71 压力38 |
| `feat/modules/types_shapes.plan.toml` | 7 | 16 | 16 | 16 | 7.0 | 7.2 | 0.70 | 正确性16 |
| `feat/modules/unicode_paths.plan.toml` | 1 | 1 | 1 | 1 | 1.6 | 2.2 | 1.59 | 正确性1 |
| `feat/modules/with_after_norm.plan.toml` | 3 | 9 | 9 | 9 | 1.4 | 1.6 | 0.28 | 正确性9 |
| `feat/opt/adb67e081.plan.toml` | 6 | 6 | 6 | 4 | 6.4 | 6.6 | 1.83 | 防回归6 |
| `feat/opt/algebra_rules.plan.toml` | 21 | 24 | 24 | 24 | 11.7 | 12.1 | 0.83 | 正确性24 |
| `feat/opt/algebra.plan.toml` | 63 | 192 | 192 | 192 | 251.5 | 259.3 | 2.72 | 正确性192 |
| `feat/opt/camel_passes.plan.toml` | 31 | 43 | 43 | 43 | 20.7 | 21.6 | 0.83 | 正确性43 |
| `feat/opt/commit_checks.plan.toml` | 6 | 6 | 6 | 0 | — | — | — | 正确性6 |
| `feat/opt/commit_refusal.plan.toml` | 14 | 14 | 14 | 0 | — | — | — | 正确性14 |
| `feat/opt/control_data.plan.toml` | 13 | 49 | 49 | 49 | 57.3 | 58.8 | 2.16 | 正确性49 |
| `feat/opt/control.plan.toml` | 4 | 4 | 4 | 4 | 5.2 | 5.3 | 2.37 | 正确性4 |
| `feat/opt/cxx_isolation.plan.toml` | 7 | 7 | 7 | 0 | — | — | — | 正确性7 |
| `feat/opt/defun.plan.toml` | 21 | 21 | 21 | 21 | 18.9 | 19.2 | 2.46 | 正确性21 |
| `feat/opt/devirtualize_callee.plan.toml` | 5 | 5 | 5 | 0 | — | — | — | 正确性5 |
| `feat/opt/e9.plan.toml` | 67 | 268 | 268 | 0 | — | — | — | 正确性268 |
| `feat/opt/edges.plan.toml` | 19 | 19 | 19 | 16 | 11.0 | 11.6 | 1.25 | 正确性19 |
| `feat/opt/effects_kept.plan.toml` | 51 | 189 | 189 | 79 | 25.6 | 26.3 | 2.79 | 正确性189 |
| `feat/opt/equal_branch.plan.toml` | 12 | 36 | 36 | 36 | 39.6 | 40.6 | 1.89 | 正确性36 |
| `feat/opt/fault_preservation.plan.toml` | 14 | 56 | 56 | 56 | 31.5 | 32.4 | 0.93 | 正确性56 |
| `feat/opt/fold_warning.plan.toml` | 11 | 11 | 11 | 11 | 7.0 | 7.4 | 1.13 | 正确性11 |
| `feat/opt/gate_value.plan.toml` | 18 | 60 | 60 | 60 | 67.2 | 69.2 | 2.21 | 正确性60 |
| `feat/opt/generic.plan.toml` | 118 | 118 | 118 | 121 | 132.2 | 137.0 | 2.31 | 正确性118 |
| `feat/opt/graph_faces.plan.toml` | 5 | 5 | 5 | 2 | 4.1 | 4.2 | 2.10 | 正确性5 |
| `feat/opt/inline_cycle.plan.toml` | 5 | 5 | 5 | 3 | 3.6 | 3.8 | 2.50 | 正确性5 |
| `feat/opt/inline_use.plan.toml` | 5 | 5 | 5 | 5 | 33.0 | 33.0 | 6.67 | 正确性5 |
| `feat/opt/inline.plan.toml` | 63 | 69 | 69 | 62 | 61.4 | 63.6 | 2.01 | 正确性69 |
| `feat/opt/never_exit.plan.toml` | 12 | 48 | 48 | 48 | 42.6 | 43.5 | 1.79 | 正确性48 |
| `feat/opt/operator_records.plan.toml` | 4 | 4 | 4 | 0 | — | — | — | 正确性4 |
| `feat/opt/opt_second_pass.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/opt/pass_function.plan.toml` | 8 | 8 | 8 | 0 | — | — | — | 正确性8 |
| `feat/opt/pass_params.plan.toml` | 60 | 60 | 60 | 60 | 28.5 | 30.0 | 2.12 | 正确性60 |
| `feat/opt/progen_simplify.plan.toml` | 45 | 45 | 45 | 45 | 45.8 | 47.1 | 2.09 | 正确性45 |
| `feat/opt/progen.plan.toml` | 3 | 3 | 1 | 0 | — | — | — | 正确性1 |
| `feat/opt/progen8_arm.plan.toml` | 3 | 3 | 3 | 3 | 3.1 | 3.3 | 1.23 | 正确性3 |
| `feat/opt/regions.plan.toml` | 14 | 14 | 14 | 9 | 1.6 | 1.7 | 0.32 | 正确性14 |
| `feat/opt/rewrite_application.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/opt/rounds_warning.plan.toml` | 6 | 6 | 6 | 0 | — | — | — | 正确性6 |
| `feat/opt/savepoint.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/opt/screens.plan.toml` | 127 | 127 | 127 | 127 | 53.2 | 55.0 | 1.92 | 正确性127 |
| `feat/opt/splice_place.plan.toml` | 12 | 45 | 45 | 45 | 21.3 | 21.8 | 0.84 | 正确性41 性能4 |
| `feat/opt/sync_chain.plan.toml` | 4 | 12 | 12 | 12 | 7.7 | 8.2 | 1.13 | 正确性12 |
| `feat/opt/unread_gate.plan.toml` | 9 | 21 | 21 | 21 | 18.0 | 18.8 | 1.47 | 正确性21 |
| `feat/opt/unused_value_chain.plan.toml` | 11 | 23 | 23 | 23 | 5.5 | 5.8 | 0.46 | 正确性23 |
| `feat/opt/writings.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性1 计时1 |
| `feat/para/core.plan.toml` | 41 | 48 | 48 | 38 | 27.4 | 31.0 | 1.61 | 正确性38 压力10 |
| `feat/para/exec_pred.plan.toml` | 10 | 31 | 31 | 0 | — | — | — | 正确性31 |
| `feat/para/handoff.plan.toml` | 13 | 25 | 25 | 25 | 10.5 | 10.9 | 0.74 | 正确性23 压力2 |
| `feat/parse/constructs.plan.toml` | 18 | 18 | 18 | 13 | 6.0 | 6.2 | 0.87 | 正确性18 |
| `feat/parse/core.plan.toml` | 32 | 32 | 32 | 31 | 13.1 | 13.5 | 0.74 | 正确性32 |
| `feat/parse/frontend.plan.toml` | 9 | 9 | 8 | 7 | 13.8 | 13.9 | 8.93 | 正确性8 |
| `feat/parse/numbers.plan.toml` | 8 | 8 | 8 | 8 | 2.6 | 2.7 | 0.45 | 正确性8 |
| `feat/parse/rules.plan.toml` | 9 | 10 | 10 | 10 | 3.6 | 3.7 | 0.75 | 正确性10 |
| `feat/parse/rulings.plan.toml` | 29 | 50 | 50 | 45 | 18.0 | 18.4 | 0.88 | 正确性50 |
| `feat/parse/tuples.plan.toml` | 1 | 1 | 1 | 1 | 0.4 | 0.4 | 0.42 | 正确性1 |
| `feat/runtime/adopt_chain.plan.toml` | 3 | 3 | 3 | 3 | 1.4 | 1.4 | 0.46 | 正确性3 |
| `feat/runtime/draft_erase_unread.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/runtime/draft_node_owner.plan.toml` | 1 | 1 | 1 | 1 | 0.6 | 0.6 | 0.57 | 正确性1 |
| `feat/runtime/edit_record.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/runtime/exit_type.plan.toml` | 3 | 3 | 3 | 3 | 1.0 | 1.2 | 0.40 | 正确性3 |
| `feat/runtime/graph_draft_ledger.plan.toml` | 2 | 2 | 2 | 2 | 3.0 | 3.1 | 1.91 | 正确性2 |
| `feat/runtime/oper_shape.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/showcase_d4.plan.toml` | 8 | 20 | 20 | 20 | 13.1 | 13.6 | 1.25 | 正确性20 |
| `feat/showcase_i2.plan.toml` | 5 | 5 | 4 | 4 | 2.3 | 2.4 | 0.73 | 正确性4 |
| `feat/showcase_s1.plan.toml` | 1 | 2 | 2 | 2 | 0.7 | 0.8 | 0.45 | 正确性2 |
| `feat/showcase_s2.plan.toml` | 10 | 10 | 10 | 10 | 3.9 | 4.0 | 0.48 | 正确性10 |
| `feat/tools/autodiff_migration_ledger.plan.toml` | 1 | 1 | 1 | 1 | 11.5 | 11.5 | 11.47 | 正确性1 |
| `feat/tools/build_stamp.plan.toml` | 1 | 1 | 1 | 1 | 3.5 | 3.5 | 3.51 | 正确性1 |
| `feat/tools/camel_remote_entry.plan.toml` | 1 | 1 | 1 | 1 | 0.5 | 0.6 | 0.50 | 正确性1 |
| `feat/tools/clean.plan.toml` | 1 | 1 | 1 | 1 | 2.3 | 2.3 | 2.26 | 正确性1 |
| `feat/tools/cli.plan.toml` | 6 | 6 | 6 | 6 | 24.9 | 25.2 | 11.86 | 正确性6 |
| `feat/tools/compile_bench.plan.toml` | 1 | 1 | 1 | 1 | 1.4 | 1.4 | 1.43 | 正确性1 |
| `feat/tools/debugger.plan.toml` | 14 | 14 | 10 | 10 | 80.3 | 80.7 | 60.79 | 正确性10 |
| `feat/tools/diagnostic_form_readers.plan.toml` | 1 | 1 | 1 | 1 | 1.4 | 1.4 | 1.39 | 正确性1 |
| `feat/tools/exit_reporting.plan.toml` | 1 | 1 | 1 | 1 | 7.1 | 7.2 | 7.07 | 正确性1 |
| `feat/tools/facts.plan.toml` | 6 | 6 | 6 | 0 | — | — | — | 正确性6 |
| `feat/tools/find_fix.plan.toml` | 1 | 1 | 1 | 1 | 18.2 | 18.3 | 18.19 | 正确性1 |
| `feat/tools/grammar.plan.toml` | 13 | 13 | 13 | 13 | 1.0 | 1.1 | 0.56 | 正确性13 |
| `feat/tools/inspect_copies.plan.toml` | 4 | 4 | 4 | 0 | — | — | — | 正确性4 |
| `feat/tools/install_runtime.plan.toml` | 1 | 1 | 1 | 1 | 0.4 | 0.4 | 0.38 | 正确性1 |
| `feat/tools/layer_accounting.plan.toml` | 1 | 1 | 1 | 1 | 14.0 | 14.1 | 13.97 | 正确性1 |
| `feat/tools/load_class.plan.toml` | 3 | 3 | 3 | 3 | 17.3 | 17.6 | 12.82 | 正确性3 |
| `feat/tools/lsp.plan.toml` | 11 | 11 | 10 | 10 | 8.4 | 9.0 | 1.25 | 正确性10 |
| `feat/tools/member_init.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/tools/plan_fields.plan.toml` | 2 | 2 | 2 | 0 | — | — | — | 正确性2 |
| `feat/tools/plan_normalizers.plan.toml` | 2 | 2 | 2 | 2 | 2.5 | 2.6 | 1.28 | 正确性2 |
| `feat/tools/plan_refs_sweep.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/tools/plan_refs.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 正确性3 |
| `feat/tools/portability.plan.toml` | 2 | 2 | 2 | 2 | 4.7 | 4.7 | 3.48 | 正确性2 |
| `feat/tools/provenance.plan.toml` | 1 | 1 | 1 | 1 | 6.1 | 6.2 | 6.11 | 正确性1 |
| `feat/tools/retention.plan.toml` | 2 | 2 | 2 | 2 | 2.4 | 2.5 | 1.86 | 正确性2 |
| `feat/tools/tree_guard.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/trans/annotated_dump.plan.toml` | 3 | 3 | 3 | 3 | 2.4 | 2.4 | 0.84 | 正确性2 未归类1 |
| `feat/trans/canon_order.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 正确性3 |
| `feat/trans/canonical_gir.plan.toml` | 1 | 1 | 1 | 1 | 3.9 | 3.9 | 3.87 | 正确性1 |
| `feat/trans/canonical_opt_export.plan.toml` | 1 | 1 | 1 | 1 | 1.3 | 1.3 | 1.28 | 正确性1 |
| `feat/trans/cml_sweep.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `feat/trans/cml.plan.toml` | 3 | 3 | 3 | 3 | 10.7 | 10.8 | 8.08 | 正确性3 |
| `feat/trans/cpp.plan.toml` | 56 | 56 | 56 | 55 | 258.4 | 260.3 | 81.34 | 正确性56 |
| `feat/trans/effect_trace.plan.toml` | 4 | 4 | 4 | 4 | 2.6 | 2.6 | 0.85 | 正确性4 |
| `feat/trans/jgir.plan.toml` | 1 | 1 | 1 | 1 | 1.7 | 1.7 | 1.66 | 正确性1 |
| `feat/trans/macro.plan.toml` | 30 | 30 | 30 | 27 | 10.8 | 13.5 | 0.88 | 正确性29 未归类1 |
| `feat/trans/pass_dump.plan.toml` | 1 | 1 | 1 | 1 | 0.4 | 0.5 | 0.45 | 正确性1 |
| `feat/trans/port_fill_metadata.plan.toml` | 1 | 1 | 1 | 1 | 1.4 | 1.4 | 1.36 | 正确性1 |
| `feat/trans/print_determinism.plan.toml` | 1 | 1 | 1 | 1 | 1.7 | 1.7 | 1.69 | 正确性1 |
| `feat/trans/profile_alloc.plan.toml` | 3 | 3 | 3 | 3 | 3.4 | 3.5 | 1.22 | 正确性3 |
| `feat/trans/same_graph.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/trans/source_spelling.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `feat/trans/spelling.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 正确性3 |
| `feat/transforms/compositions.plan.toml` | 10 | 26 | 26 | 14 | 10.8 | 11.1 | 1.38 | 正确性26 |
| `feat/transforms/floatgate.plan.toml` | 5 | 5 | 5 | 5 | 3.8 | 4.1 | 0.89 | 正确性5 |
| `feat/transforms/nested.plan.toml` | 10 | 10 | 10 | 0 | — | — | — | 正确性10 |
| `feat/transforms/split.plan.toml` | 23 | 30 | 30 | 29 | 14.1 | 15.1 | 0.90 | 正确性30 |
| `feat/tutorial_examples.plan.toml` | 68 | 244 | 244 | 244 | 76.4 | 80.1 | 1.19 | 正确性244 |
| `gpu/profile.plan.toml` | 6 | 6 | 6 | 0 | — | — | — | 正确性6 |
| `gpu/reading.plan.toml` | 1 | 1 | 1 | 0 | — | — | — | 正确性1 |
| `gpu/tiles_regions.plan.toml` | 9 | 30 | 0 | 0 | — | — | — |  |
| `gpu/tiles_rows.plan.toml` | 2 | 2 | 0 | 0 | — | — | — |  |
| `gpu/tiles_transfer.plan.toml` | 3 | 3 | 0 | 0 | — | — | — |  |
| `gpu/tiles.plan.toml` | 104 | 104 | 1 | 0 | — | — | — | 正确性1 |
| `perf/collections_fuzz.plan.toml` | 4 | 4 | 0 | 0 | — | — | — |  |
| `perf/fib.plan.toml` | 6 | 6 | 0 | 0 | — | — | — |  |
| `perf/gc_tools.plan.toml` | 2 | 2 | 0 | 0 | — | — | — |  |
| `perf/gc.plan.toml` | 6 | 6 | 0 | 0 | — | — | — |  |
| `perf/gemm_canonical_shapes.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/gemm_threads.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/instruction_counts_load.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/instruction_counts.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/para_speedup.plan.toml` | 2 | 2 | 0 | 0 | — | — | — |  |
| `perf/para.plan.toml` | 6 | 6 | 0 | 0 | — | — | — |  |
| `perf/reduce_layout_counts.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/reduce_layout.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/showcase_c1.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/showcase_c2.plan.toml` | 4 | 4 | 0 | 0 | — | — | — |  |
| `perf/showcase_c3.plan.toml` | 19 | 19 | 0 | 0 | — | — | — |  |
| `perf/showcase_c6.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/showcase_d4.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/showcase_d5.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/startup.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/step_budget.plan.toml` | 1 | 1 | 0 | 0 | — | — | — |  |
| `perf/step_counts.plan.toml` | 2 | 2 | 0 | 0 | — | — | — |  |
| `perf/training_steps.plan.toml` | 6 | 6 | 0 | 0 | — | — | — |  |
| `probe/dpack.plan.toml` | 3 | 3 | 3 | 0 | — | — | — | 防回归3 |
| `probe/dpack5.plan.toml` | 5 | 5 | 5 | 0 | — | — | — | 防回归5 |
| `probe/dpack5ok.plan.toml` | 5 | 5 | 5 | 0 | — | — | — | 防回归5 |
| `probe/refleak.plan.toml` | 8 | 32 | 0 | 0 | — | — | — |  |
| `smoke/core.plan.toml` | 10 | 10 | 10 | 10 | 4.2 | 11.4 | 0.54 | 正确性10 |


## 附录 B：超 2 s 的 261 条，逐条分诊

按子进程墙钟降序。`去向` 是 §⑨ 的三档；`依据` 是这一档对这一条的具体判据。
这一列只用于分诊，不改任何 `tier`。

| # | 用例 | 计划 | 墙钟(s) | 去向 | 依据 |
|---:|---|---|---:|---|---|
| 1 | `agree_with_fvm` | `feat/trans/cpp.plan.toml` | 81.34 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 2 | `every_expect_program_links_no_runtime` | `feat/trans/cpp.plan.toml` | 78.64 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 3 | `debugger.dap_fusion_group` | `feat/tools/debugger.plan.toml` | 60.79 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 4 | `runner.backend_matrix` | `feat/consistency/runner.plan.toml` | 33.27 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 5 | `every_fault_program_says_what_fvm_says` | `feat/trans/cpp.plan.toml` | 32.53 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 6 | `root_window.residual_mlp.moving_stress` | `feat/mm/gc_root_window.plan.toml` | 25.36 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 7 | `type_text.roundtrip` | `feat/compile/type_text.plan.toml` | 24.11 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 8 | `registry.docs_complete` | `feat/modules/stdlib_registry.plan.toml` | 20.32 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 9 | `docs.snippets_compile` | `feat/docs_links.plan.toml` | 19.73 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 10 | `tools.find_fix_asks_who_already_fixed_it` | `feat/tools/find_fix.plan.toml` | 18.19 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 11 | `narrow_reductions_needs_no_runtime` | `feat/trans/cpp.plan.toml` | 15.60 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 12 | `catalog.doc_is_generated` | `feat/compile/diag_catalog.plan.toml` | 14.95 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 13 | `ort_entry_export` | `feat/modules/onnx.plan.toml` | 14.55 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 14 | `ort_time_axis` | `feat/modules/onnx.plan.toml` | 14.36 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 15 | `runner.tier_declared_against_started` | `feat/tools/layer_accounting.plan.toml` | 13.97 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 16 | `load_class.shared_place` | `feat/tools/load_class.plan.toml` | 12.82 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 17 | `ort_core_slice` | `feat/modules/onnx.plan.toml` | 12.66 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 18 | `cli.plan_arg_domain_refused` | `feat/tools/cli.plan.toml` | 11.86 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 19 | `import.audit` | `feat/modules/import_cost.plan.toml` | 11.79 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 20 | `type_identity.corpus` | `feat/compile/type_identity.plan.toml` | 11.68 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 21 | `tools.autodiff_migration_ledger.ratchet` | `feat/tools/autodiff_migration_ledger.plan.toml` | 11.47 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 22 | `cli.plan_stdout_matches` | `feat/tools/cli.plan.toml` | 10.81 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 23 | `ort_loop_values` | `feat/modules/onnx.plan.toml` | 10.06 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 24 | `ort_struct_loop` | `feat/modules/onnx.plan.toml` | 9.69 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 25 | `capacity.growth_simplify_gc_stress` | `feat/mm/capacity.plan.toml` | 9.67 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 26 | `frontend.format_keeps_ast` | `feat/parse/frontend.plan.toml` | 8.93 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 27 | `narrow_reductions_four_backends` | `feat/trans/cpp.plan.toml` | 8.83 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 28 | `ort_grad_loop_values` | `feat/modules/onnx.plan.toml` | 8.80 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 29 | `capacity.wide_graph` | `feat/mm/capacity.plan.toml` | 8.51 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 30 | `print_and_recompile` | `feat/trans/cml.plan.toml` | 8.08 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 31 | `gate.conformance_matrix` | `feat/gate/conformance.plan.toml` | 7.83 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 32 | `native_state_checkpoints` | `feat/modules/checkpoints.plan.toml` | 7.55 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 33 | `python_binding.session` | `feat/bindings/python_binding.plan.toml` | 7.35 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 34 | `runner.exit_reporting_names_the_signal` | `feat/tools/exit_reporting.plan.toml` | 7.07 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 35 | `surface.minus.nvm` | `feat/consistency/surface.plan.toml` | 6.84 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 36 | `surface.member_chain.nvm` | `feat/consistency/surface.plan.toml` | 6.78 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 37 | `surface.lambda_call.nvm` | `feat/consistency/surface.plan.toml` | 6.72 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 38 | `surface.zip_many.nvm` | `feat/consistency/surface.plan.toml` | 6.69 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 39 | `inline_keeps_stdout.write.nvm` | `feat/opt/inline_use.plan.toml` | 6.67 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 40 | `gate.plan_inventory` | `feat/gate/plan_inventory.plan.toml` | 6.67 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 41 | `export_loop_cond` | `feat/modules/onnx.plan.toml` | 6.65 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 42 | `inline_keeps_stdout.inline.nvm` | `feat/opt/inline_use.plan.toml` | 6.63 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 43 | `surface.operator_values.nvm` | `feat/consistency/surface.plan.toml` | 6.62 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 44 | `inline_keeps_stdout.drop.nvm` | `feat/opt/inline_use.plan.toml` | 6.57 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 45 | `inline_keeps_stdout.twice_write.nvm` | `feat/opt/inline_use.plan.toml` | 6.56 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 46 | `ort_grad_loop` | `feat/modules/onnx.plan.toml` | 6.55 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 47 | `inline_keeps_stdout.twice_inline.nvm` | `feat/opt/inline_use.plan.toml` | 6.54 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 48 | `gate.case_ledger` | `feat/gate/case_ledger.plan.toml` | 6.52 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 49 | `attempt.resources.isolated.nvm` | `feat/consistency/attempt.plan.toml` | 6.44 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 50 | `attempt.resources.normal.nvm` | `feat/consistency/attempt.plan.toml` | 6.38 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 51 | `attempt.resources.fault.nvm` | `feat/consistency/attempt.plan.toml` | 6.32 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 52 | `attempt.resources.exit.nvm` | `feat/consistency/attempt.plan.toml` | 6.27 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 53 | `attempt.pipeline.places_opt.nvm` | `feat/consistency/attempt.plan.toml` | 6.17 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 54 | `attempt.translate.cpp` | `feat/consistency/attempt.plan.toml` | 6.14 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 55 | `attempt.pipeline.inline_opt.prl` | `feat/consistency/attempt.plan.toml` | 6.14 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 56 | `attempt.pipeline.macro_opt.prl` | `feat/consistency/attempt.plan.toml` | 6.12 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 57 | `runner.provenance_line_names_the_artifact` | `feat/tools/provenance.plan.toml` | 6.11 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 58 | `attempt.pipeline.nested_opt.nvm` | `feat/consistency/attempt.plan.toml` | 6.11 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 59 | `attempt.pipeline.cse_dce.prl` | `feat/consistency/attempt.plan.toml` | 6.11 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 60 | `attempt.not_isolated.fault_in_argument.prl` | `feat/consistency/attempt.plan.toml` | 6.08 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 61 | `ort_branch` | `feat/modules/onnx.plan.toml` | 6.08 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 62 | `attempt.pipeline.fold.prl` | `feat/consistency/attempt.plan.toml` | 6.08 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 63 | `attempt.not_isolated.fault_in_argument.jit` | `feat/consistency/attempt.plan.toml` | 6.03 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 64 | `attempt.not_isolated.fault_in_argument.fvm` | `feat/consistency/attempt.plan.toml` | 6.00 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 65 | `attempt.refuse.onnx` | `feat/consistency/attempt.plan.toml` | 5.96 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 66 | `every_expect_program_five_times` | `feat/trans/cpp.plan.toml` | 5.59 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 67 | `export_loop_for` | `feat/modules/onnx.plan.toml` | 5.57 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 68 | `ort_slice_bounds` | `feat/modules/onnx.plan.toml` | 5.39 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 69 | `capacity.fanout` | `feat/mm/capacity.plan.toml` | 5.27 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 70 | `ort_loop_loaded_weights` | `feat/modules/onnx.plan.toml` | 5.06 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 71 | `value.deep_enum_chain.prl` | `feat/consistency/values.plan.toml` | 4.96 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 72 | `ort_dynamic_params` | `feat/modules/onnx.plan.toml` | 4.92 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 73 | `unused_values.one_input_one_answer` | `feat/compile/unused_values.plan.toml` | 4.84 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 74 | `ort_dynamic_batch` | `feat/modules/onnx.plan.toml` | 4.79 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 75 | `ort_train_step_dynamic` | `feat/modules/onnx.plan.toml` | 4.76 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 76 | `ort_const_under_as` | `feat/modules/onnx.plan.toml` | 4.65 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 77 | `rules.the_coverage_check_refuses_every_refuted_law` | `feat/modules/rules.plan.toml` | 4.52 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 78 | `attempt.pipeline.simplify.prl` | `feat/consistency/attempt.plan.toml` | 4.50 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 79 | `ort_linear_recursion` | `feat/modules/onnx.plan.toml` | 4.46 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 80 | `ort_slice_start` | `feat/modules/onnx.plan.toml` | 4.43 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 81 | `ort_duplicate_outputs` | `feat/modules/onnx.plan.toml` | 4.39 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 82 | `catalog.failure_paths` | `feat/compile/diag_catalog.plan.toml` | 4.38 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 83 | `ort_struct_casts` | `feat/modules/onnx.plan.toml` | 4.29 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 84 | `attempt.many.fvm` | `feat/consistency/attempt.plan.toml` | 4.24 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 85 | `attempt.refuse.grad` | `feat/consistency/attempt.plan.toml` | 3.90 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 86 | `canonical_document_and_equivalence` | `feat/trans/canonical_gir.plan.toml` | 3.87 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 87 | `attempt.many.jit` | `feat/consistency/attempt.plan.toml` | 3.81 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 88 | `process_state.stw_give_up_escalates` | `feat/mm/process_state.plan.toml` | 3.81 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 89 | `nested_return.compile_time` | `feat/compile/return_type.plan.toml` | 3.68 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 90 | `build_stamp.options_change` | `feat/tools/build_stamp.plan.toml` | 3.51 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 91 | `portability.std_includes` | `feat/tools/portability.plan.toml` | 3.48 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 92 | `attempt.collector.stress.nvm` | `feat/consistency/attempt.plan.toml` | 3.47 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 93 | `attempt.siblings.nvm` | `feat/consistency/attempt.plan.toml` | 3.47 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 94 | `attempt.collector.stress.prl` | `feat/consistency/attempt.plan.toml` | 3.45 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 95 | `attempt.collector.stress_both.nvm` | `feat/consistency/attempt.plan.toml` | 3.41 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 96 | `attempt.many.nvm` | `feat/consistency/attempt.plan.toml` | 3.37 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 97 | `attempt.collector.stress.jit` | `feat/consistency/attempt.plan.toml` | 3.36 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 98 | `attempt.collector.stress_both.fvm` | `feat/consistency/attempt.plan.toml` | 3.36 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 99 | `capacity.dead_function_closure_verify` | `feat/mm/capacity.plan.toml` | 3.34 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 100 | `attempt.collector.stress_both.jit` | `feat/consistency/attempt.plan.toml` | 3.29 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 101 | `attempt.collector.stress.fvm` | `feat/consistency/attempt.plan.toml` | 3.23 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 102 | `collfuzz.quick` | `feat/compile/collections.plan.toml` | 3.20 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 103 | `unused_values.help_let_sync_is_not_a_program` | `feat/compile/unused_values.plan.toml` | 3.19 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 104 | `frontend.ast_snapshot` | `feat/parse/frontend.plan.toml` | 3.16 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 105 | `unused_values.called_fallible` | `feat/compile/unused_values.plan.toml` | 3.10 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 106 | `registry.migrations_stay_migrated` | `feat/modules/stdlib_registry.plan.toml` | 3.02 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 107 | `process_state.stw_sleep_leaves_its_region` | `feat/mm/process_state.plan.toml` | 3.00 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 108 | `docs.links.citations` | `feat/docs_links.plan.toml` | 2.98 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 109 | `debugger.dap_tail_steps` | `feat/tools/debugger.plan.toml` | 2.96 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 110 | `grad_structures` | `feat/modules/autodiff.plan.toml` | 2.95 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 111 | `ort_grad_matmul_batched` | `feat/modules/onnx.plan.toml` | 2.93 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 112 | `unused_values.called_fallible_pipeline` | `feat/compile/unused_values.plan.toml` | 2.93 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 113 | `gpu_scope.placement_with_a_device` | `feat/modules/tiles.plan.toml` | 2.92 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 114 | `attempt.collector.stress_both.prl` | `feat/consistency/attempt.plan.toml` | 2.85 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 115 | `rules.accuracy_scalar_every_backend.jit` | `feat/modules/rules_libraries.plan.toml` | 2.84 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 116 | `attempt.concurrent.jit` | `feat/consistency/attempt.plan.toml` | 2.84 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 117 | `attempt.concurrent.fvm` | `feat/consistency/attempt.plan.toml` | 2.83 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 118 | `gpu_scope.host_effect_synchronises` | `feat/modules/tiles.plan.toml` | 2.82 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 119 | `attempt.compiled.jit` | `feat/consistency/attempt.plan.toml` | 2.80 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 120 | `unused_values.help_if_arm_then_sync_runs` | `feat/compile/unused_values.plan.toml` | 2.79 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 121 | `pass_written.inventory.nvm` | `feat/opt/effects_kept.plan.toml` | 2.79 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 122 | `attempt.compiled.fvm` | `feat/consistency/attempt.plan.toml` | 2.78 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 123 | `rules.accuracy_scalar_every_backend.fvm` | `feat/modules/rules_libraries.plan.toml` | 2.77 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 124 | `capacity.scale_check` | `feat/mm/capacity.plan.toml` | 2.76 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 125 | `match.complete.prl` | `feat/consistency/match.plan.toml` | 2.72 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 126 | `value.deep_enum_chain.jit` | `feat/consistency/values.plan.toml` | 2.72 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 127 | `gradient.false` | `feat/opt/algebra.plan.toml` | 2.72 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 128 | `docs.no_unfinished_merge_left_in_the_tree` | `feat/docs_links.plan.toml` | 2.71 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 129 | `value.deep_enum_chain.fvm` | `feat/consistency/values.plan.toml` | 2.70 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 130 | `fixes_apply` | `feat/compile/diag_fixes.plan.toml` | 2.68 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 131 | `match.complete.jit` | `feat/consistency/match.plan.toml` | 2.66 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 132 | `docs.no_unfinished_merge_pattern_sees_all_four_forms` | `feat/docs_links.plan.toml` | 2.65 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 133 | `boundary64.inf.safe.prl` | `feat/opt/algebra.plan.toml` | 2.64 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 134 | `execution.opt_safe.prl` | `feat/opt/algebra.plan.toml` | 2.63 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 135 | `rules.fusion_bit_identical_fused.jit` | `feat/modules/rules_libraries.plan.toml` | 2.62 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 136 | `opt_matches_interpreter.mnist.nvm` | `feat/opt/effects_kept.plan.toml` | 2.61 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 137 | `narrow_one_program_twenty_times` | `feat/trans/cpp.plan.toml` | 2.61 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。每条要 C++ 编译 + 链接。 |
| 138 | `debugger.http_gateway` | `feat/tools/debugger.plan.toml` | 2.61 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 139 | `boundary64.-0.algebra.fvm` | `feat/opt/algebra.plan.toml` | 2.59 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 140 | `match.complete.fvm` | `feat/consistency/match.plan.toml` | 2.58 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 141 | `boundary64.inf.safe.jit` | `feat/opt/algebra.plan.toml` | 2.54 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 142 | `attempt.library.jit` | `feat/consistency/attempt.plan.toml` | 2.53 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 143 | `execution.opt_safe.jit` | `feat/opt/algebra.plan.toml` | 2.52 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 144 | `attempt.siblings.jit` | `feat/consistency/attempt.plan.toml` | 2.51 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 145 | `inline_cycle.splice_place` | `feat/opt/inline_cycle.plan.toml` | 2.50 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 146 | `root_window.gru_lite.moving_stress` | `feat/mm/gc_root_window.plan.toml` | 2.50 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 147 | `gradient.true` | `feat/opt/algebra.plan.toml` | 2.48 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 148 | `attempt.library.fvm` | `feat/consistency/attempt.plan.toml` | 2.47 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 149 | `boundary64.2.5e-323.safe.fvm` | `feat/opt/algebra.plan.toml` | 2.47 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 150 | `attempt.pipeline.simplify.jit` | `feat/consistency/attempt.plan.toml` | 2.46 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 6 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 151 | `execution.algebra.jit` | `feat/opt/algebra.plan.toml` | 2.46 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 152 | `attempt.siblings.fvm` | `feat/consistency/attempt.plan.toml` | 2.46 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 153 | `defun.four_backends` | `feat/opt/defun.plan.toml` | 2.46 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 154 | `rules.fusion_bit_identical_fused.fvm` | `feat/modules/rules_libraries.plan.toml` | 2.44 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 155 | `unused_values.help_if_arm_else_sync_runs` | `feat/compile/unused_values.plan.toml` | 2.43 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 156 | `boundary64.5e-324.reference.jit` | `feat/opt/algebra.plan.toml` | 2.43 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 157 | `boundary64.inf.safe.fvm` | `feat/opt/algebra.plan.toml` | 2.43 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 158 | `boundary64.inf.algebra.fvm` | `feat/opt/algebra.plan.toml` | 2.42 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 159 | `attempt.effects.fvm` | `feat/consistency/attempt.plan.toml` | 2.42 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 160 | `debugger.cli_session` | `feat/tools/debugger.plan.toml` | 2.42 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 161 | `attempt.depth.jit` | `feat/consistency/attempt.plan.toml` | 2.41 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 162 | `execution.opt_safe.fvm` | `feat/opt/algebra.plan.toml` | 2.40 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 163 | `boundary64.1e308.reference.jit` | `feat/opt/algebra.plan.toml` | 2.40 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 164 | `boundary64.5e-324.reference.prl` | `feat/opt/algebra.plan.toml` | 2.39 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 165 | `control.pipeline_contract` | `feat/opt/control.plan.toml` | 2.37 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 166 | `library_gemm.tolerance` | `feat/compile/numeric_permissions.plan.toml` | 2.37 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 167 | `attempt.not_isolated.exit.nvm` | `feat/consistency/attempt.plan.toml` | 2.36 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 168 | `ort_var_parameter` | `feat/modules/onnx.plan.toml` | 2.34 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 169 | `gpu_preference.named_device` | `feat/modules/tiles.plan.toml` | 2.34 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 170 | `warn.werror_pass_warning` | `feat/compile/diag_options.plan.toml` | 2.33 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 171 | `ort_argmax` | `feat/modules/onnx.plan.toml` | 2.33 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 172 | `boundary64.nan.algebra.prl` | `feat/opt/algebra.plan.toml` | 2.33 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 173 | `boundary64.1e308.safe.prl` | `feat/opt/algebra.plan.toml` | 2.32 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 174 | `optimization_namespace` | `feat/opt/generic.plan.toml` | 2.31 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 175 | `load_class.tree` | `feat/tools/load_class.plan.toml` | 2.30 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 176 | `library_gemm.tolerance.safe` | `feat/compile/numeric_permissions.plan.toml` | 2.30 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 177 | `execution.safe.fvm` | `feat/opt/algebra.plan.toml` | 2.29 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 178 | `boundary32.3e38.true.fvm` | `feat/opt/algebra.plan.toml` | 2.29 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 179 | `graph_space.function_value_keeps_program` | `feat/mm/graph_space.plan.toml` | 2.29 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 180 | `boundary64.5e-324.reference.fvm` | `feat/opt/algebra.plan.toml` | 2.28 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 181 | `boundary64.1e308.reference.fvm` | `feat/opt/algebra.plan.toml` | 2.27 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 182 | `attempt.not_isolated.fault_outside.nvm` | `feat/consistency/attempt.plan.toml` | 2.27 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 183 | `cse_arm_constants.inline_cse.fvm` | `feat/opt/generic.plan.toml` | 2.27 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 184 | `ort_slice_grad_stride` | `feat/modules/onnx.plan.toml` | 2.27 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。整条 onnxruntime 会话。 |
| 185 | `catalog.coverage_report` | `feat/compile/diag_catalog.plan.toml` | 2.27 | **B** | node 驱动：它走整棵语料/整份文档，成本随树的大小走，不随这条用例走。 |
| 186 | `clean.tiers` | `feat/tools/clean.plan.toml` | 2.26 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 187 | `attempt.not_isolated.fault_in_argument.nvm` | `feat/consistency/attempt.plan.toml` | 2.25 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 188 | `execution.algebra.fvm` | `feat/opt/algebra.plan.toml` | 2.25 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 189 | `unused_values.help_function_body_sync_runs` | `feat/compile/unused_values.plan.toml` | 2.25 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 190 | `debugger.loop_frames` | `feat/tools/debugger.plan.toml` | 2.25 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 191 | `boundary64.1e308.safe.jit` | `feat/opt/algebra.plan.toml` | 2.23 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 192 | `debugger.dap_frames` | `feat/tools/debugger.plan.toml` | 2.23 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 193 | `boundary32.7.006492321624085e-45.false.nvm` | `feat/opt/algebra.plan.toml` | 2.22 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 194 | `print_then_run_agrees_with_the_origin` | `feat/trans/cml.plan.toml` | 2.22 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 195 | `value.deep_enum_chain.nvm` | `feat/consistency/values.plan.toml` | 2.22 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 196 | `execution.opt_safe.nvm` | `feat/opt/algebra.plan.toml` | 2.21 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 197 | `covered_value.graph.true` | `feat/opt/gate_value.plan.toml` | 2.21 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 198 | `boundary32.1.401298464324817e-45.true.prl` | `feat/opt/algebra.plan.toml` | 2.21 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 199 | `boundary32.3e38.false.fvm` | `feat/opt/algebra.plan.toml` | 2.20 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 200 | `attempt.bad_calls` | `feat/consistency/attempt.plan.toml` | 2.20 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 201 | `specialize_element_type_nest.refused` | `feat/opt/generic.plan.toml` | 2.20 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 202 | `debugger.dap_pipeline` | `feat/tools/debugger.plan.toml` | 2.19 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 203 | `residual_mlp_inline_large` | `feat/modules/nn.plan.toml` | 2.19 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 204 | `tensor_grad_check.simplify.jit` | `feat/modules/autodiff.plan.toml` | 2.19 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 205 | `load_class.explain_under_load` | `feat/tools/load_class.plan.toml` | 2.19 | **B** | node 驱动：它自己再起一次跑测（嵌套跑），成本随树的大小走，不随这条用例走。 |
| 206 | `attempt.bad_call.argument` | `feat/consistency/attempt.plan.toml` | 2.18 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 207 | `cse_arm_constants.inline_cse.jit` | `feat/opt/generic.plan.toml` | 2.18 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 208 | `control_data.unused_argument.reference.prl` | `feat/opt/control_data.plan.toml` | 2.16 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 209 | `static_branches.simplify` | `feat/opt/generic.plan.toml` | 2.15 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 210 | `integrated.graph.true` | `feat/opt/algebra.plan.toml` | 2.15 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 211 | `reshape_view_gc.fold_specialize.nvm` | `feat/opt/generic.plan.toml` | 2.14 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 212 | `var_tensor_read.opt_macro_nvm` | `feat/linear/var_reference.plan.toml` | 2.14 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 213 | `boundary32.7.006492321624085e-45.true.nvm` | `feat/opt/algebra.plan.toml` | 2.13 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 214 | `residual_mlp.simplify.fvm` | `feat/modules/nn.plan.toml` | 2.12 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 215 | `docs.word_form_only` | `feat/opt/pass_params.plan.toml` | 2.12 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 216 | `covered_value.graph.false` | `feat/opt/gate_value.plan.toml` | 2.12 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 217 | `control_data.unused_argument.inline.prl` | `feat/opt/control_data.plan.toml` | 2.12 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 218 | `boundary64.2.5e-323.algebra.prl` | `feat/opt/algebra.plan.toml` | 2.12 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 219 | `boundary32.7.006492321624085e-45.false.prl` | `feat/opt/algebra.plan.toml` | 2.11 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 220 | `integrated.graph.false` | `feat/opt/algebra.plan.toml` | 2.10 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 221 | `boundary32.-0.true.prl` | `feat/opt/algebra.plan.toml` | 2.10 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 222 | `graph_faces.readings_absolute` | `feat/opt/graph_faces.plan.toml` | 2.10 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 223 | `pb_statics_gc.runtime.nvm` | `feat/modules/autodiff.plan.toml` | 2.10 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 224 | `var_grad.inline_tf` | `feat/linear/var_reference.plan.toml` | 2.10 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 225 | `fault_narrow_div_zero` | `feat/trans/cpp.plan.toml` | 2.10 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 226 | `boundary32.-0.false.prl` | `feat/opt/algebra.plan.toml` | 2.09 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 227 | `fused_grad_check.fused` | `feat/modules/autodiff.plan.toml` | 2.09 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 228 | `simplify_arm_write_lost.simplify_fvm` | `feat/opt/progen_simplify.plan.toml` | 2.09 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 229 | `execution.opt.nvm` | `feat/opt/algebra.plan.toml` | 2.08 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 10 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 230 | `var_grad.opt_tf` | `feat/linear/var_reference.plan.toml` | 2.08 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 231 | `cse_arm_constants.inline_arm_cse.nvm` | `feat/opt/generic.plan.toml` | 2.08 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 232 | `attempt.partial_fault.nvm` | `feat/consistency/attempt.plan.toml` | 2.07 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 233 | `branch.42.opt.prl` | `feat/opt/gate_value.plan.toml` | 2.07 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 234 | `residual_mlp_gir` | `feat/modules/nn.plan.toml` | 2.06 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 235 | `control_data.unused_argument.inline_fold.prl` | `feat/opt/control_data.plan.toml` | 2.05 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 236 | `literal_rank` | `feat/trans/cpp.plan.toml` | 2.05 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 237 | `01_scalar_boundary` | `feat/exemplars.plan.toml` | 2.05 | **B** | node 驱动：它驱动一整套检查，成本随树的大小走，不随这条用例走。 |
| 238 | `boundary32.-0.false.nvm` | `feat/opt/algebra.plan.toml` | 2.04 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 239 | `other_wait.graph` | `feat/opt/gate_value.plan.toml` | 2.04 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 240 | `pb_statics_gc.runtime.fvm` | `feat/modules/autodiff.plan.toml` | 2.04 | **B** | 压力模式（`--gc-stress` / `--gc-check`）：按 ④ 它本来就该进压力层，不是提速问题。 |
| 241 | `static_branches.simplify.rgir` | `feat/opt/generic.plan.toml` | 2.04 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 242 | `control_data.graph` | `feat/opt/control_data.plan.toml` | 2.04 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 243 | `tiny_cnn_gir` | `feat/modules/nn.plan.toml` | 2.03 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 244 | `literal_rows_computed` | `feat/trans/cpp.plan.toml` | 2.03 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 245 | `attempt.forms.jit` | `feat/consistency/attempt.plan.toml` | 2.02 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 246 | `var_capture.macro_fold` | `feat/linear/var_reference.plan.toml` | 2.02 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 247 | `capacity.growth_fvm_gc_stress` | `feat/mm/capacity.plan.toml` | 2.02 | **B** | python 驱动：一条用例要起多个进程（编译/推理/多程序语料），成本是环境不是程序。 |
| 248 | `dependent_names` | `feat/compile/generics.plan.toml` | 2.02 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 249 | `siamese_shared_encoder.opt.tf` | `feat/modules/nn.plan.toml` | 2.02 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 250 | `boundary64.2.5e-323.algebra.jit` | `feat/opt/algebra.plan.toml` | 2.02 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 16 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 251 | `arm_call_then_effect.opt.nvm` | `feat/opt/inline.plan.toml` | 2.01 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 252 | `simplify_arm_write_lost.simplify_jit` | `feat/opt/progen_simplify.plan.toml` | 2.01 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 253 | `boundary32.-0.true.jit` | `feat/opt/algebra.plan.toml` | 2.01 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 254 | `attempt.forms.fvm` | `feat/consistency/attempt.plan.toml` | 2.01 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 2 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 255 | `var_grad.simplify_jit` | `feat/linear/var_reference.plan.toml` | 2.01 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 3 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 256 | `control_data.unused_argument.ctrl_inline.prl` | `feat/opt/control_data.plan.toml` | 2.01 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 4 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 257 | `boundary32.7.006492321624085e-45.true.prl` | `feat/opt/algebra.plan.toml` | 2.00 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 11 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 258 | `literal_rows_filled` | `feat/trans/cpp.plan.toml` | 2.00 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 259 | `with_defaults.applied.prl` | `feat/compile/with_defaults.plan.toml` | 2.00 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |
| 260 | `var_read_before_callee_write.macro_simplify_fvm` | `feat/linear/var_reference.plan.toml` | 2.00 | **C** | 单程序、单遍、无驱动，秒数可能就是这个程序自己的活，也可能是 28 路争用的放大——缺一条 CPU 读数或 `--jobs 1` 的单跑读数。 |
| 261 | `simplify_growth_budget.simplify.nvm` | `feat/opt/generic.plan.toml` | 2.00 | **A** | 同一份用例在本层按 backend / 遍展开的多条之一（这份程序在本层有 1 条 >2 s）：按重构计划第 3 条收成每类一个代表，其余留矩阵层。 |

合计：A 112 条（留快层，收代表）、B 106 条（进 slow）、C 43 条（说不清）。
