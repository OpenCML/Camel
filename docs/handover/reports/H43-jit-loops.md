# H43 第 2 项：JIT 编译含循环的图（验证报告）

- 分支：`handover/H43-jit-loops`，工作树 `D:\Projects\Camel-wt\h43jit`。
- 基点：`handover/H43-region-perf`（`8cd2880a`）；合并 `origin/develop`（`ea3a196c`）后的合并提交：`ef7b8a11`。
- 本分支的代码提交：`7f533bd7`（`feat(jit): compile a graph with a loop at its first call`）。
- 本报告提交：见 `git log -- docs/handover/reports/H43-jit-loops.md`。
- 测量主机：`88zjwei`（Linux x86-64，32 核 EPYC 7343，共享；构建与运行都用 `camel-remote.ps1`）。

## 0. 结论

1. **第 2 项（`LBEG`/`LTST`/`LEND` 的 x64 下降）已经在 develop 里**：`48345984`（H43a）把三个循环事件编译成有帧代码，经
   `jitHelperLoopBegin/Test/End` 调 `camel/execute/loop_frame.h` 的同一份规则；H45 又修了"循环在 `if` 臂里给错值"。
   H43-perf 报告里"含循环的图仍解释执行"只对它的基点 `fc62829f` 成立，对合并后的树不成立。
2. 合并后仍有一个真实缺口：**默认 `std::jit` 下，写在入口函数里的循环不会被编译**。`bench_loop.cml` 的循环在 `main`，
   `main` 只被 `__root__` 调一次，而 OnDemand 的 `hot_threshold = 1` 要求同一调用点第二次调用才编译，于是循环仍解释执行
   （同一批 0.448 s，与 `std::fvm` 的 0.444 s 相同）。这不是循环下降的问题，是分层策略看不见"轮次"。
3. 补法（`7f533bd7`）：`TierPolicy::shouldJit` 增加 `hasLoop`，`graphHasLoopRegions` 直接读 `ExecPlan::loops`；在阈值
   0 与 1（默认）下，含循环区域的图**第一次调用就编译**。阈值显式大于 1 时该捷径不生效，所以
   `hot_threshold=1000000000`（"什么都不编译"的度量方式）语义不变。
4. 修复后，默认 `std::jit` 跑 1000 万轮两状态 `while`：**0.342 s**，同批 `std::fvm` 0.430 s（快 20%），比 H43-perf 的
   `std::jit` 0.435 s 快 21%。JIT 自己的统计与日志证明循环跑在编译出的代码里：`main` 为 `compiled` / `framed`、
   `executed = 0`、`runtime` 含 `LOOP`，日志为 `OnDemand: compiled & cached graph 'main' as framed code`。
5. H43 的 0.155 s 目标仍**未达到**（还差 2.2 倍）：每轮仍有两次辅助函数调用与帧上的读写；编译后的同一份尾递归是
   8.0 ns/轮，循环是 34.2 ns/轮。把 `LTST`/`LEND` 原生化（状态放寄存器、测试与轮末内联）是下一步，不属于第 2 项。

## 1. 合并与事实核对

`git log --oneline`（本分支）：

```
7f533bd7 feat(jit): compile a graph with a loop at its first call
ef7b8a11 Merge origin/develop into the H43 loop branch
8cd2880a docs(handover): H43 item 7 measured; the targets are not met, and where the rest is
```

`origin/develop` 带来的相关提交：

- `48345984 feat(jit): compile loop regions (LBEG/LTST/LEND) as framed code`（H43a）：三个事件经辅助函数在帧上执行，
  含循环的图因此是有帧代码（`collectRuntimeConstructs` 的 `LOOP`），`LEND` 在轮次边界轮询安全点。
- H45：`if` 臂里的循环在第一个轮次从臂区域的起点开始（`arm_loops` 矩阵）。
- 其余 develop 内容按内容合并；冲突只在 `docs/handover/README.cn.md`、`docs/technical/39_loops.md`、
  `x64_backend.cpp`、`trampoline.cpp` 四处，均已按两边内容合并（`IMOD`/`LMOD` 与 `LOOP` 两个运行时构造都保留）。

