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
## 五、A-1 降级为 B（卡面作废）与 A-3 前提更正（2026-11-28 下午，只读）

### A-1：不是 A，是 B —— 按卡面改会违反 `ExecPlan` 的契约

```
exec_plan.h:16-19  "What the kind hands out is a whole immutable value: nothing a back end records
                    while it runs lives in it. The state a bytecode engine keeps about the code it
                    made for a version is the engine's own, keyed by the plan (fvm/version_state.h) …"
exec_plan.h:165-168 "… handed over by value, because a plan is a value and keeps no table a reader
                    fills in -- the reader holds the table it walks."
调用点：nodevm.cpp:354（进执行器一次）、nodevm.cpp:667（切换到被调图一次）—— 全仓仅此两处
```
⇒ 把表存进 `ExecPlan` = 第二份真相 + 违反契约；正确形状是**读者侧缓存（后端自己的状态、按 plan 键）**，
属"给后端加状态"，降为 **B**。收益上限也只是"每次进入/每次跨图调用省一张表"，不在节点循环里。
**没有窗口读数证明 >5% 之前不做。**

### A-3 的前提要更正：字符串表**只在静态初始化时用一次**

```
fvm/compile.cpp:84-123
  static const std::unordered_map<std::string_view, OpCode> byUri = { … 40 条 URI … };   // 一次
  static const std::unordered_map<OperId, OpCode> supportedInlineOperators = [] {
      for (const auto &[uri, code] : byUri) byId.emplace(OperatorTable::instance().intern(uri), code);
  }();                                                                                    // 也一次
```
⇒ **编译期并不逐节点查字符串表**：字符串只在第一次建表时被 `intern` 成 `OperId`；编译时每节点做的是
`unordered_map<OperId, OpCode>` 的哈希查找。所以 A-3 按原写法（"换成枚举下标、去掉字符串"）**是空改动**，
真正剩下的候选是 A-4 的形状：**按 `OperId` 直接下标的数组**（由同一张表在初始化时填，不新造第二份真相）。
这一条要先确认 `OperId` 的取值范围与稀疏度，再决定用数组还是分段表；**未量、未改**。
## 六、A-4 只读探测的结论：**不值得**（未改代码）

### ① `OperId` 是什么、最大值从哪来
```
src/kernel/include/camel/core/oper_id.h:15        using OperId = uint32_t;
src/kernel/core/operator_table.cpp:117-135        OperId OperatorTable::internLocked(std::string_view uri) {
                                                     … const OperId id = entries_.append(...); …
                                                  }
⇒ OperId 是**按 intern 顺序 append 出来的密集序号**，最大值 = 该时刻已 intern 的**互异算子 URI 总数**，
   不是 40（40 是 `:op/*` 里被字节码内联的那一批）。序号在 `entries_` 里连续，但"内联的那 40 个"
   在序号空间里的分布取决于模块注册顺序，`.cml` 标准库进来之后表会长到远超 40。
src/kernel/core/operator_table.cpp:90             `internLocked(kWellKnownOperatorUris[known])`
⇒ `:op/*` 属**早期 well-known** 一批（该文件里引号字面量 57 处），所以这 40 个 id 大概率落在低位一小段。
```
### ② 稀疏度：`max_id / count` **无法静态给出精确值**，但可给界
```
· count = 40（内联算子数，`fvm/compile.cpp:85-113` 逐条可数：add/sub/mul/div/mod 4 类 ×4 型 + 6 个比较 × 4 型）
· max_id = 已 intern 的互异算子总数：`entries_` 是 append-only（`operator_table.cpp:132`），
  含全部 well-known + 所有被载入模块注册的算子 + stdlib 导入的算子
⇒ 稀疏度 = max_id / 40。**不做运行时探测就说不准具体倍数**；但从"well-known 57 条 + 各模块注册"这个结构看，
  它**很可能 ≥ 8×**（即 max_id ≥ 320）。按你给的判据：> ~8× ⇒ **扁平数组不可行、要分段表**。
· 精确值要么在窗口里用一次 `camel --log-level trace` 之类读，要么另开一次带探针的构建 —— 两者都超出"只读"。
```
### ③ 数组（若做）只能由同一张表填
```
· 唯一合法来源：`supportedInlineOperators` 建好之后，用它的键值对填数组（`for (auto &[id, code] : map) arr[id] = code;`），
  **不许**另写一份 `id -> OpCode` 的清单（那就是第二真相；40 条 URI 已经是唯一记录）。
```
### ④ 结论：**不值得**
```
· 收益发生在**编译期**，规模是"每个节点一次哈希查找"（`compile.cpp` 的 `supportedInlineOperators.find(id)`），
  一次 `unordered_map<uint32_t,...>` 查找 ~10–20 ns；一张 1000 节点的图 ⇒ 约 10–20 µs，
  相对一次 fvm 编译（ms 级）与 `perf` 的 cpu 预算（`9.00 s` 那一档）不足 1%；
· 而"证明收益"必须先过窗口（cpu 秒），产物一致只能证"没改坏"；
· 稀疏度很可能 ≥8× ⇒ 扁平数组方案本身可能不成立，得分段表 ⇒ 引入第二处索引结构，风险上升。
⇒ **A-4 判"不值得"，不再做；`instruction_counts`/`step_counts` 也不为它跑**（编译期收益不进这两把尺子）。
```

## 七、当前状态：无可用 A 级动作，进等窗口

| 点 | 级 | 状态 |
|---|---|---|
| A-1 表复制 | **B** | 挂起（`exec_plan.h` 契约 + 收益上限两条读数，见 §五） |
| A-2 四处按需 vector | A | **只能 cpu 秒证明**，未取基线 ⇒ 等窗口 |
| A-3 字符串表 | — | **作废**（前提不成立，见 §五） |
| A-4 枚举下标数组 | A | **不值得**（见 §六） |
| B-1 拓扑/pass 顺序 | B | 不动 |

等窗口条件：`origin/integration` 的 70 那批落定 **且** 宿主 `[load] 1m < 8`。
到窗口时按序：A-2 改前/改后 cpu 秒（各 3 次中位）→ A-1（若 B 批了）→ A-4 不测。
## 八、A-2 实测收窄：清单说"四处"，能省的只有两处（2026-11-28，改完代码后回填）

改完 `nodevm.cpp` 再逐处读，四处里有**两处的前提不成立**：

```
:374  std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> enclosingArms;
:381  std::vector<gc_node_ref_t> groupDeferred;
  两者都是"默认构造 + 需要时 push_back"：**默认构造的 vector 不分配**，不用就一个字节也不给；
  而且它们本来就在 `loop_start` 之外、一次运行只构造一次 ⇒ **没有"每处一次分配"可省**。保持原样。
:476  std::vector<gc_node_ref_t> groupMembers;      ← 在节点循环内，每个"组尾"节点每轮一次
:704  std::vector<slot_t> withValues;               ← 每次"带写入的调用"一次（还带 reserve）
  这两处才是真按需分配；改为函数作用域的两只缓冲（`groupMembersOf`/`withValuesOf`），
  用前 `clear()`，容量跨轮/跨调用复用 ⇒ 分配只在第一次需要更大容量时发生。
```

结论：**A-2 = 两处**（`:476`、`:704`），不是四处；另两处记"前提不成立"。
语义面不改：两处都在填充前 clear，且都不跨节点存活。
计时口径：交错 A/B（改后/改前交替各三次、每次记 `[load]`），**两侧 load 差 ≤ 2 才下结论**，否则写"未测出差异"。