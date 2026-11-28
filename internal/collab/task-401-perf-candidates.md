# 后端提速：候选点 + 量法 + 风险级（静态排查，未改代码）

范围：`src/stdlib/std/nvm/`（节点 VM）、`src/stdlib/std/fvm/`（字节码 VM）与它们调用的执行面。
方法：只读 `git grep` + 读源码，**不含任何计时**（当前宿主 1m 负载 30–44，`perf` 计划的判据是
`cpu_budget_ms`，此时红的是预算不是功能，读数不可用作对照）。

## 一、候选点

### 1. 每次进入执行器复制一整张 `ExecNode` 表 —— A 级
- `src/stdlib/std/nvm/nodevm.cpp:354`
  `std::vector<camel::runtime::ExecNode> currRecordsOf = currPlan->nodesOfTopo();`
- 现状：`nodesOfTopo()` 按值返回整表（`src/kernel/include/camel/runtime/exec_plan.h:168`
  `std::vector<ExecNode> nodesOfTopo() const;`），进入执行器即复制一次；注释自己写着"plan hands them over
  by value -- it keeps no table a reader fills in"。
- 改法方向（不动语义）：把这张表在 `ExecPlan` 里**存一份**、执行器拿 `std::span` 读；
  或按计划缓存本后端自己的副本，`currRuntimeGraph` 不变就不重建。

### 2. 循环体内的 `std::vector` 反复分配 —— A 级
- `src/stdlib/std/nvm/nodevm.cpp:374` `std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> enclosingArms;`
- `src/stdlib/std/nvm/nodevm.cpp:381` `std::vector<gc_node_ref_t> groupDeferred;`
- `src/stdlib/std/nvm/nodevm.cpp:476` `std::vector<gc_node_ref_t> groupMembers;`
- `src/stdlib/std/nvm/nodevm.cpp:704` `std::vector<slot_t> withValues;`
- 现状：都在执行路径上按需构造；同一次运行里同一形状反复出现（分支/循环每轮一次）。
  量法见 §二第 2 条：这类点靠 **`instruction_counts` / `step_counts` 看不出来**（它们数的是步，不是分配），
  必须用 `cpu`/`wall` 量 ⇒ **等窗口**。
- 正面样板（不改，作为形状参照）：`nodevm.cpp:164` 的 `std::vector<slot_t> &scratch` 已经是**按引用传入的复用缓冲**
  ——第 2 类点的改法与它同形。

### 3. 运行期按名字/Uri 查算子 —— A 级（先量命中率再动）
- `src/stdlib/std/fvm/compile.cpp:85` `static const std::unordered_map<std::string_view, OpCode> byUri = …`
- 现状：编译期（每次 `std::fvm` 一遍）逐节点查一次静态表；**运行期不查**。
- 若量出来它占 fvm 一遍的可见份额，改法是把它换成按 `NodeOperator` 枚举的直接映射（`switch`/数组），
  保留 URI 表只给诊断用。

### 4. 每节点一次 `unordered_map` 查找可用一条缓存替代 —— A 级
- 与 3 同源：`compile.cpp` 里 `byUri` 的查找在节点数 × 遍数上重复；一个 `NodeOperator -> OpCode` 的
  扁平数组（枚举值直接下标）即可，且**不需要字符串**。

### 5. B 级（先报，不动）
- 任何要改 pass 顺序、图结构、`ExecPlan` 拓扑生成方式的想法（例如"把 topo 预转成下标的跳转表"）——
  会改变执行期的可达顺序，属 §一.1 的上位改动，**先报再议**。

## 二、每点的量法

1. **确定性计数（不受负载影响，先用它）**
   ```
   camel-remote.ps1 -Path <树> -Plan 'test/plans/perf/instruction_counts.plan.toml,test/plans/perf/step_counts.plan.toml' -Jobs 1 -NoGpuWatch
   取：[SUMMARY] 与用例 stdout 里的计数行；同一份计划连跑 2 次，**两次必须完全相同**（计数是确定量，不同说明量具坏了）
   ```
   适用：候选 1、3、4（它们改的是"每条指令做多少工作"，不是"做多少条指令"时，计数不变 —— 见下面的提醒）。

2. **cpu / wall（会被负载吃掉 ⇒ 等窗口）**
   ```
   camel-remote.ps1 -Path <树> -Plan 'test/plans/perf/fib.plan.toml,test/plans/perf/startup.plan.toml' -Jobs 1 -NoGpuWatch
   取：`[FAIL]/[PASS] <name>  A s cpu of B s budget` 里的 A（cpu 秒），或 `[SUMMARY]` 同侧的读数
   规矩：**连跑 3 次取中位**；三次跨度 > 5% 视为噪声、不下结论；`[load] 1m > 8` 一律写"等窗口"
   ```
   本轮实测（**不可用作基线**，仅作为"为什么现在不能量"的读数）：
   ```
   perf/fib        [SUMMARY] total 6 / pass 2 / fail 4   nvm_timeit 11.80 s cpu of 9.00 s budget   [load] 38.44
   perf/startup    [SUMMARY] total 1 / pass 0 / fail 1   startup.cost 19.73 s cpu of 17.50 s budget [load] 30.53
   perf/step_counts[SUMMARY] total 2 / pass 0 / fail 2                                              [load] 30.71
   ```

3. **分配次数（候选 2 的主量）**
   - 现在没有现成量具：这类点的改前/改后必须给**同一程序、同一 tip、同机**的 cpu 秒（§二.2），
     并附"两次都在 load < 8 的窗口、各 3 次中位"。
   - 若窗口一直不来，**记录该点未量**，不要用指令计数代替（指令计数不反映分配）。

4. **判据口径**：提速以 **cpu 秒**为准，`wall` 只作附注；超过 **5%** 才叫"测出差异"，
   低于就写"未测出差异（噪声内）"。功能面同时给红集：改前后各跑一次
   `test/plans/feat/compile`，逐名比当前晋级红表，**甲 0** 才算没弄坏东西。

## 三、风险分级

| 级 | 含义 | 本清单中的点 |
|---|---|---|
| A | 只改调度/内存复用/常量，不碰语义、不改断言、不改 pass 顺序 | 1、2、3、4 |
| B | 要碰 pass 顺序、图结构或 `ExecPlan` 的拓扑来源 | 5（先报） |

## 四、状态

- 本文件是**排查清单**，不含代码改动；`git status` 干净。
- 基线（`perf/*` 的 cpu 秒）与 A 级改动的改后读数**均等窗口**：宿主 1m 负载降到 8 以下再取。
- 先把 §二.1 的确定性计数取到（不受负载影响），作为"A 级改动有没有改变执行步数"的护栏 —— 这一步随时可做。