合并后、补缺口前的测量（同一台主机，RUSAGE_CHILDREN 整进程 CPU 时间，5 次交替取中位数，N=10000000，
loadavg 59.72）：`std::nvm` 1.596 s、`std::fvm` 0.444 s、`std::jit` 0.448 s、`std::jit(hot_threshold=0)` 0.361 s。
默认 `std::jit` 与 `fvm` 相同，说明循环仍在解释执行；`hot_threshold=0` 已经能编译并快 19%——这证明循环下降本身是好的，
缺的只是"只进入一次的函数"没有达到热度。

## 2. 补的缺口：只进入一次的入口函数

机制（`src/passes/sched/fastvm/fastvm.cpp`、`jit/tier/tier_policy.{h,cpp}`）：

- 直接调用（`materializeCallTarget`）与经函数值的调用（`invokeCallOrJit`）都把
  `graphHasLoopRegions(target)` 传给 `TierPolicy::shouldJit`；
- `graphHasLoopRegions` 读版本自己的执行计划：`graph->execPlan().loops` 非空即含循环区域。不用扫描字节码，
  也不额外占每张图的元数据；
- `shouldJit` 在 OnDemand 下返回 `eagerLoop || callCount > hotThreshold`，其中
  `eagerLoop = hasLoop && hotThreshold <= 1`。`JitPolicy::Disabled` / `Always` 的语义不变。

为什么只在阈值 0/1 生效：0 与 1 是"尽早编译"的两档（0 是第一次调用、1 是第二次），默认就是 1；再高的阈值是调用方
显式要求冷代码保持解释（包括 `hot_threshold=1000000000` 这种"不编译"度量），不应被循环规则覆盖。文档已同步：
`docs/cli.md`、`docs/cli.cn.md`、`docs/technical/07_jit_architecture.md` 14.1。

修复后默认 `std::jit` 的 JIT 自己的证据（`N=100`，`std::jit(stats=...)`，`-vv`）：

```
OnDemand: compiled & cached graph 'main' as framed code
{"name": "main", "pc": 0, "ops": 17, "executed": 0, "status": "compiled", "mode": "framed",
 "reason": "", "blockers": [],
 "runtime": [..., {"kind": "LOOP", "detail": "", "count": 3}, ...]}
```

`executed = 0` 表示驱动循环没有为 `main` 执行任何字节码（解释执行的图 `executed` 等于它的字节码条数）。
`__root__`（2 条字节码的入口包装图，没有循环）仍是 `compilable`、`executed = 2`：它由 `call()` 直接解释，
不经过 FUNC 调用点，所有版本都是如此，与循环无关。

## 3. 机制：编译出的循环长什么样

`x64_backend` 对 `LBEG`/`LTST`/`LEND` 发 `emitCallHelper`（`jitHelperLoopBegin/Test/End`），返回值在 `rax`，
`LTST`/`LEND` 用 `VTest` + `JneRel32(getJumpTargetPc)` 跳到自己的目标（`LTST` 停时跳到 `LEND`，`LEND` 还有一轮时
跳回轮次开头）；跳转目标是 `blockStarts` 的一项，`slotCache` 在事件与跳转处清空，所以每轮都从帧重新读写状态。
`LOOP` 出现在 `collectRuntimeConstructs` 里，含循环的图因此走有帧代码（驱动循环 + 续点），不会退回解释器。
这与解释器（`gotovm.cpp`/`casevm.cpp`）逐句相同，规则只有 `loop_frame.h` 一份。

## 4. 现在能编译哪些循环形状（证据）

`std::jit(hot_threshold=0, stats=...)`，`test/cases/loops/` 与 `docs/design/loops/examples/bench_loop.cml`：

