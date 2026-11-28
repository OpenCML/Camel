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