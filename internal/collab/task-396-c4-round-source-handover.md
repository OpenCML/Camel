# `task-396` / C4 第三种轮数来源：现状、读数、下一刀

写于 2026-11-28，作者 `lane-editview-commit`（本会话上下文将尽，按 `D-1128-17` 交卡不交半笔改动）。

## ① 当前一步

- 工作树：`D:\Projects\Camel-wt\hdr-channel`（原先 detached，未被别的车道占用）
- 分支：`lane/c4-round-source`，从 `origin/integration = 368689229c`（`Merge seat/ccl3 into integration`）切出
- 枝上**零改动**：`git status` 为空；本卡是这棵树上的第一笔提交
- 要做的事（`internal/plans/2026-11-28-next-12h.cn.md` 第三段第 1 项）：手写线性循环 + 条件读非线性状态 + **无残差数组**时，现在给"诚实拒绝"（算不出轮数 n）；改成**把正向循环的非线性部分（条件与控制状态）重放一遍、外加计数**得 n，取消这一形态的拒绝

## ② 读数（真跑，`internal/tools/camel-remote.ps1`，88 主机）

基线（枝尖 `368689229c`，`test/plans/feat/compile/transforms.plan.toml` 整计划）：

```
[SUMMARY] total: 219, pass: 210, fail: 8, known failures: 1, timed out: 2, re-run: 2, re-run passed: 0
[provenance] tip 368689229c1a02f4c1136741184756d958faf28d | binary built 2026-11-28T08:45:39+08:00 | tree /home/zjwei/camel-work/hdr-channel | tier fast | jobs 4
```

8 条红逐名（全部在 `2026-11-28-promotion-68-reds.tsv` 的 `transforms` 行里，属**丙**）：

```
transpose_runs_a_loop_rounds_backwards.nvm
transpose_loop_escape.names_the_value_one_read.nvm
transpose_loop_escape.one_value_read_twice_is_one_capture.nvm
transpose_loop_escape.two_values_of_two_kinds.nvm
transpose_loop_escape.int_is_a_zero_tangent_space.nvm
transpose_loop_escape.two_states_before_the_escape.nvm      （超时）
transpose_loop_escape.second_state_of_the_same_loop.nvm     （超时）
cml_var_holding_a_lambda                                    （同在 68 表 transforms 段）
```

**没跑的**：`probe.editview`、`feat/modules`、`feat/trans`（这格还没改代码，等改完一起跑）；本卡不知道有没有现成用例能触发那条态拒绝，**尚未找到**（见 ③ 的第一件事）。

## ③ 下一刀怎么接（代码位置全部读过，按此落笔）

**要删的那条拒绝**（`src/stdlib/transforms/transpose/walk.cpp:3066-3074`）：

```cpp
if (readsAState) {
    refuse(TransposeRefusalCause::State);
    os << " node=" << ref << " origin=" << regionOrigin
       << " why=this-version-does-not-yet-recompute-the-rounds-of-a-loop-"
          "whose-condition-reads-a-state\n";
    return false;
}
```

它上面那段注释就是设计说明：条件若只读它自己的控制（pull 设的旗标），重跑 pull 数次数就行；**条件读任何别的状态值**时，"recomputing those rounds means replaying the body, which this version does not do" —— 本卡要做的正是这个"replay the body"。

**轮数的两个既有来源**（`src/stdlib/autodiff/pullback.h:80-104`）：

```cpp
struct ReverseRoundCount {
    enum class Source : uint8_t { TapeLength, Recomputed };
    Source source = Source::TapeLength;
    gc_node_ref_t tape  = kInvalidNodeRef;   // TapeLength
    gc_node_ref_t value = kInvalidNodeRef;   // Recomputed
};
```

- 读它的地方只有两处：`transpose/walk.cpp:3300`（`.source = Recomputed`）、`pullback.cpp:2482`（`.source = TapeLength`）；
- 消费它的地方只有一处：`pullback.cpp:5646 reverseFirstRound()`，按 `source` 二选一；反循环的条件由 `buildReverseCondRegion` 共享。
⇒ 第三种来源（建议 `Source::Replayed`）在这三处各加一支，**不改** `TapeLength`/`Recomputed` 的判据。

**形状**（照现有计数循环的路子，`walk.cpp:3030-3300` 那一段是现成模板）：
1. 从 `function->region(r)` 里取该循环的 `LoopCond` 区（`condRegionOld` 的取法现成，`walk.cpp:3043-3052`）；
2. 建一个自己的计数循环：状态 = 正向循环的状态 + 一个 `int` 计数器；条件 = **正向循环的条件区的复制**（不是"只数 pull"）；体 = **正向体里更新状态的那些节点 + 计数器加一**（这就是"非线性部分重放"）；
3. 计数循环结束后，`value` = 计数器（`Source::Replayed`），交给 `reverseFirstRound`；
4. **有界**：轮数来自计数器本身，天然有界；不得引入任何"猜一个上限"的常数。