| 形状 | 用例/图 | 统计 |
|---|---|---|
| 一个状态、数组上求和 | `compiled.cml` / `sum_squares` | `compiled`, `framed`, `executed=0`, `LOOP` |
| 两个状态、条件在上的 `while` | `bench_loop.cml` / `main` | 同上，`LOOP` 3 个事件 |
| `for … while` + `if` | `until_above` | 同上 |
| 空数组 | `over_empty` | 同上 |
| 嵌套循环 | `nested` | 同上，`LOOP` 6 个事件 |
| 收集（每轮 append） | `collects` | `compiled`, `framed`, `executed=0`, `LOOP` + `:struct/append_arr_owned` |
| 循环体里调另一张图 | `rows_total`、`through_call` | `framed`，`FUNC-other-graph` + `LOOP` |
| 同步体 | `ticks` | `framed`，`LOOP` + `ACCS`/`FILL` |
| 块返回 | `sum_to` | `framed`，`LOOP` + `ACCS` |
| `if` 臂里的循环（H45 矩阵） | `arm_loops.cml` / `two_arms`、`then_for`、`else_for`、`while_arm` | 全部 `compiled`, `framed`, `executed=0`, `LOOP` |
| 臂里的循环 + `match` | `match_arm` | `compiled`, `framed`；运行时构造 `BRCH-match` 与 `LOOP` 并存 |
| 内联后的入口函数 | `loops.compiled` 的 `jit_hot_threshold0`/`inline_jit_hot_threshold0` | 见第 6 节计划 |

也就是说：**没有一种循环形状被拒绝**；嵌套、臂内、收集、`for … while`、循环体内调用、同步体、块返回都编译成
有帧代码。`match_arm` 说明即使图里还有 `match`（`BRCH-match`），循环也照样编译，只是整张图继续走有帧路径。

## 5. 数字

方法同 H43-perf：`~/bench.py`/`~/benchcases.py`，`taskset -c 3`，整进程 `user+sys` CPU 时间，批内交替，5 次取中位数；
`N=10000000`；`bench_loop.cml` 与 `bench_tail.cml` 输出一致（`29999994`）。机器共享，loadavg 在 26 到 75 之间，
所以只看同一批里的比例，跨批只作量级参考。

最终树（`7f533bd7` 构建，loadavg 34.48）：

| | `std::nvm` | `std::fvm` | `std::jit` | `std::jit(hot_threshold=0)` | 手写尾递归 `fvm` | 手写尾递归 `jit` |
|---|---|---|---|---|---|---|
| 10M 轮两状态 `while` | 1.620 s | 0.430 s | **0.342 s** | 0.341 s | 0.295 s | 0.080 s |

- 每轮：nvm 162.0 ns、fvm 43.0 ns、jit **34.2 ns**、尾递归解释 29.5 ns、尾递归编译 8.0 ns。
- 同批 `std::jit` 比 `std::fvm` 快 **20.5%**（0.430 → 0.342）；比 H43-perf 的 `std::jit` 0.435 s 快 **21%**。
- 修复前（同一合并树，未加循环规则，loadavg 59.72）：`std::jit` 0.448 s，`std::jit(hot_threshold=0)` 0.361 s。
- H43-perf 原来的 before（loadavg 26.2）：nvm 1.571 s、fvm 0.436 s、jit 0.435 s、尾递归 0.310 s。
- H43 的目标 0.155 s（当时手写尾递归 0.310 s 的一半）仍未达到：0.342 s / 0.155 s ≈ 2.2 倍；按今天的手写尾递归
  0.295 s 算，一半是 0.148 s，更远。差距就是每轮两次辅助函数调用和帧上的读写，编译后的尾递归（8.0 ns/轮）说明
  原生化后仍有很大空间。

数字来源：最终树一行全部来自 `88zjwei` 本次测量；"修复前"一行来自同一主机、同一脚本的合并后未补缺口构建；
H43-perf 一行引自 `docs/handover/reports/H43-perf.md`（同主机、同方法，loadavg 26.2）。`std::jit(hot_threshold=0)`
与默认 `std::jit` 现在给出相同的 0.341/0.342 s，这本身说明默认路径已经编译循环。

## 6. 测试

在合并后的树上、补缺口后（`-NoBuild`，Jobs 8）：

| 计划 | 结果 |
|---|---|
| `test/plans/feat/linear/loops.plan.toml` | 114/114 |
| `test/plans/feat/linear/loops_in_arms.plan.toml` | 23/23 |
| `test/plans/feat/linear/jit_framed.plan.toml` | 56/56 |
| `test/plans/feat/modules/graph.plan.toml` | 51/51 |
| 四个计划合计 | **244/244** |