**第一件事（判断用哪个用例）**：`git grep -n 'why=this-version-does-not-yet-recompute' -- test` **为空** ⇒ 现在没有用例钉住这条拒绝，需要**新写**一条（线性循环、条件读状态、无残差数组），先量出改前拒绝原文（带 `node=… origin=…` 位置），改后同一用例转 `[PASS]`，并给出"重放得到的 n 与真值一致"的证据（该用例把 n 从结果里读出来，与手工算的值比对）。

**两侧用例（必须仍走原路）**：有残差数组的（`TapeLength` 那条）、条件不读非线性状态的（`Recomputed` 那条）各贴 `[PASS]`；现有 `transforms` 的 8 条丙红不得变多。

## ④ 边界

- 只加一条来源，**不改**已有两条来源的判据（要动就停下贴两边读数报给负责人）。
- 不推远端、不 merge、不开别的枝；构建只走 88 主机；不动冻结目录（`src/stdlib/autodiff/` 里的求导引擎是冻结的对照实现 —— 本卡对 `pullback.h` 只加枚举与一支消费，不改引擎本体）。
- 判据：`[SUMMARY]`/`RUN_EXIT`/`[provenance] tip + binary built`/`numstat`/`git status` 空；红集对 `promotion-68`（147 行、`plan`/`case` 两键、两侧 `Trim`）**甲 0**。
## ⑤ 续做记（2026-11-28，接手这条车道）

### 已落

- `62a77296cf` 钉住拒绝的用例 + 计划条目：`test/cases/compile/transforms/transpose_loop_condition_reads_state.cml`、
  `test/plans/feat/compile/transforms.plan.toml`（条目 `transpose_loop_condition_reads_state`）。
  `feat/compile/transforms` 整计划：**220 例、8 红**（原 219/8，红集逐名未动），新条目 `[PASS]`（它钉的是拒绝本身）。
- 改前原文（本车道实跑，`--trace transforms`）：

```
TRANSPOSE function=halve skip=state node=17 origin=0 why=this-version-does-not-yet-recompute-the-rounds-of-a-loop-whose-condition-reads-a-state
transforms: transpose has no transpose for this function: a loop ...
```

### 量到的两个事实（都来自实跑，不是推断）

1. **能走到这条拒绝的循环形态是 `while` 头部 + 三个状态**。`while` 循环是 `LoopKind::While`：
   `loopHasIterable(While) == false`。一个状态、两个状态的 `while` 循环**走不到**这条拒绝，
   先被上一道门拦成 `why=the-loop-carries-no-state`。
2. **`walk.cpp` 里 `stateCount = regionPorts.size() - (iterates ? 1 : 2)` 对 `while` 循环是错的**。
   临时插桩（已 `git checkout` 还原）读到：三状态的 `while` 循环 **`ports=3 stateCount=1`** ——
   它实际带 3 个状态。运行期自己的读法是 `src/kernel/runtime/loop.cpp:93`
   （`inputs(Normal).size() - (loopHasIterable ? 1 : 0)`，`while` 不扣 2）。

⇒ 这一条**不是“只加一条来源”能带过的**：计数器要接到反向循环上，而反向循环的状态数就由这个 `stateCount` 决定；
在 `while` 上它现在是 1（真值 3）。照现在的数接下去，重放出来的 n 会被安到一个状态数不对的反向循环上。

### 停下来问的一件事

- 判据本体要不要改：把 `stateCount` 按 `loopKindOf` 分（`while` 取 `ports.size()`，`iterates` 仍取 `ports.size()-1`，
  与 `loop.cpp:93` 同一读法），还是你认为这条该由别的车道/别的卡处理？
- 另外：**重放要复制正向条件区与正向体里“更新状态”的那些节点**。walk 现有的是 `forward(ref)`（整函数走法下的原值复制），
  没有“把某个区整段复制进 draft 并重映射端口”的现成设施 —— 这一截是新写，不是加一个枚举值。

### 边界照旧

不推、不 merge、不动 `src/stdlib/autodiff/` 引擎本体、不改 `TapeLength`/`Recomputed` 的判据；
枝 `lane/c4-round-source`，树 `D:\Projects\Camel-wt\hdr-channel`，`git status` 空。