`loops.plan.toml` 包含 `loops.compiled`（nvm/fvm/jit_hot_threshold0/inline_jit_hot_threshold0，`backends_agree`）、
`compiled.fault`（第 3 轮除零的诊断位置）、`compiled.moving`（分代回收）、`compiled.round_safepoint`（GC 压力下的
轮次安全点）；`jit_framed.plan.toml` 的 `framed.loops.regions.*` 用 `-vv` 日志断言含循环的图被编译成有帧代码。
`graph.plan.toml` 是任务要求中 `std::jit` 与 `std::fvm` 必须一致的那份。

全量 fast 层（`smoke` + `test/plans/feat`，Jobs 8，共享主机）跑了两次：

- 第一次 **4749 个，4733 过，14 跳过，2 失败**：`debugger.dap_pipeline` 与 `debugger.dap_memory`；单独串行重跑
  `debugger.plan.toml` 时两者都过，只有已知的 `debugger.dap_stepping` 失败（6 条尾调用帧断言，与 H43 无关的既有
  失败，未改动），串行结果 11/12。
- 第二次 **4749 个，4732 过，14 跳过，3 失败**：`gpu_preference.no_device.{fvm,jit_hot_threshold0,prl}` 各用
  10.02–10.13 s，撞上计划里 `timeout_ms = 10000`；H39 已记录这三个用例单独跑也要 9.2–9.4 s（`CAMEL_GC` 分代 +
  `GC_VERIFY=both` + GC 压力），是主机负载下的超时抖动。

两次失败集合不同、都不是循环或 JIT 相关，且单独重跑即过；`debugger.dap_stepping` 是唯一稳定失败，属于既有问题。

## 7. 仍然不能编译 / 仍会解释执行的情况

**形状上：没有。** 所有测过的循环形状都编译成有帧代码（第 4 节）。仍会解释执行的是这些，都不是"含循环的图被拒绝"：

1. **图没有达到阈值、且不含循环**：`std::jit` 默认 OnDemand 的正常语义；统计里是 `compilable`，不是 `compiled`。
   本改动只免掉"含循环的图"的阈值限制。
2. **入口包装图 `__root__`**（2 条字节码，无循环）：由 `call()` 直接解释，不经过 FUNC 调用点；每个版本都如此。
3. **调试器下的 `std::jit`**：按设计退化为 `std::fvm`，循环解释执行（调试器要逐字节码停）。
4. **非 x86-64 构建**（`ENABLE_FASTVM_JIT=0`）：没有 JIT，全部解释执行。
5. **编译拒绝**（`collectBlockers`）：`no-graph`、`no-frame-layout`、`empty`、`BRCH-table`（字节码的跳转表不完整），
   以及后端 `compile()` 的 `register allocation failed`。这些是链接后不应出现的内部不一致，任何测过的循环形状都没有
   触发；一旦发生，`CAMEL_LOG_WARN` 会打印 `OnDemand compile skipped for graph '…': …`，统计的 `blockers` 也会列出，
   不是静默回退。

## 8. 没做的事

- **原生下降 `LTST`/`LEND`**（跳转内联、状态留在寄存器、轮末不经过 C++）：这是把 34.2 ns 拉向 15.5 ns 的下一步，
  不属于第 2 项；本分支只确认第 2 项已成立并补上分层缺口。
- 没有改 `LEND` 的安全点轮询、收集与故障语义；`compiled.fault`/`compiled.moving`/`compiled.round_safepoint`
  仍然通过。
- 没有改 `debugger.dap_stepping` 的既有失败。

## 9. 复现

```bash
# 构建（主机）
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h43jit -Jobs 8
# 计划
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h43jit -NoBuild \
  -Plan test/plans/feat/linear/loops.plan.toml,test/plans/feat/linear/loops_in_arms.plan.toml,test/plans/feat/linear/jit_framed.plan.toml,test/plans/feat/modules/graph.plan.toml -Jobs 8
# JIT 自己的证据（N=100）
CAMEL_HOME=out/latest N=100 out/latest/bin/camel docs/design/loops/examples/bench_loop.cml -vv \
  'std::jit(stats="/tmp/h43.json")'
# 数字
N=10000000 python3 ~/benchcases.py <checkout> 5 \
  "loop_fvm:.../bench_loop.cml:std::fvm" "loop_jit:.../bench_loop.cml:std::jit" \
  "tail_fvm:.../bench_tail.cml:std::fvm" "tail_jit:.../bench_tail.cml:std::jit"
```