# P0-2：哪些控制边还在算链位

**状态：调研页，没有代码改动。作者 23:30 的裁定 ② 说「ctrl 边还算链位，随 P0-2 一起去掉」。本页回答三个问题，第三问之前不许动手。**

## 一、清单：`DraftEdgeKind::Ctrl` 在 `src/ include/` 共 10 个文件 17 行

| 位置 | 在做什么 | 判 |
| --- | --- | --- |
| `include/camel/runtime/draft.h:90` | 解码一个边种类 | 只是被读 |
| `include/camel/runtime/draft.h:694` | `appendInput(Ctrl, userId, inputId)` —— **写入** | 写入 |
| `src/compile/gir/builder_support.h:87` | 把某个种类映射成 `Ctrl` | **写入的源头之一** |
| `src/compile/gir/builder_support.h:200` | `unlink(Ctrl, from, to)` | 摘下 |
| `src/runtime/draft.cpp:998` | `decodeUsers(Ctrl, ctrlOutputsOf(source))` | 只是被读（传输） |
| `src/passes/opt/generic/gates.cpp:98` | GATE 的 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/generic.cpp:614` / `:667` | 分支/汇合处的 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/rewrite.cpp:419` | 重写时 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/rewrite.h:117` | `appendInput(Ctrl, user, pred)` | **写入（重写器自己造一条）** |
| `src/passes/test/fault.cpp:79/80/97/144` | 故障注入测试 | 测试 |

**判据的落点：真正"写入"控制边的只有三处 —— `builder_support.h:87`（构造）、`rewrite.h:117`（重写器）、`draft.h:694`（底层入口）。其余是摘下或读取。** 「谁在算链位」要落在前三处的调用者身上，而这需要一次调用图，本页没有做，**所以本页不声明"链位只剩几处"。**

## 二、合成边已经承担了什么，控制边还多承担什么

**这一格是我今天唯一说得清一半、说不清一半的地方，照实写：**

- **说得清的**：`edges.gate_has_no_control_input` 的诊断是 `GATE no longer has a control input"; N04 has 1 CTRL input(s) from N03` —— **控制边今天还在给 GATE 造输入口**，而模型（edges.cn.md 5.3）说那个口不该存在，等待应当由 sync 链承担。
- **说不清的**：**控制边在"顺序"之外还承担了什么**（例如它是否仍参与副作用的可达性、是否仍让某个 pass 认为两个节点有依赖）。**这一格是这一页的核心，而它今天没有读数支撑，所以按规矩：说不清就不许动。**

## 三、最小可落地的一步与它的读数

作者给的第一个读数是 ③：「解掉同步边只留标记，看它报在凭据那一条」。**照此，第一步不是删控制边，而是**先让"标记"单独承重**：

1. 停掉 `builder_support.h:87` 把种类映射成 `Ctrl` 的那一处（**只一处**），保留 `kGCNodeFlagSyncStatement` 标记；
2. **读数**：`poison:linear:edges.no_ctrl_edge_kind` 与 `edges.gate_has_no_control_input` 这两条 `known_fail` 是否变绿；以及**凭据那一条**（`pass.cpp` 读 `hasFlag(kGCNodeFlagSyncStatement)` 的地方）是否报出来 —— **作者说它应当报在凭据那一条，这是这一步的判据**；
3. **红了谁**：若 `gates.cpp:98` 或 `rewrite.h:117` 因缺边崩，那说明控制边仍在承重，**回到第二格，先补读数再动手。**

## 四、和 `pass.cpp` 那处的先后

作者裁定 ① 说 `pass.cpp` 里读标记那条凭据要删掉、改成读边。**它是同一族的第二处**：**标记是"我知道这里有个等待"的表达，边是"谁等谁"的表达。** 本页的判断是 **P0-2 先做**（边是数据的载体），**凭据那条随后**（它读的是标记，标记去掉后它自然无处可读）—— **这个先后是我的判断，不是作者的裁定，读写这页的人请当判断看。**

## 五、那两条 `known_fail` 与这一步是同一件事

`test/plans/feat/linear/effects_model_edges.plan.toml` 的两条标记（owner 已改成 `P0-2 (batch item 9, ruled 23:30)`）**会在 P0-2 落地时变绿，所以"谁做 P0-2"和"标记什么时候摘"是同一件事：标记在落地的同一笔里摘掉。**
## 第二格有读数了：4192 条红（2026-11-02 实测）

在一次性分支 `probe/ctrl-off` 上把 `src/compile/gir/builder_support.h` 的 `case LinkType::Ctrl:` 改成 `return runtime::DraftEdgeKind::Sync;`，构建后跑 `-Tier fast`：

**`total: 8447, pass: 4245, fail: 4192, skip: 10`** —— 按前缀分组前 10：`fault` 172、`attempt` 146、`stdlib` 144、`limit` 140、`rules` 129、`examples` 107、`tutorial` 94、`stream` 92、`entering` 77、`boundary64` 56。

**为什么映射成 `Sync` 而不是 `Data`**：终局是"等待由 sync 链承担"，`Ctrl→Sync` 直接探那个终局；`Ctrl→Data` 测的是把等待降级成数据依赖，红表不可解释，所以没选。

**为什么只跑 fast**：`fault`/`limit`/`stream`/`entering` 这些行为类都红了 —— **记账类只会红 canonical/gir 快照那一小撮** —— 所以 fast 已经足够说明"不是记账、是承重"，milestone 再加一遍不会改变结论。

**结论：P0-2 不是"删一条边"，是"给依赖控制边的 pass 换判据"。** 最小一步因此改成：**选一个族、换它的判据，读数是红数下降多少**（默认从红数最大的 `fault` 172 条起）。

**撤净证明**：`git status --porcelain` 空；`git branch --contains <探针提交>` 空；`Deleted branch probe/ctrl-off (was 6938afa67)`；车道 HEAD 与实验前逐字相同。
## 第一个样本：`fault` 族里的一条，为什么缺控制边就红

**为什么默认从 `fault` 起：因为它是最大的族（172），不是因为它最有代表性。** 172 条里估计不止一个根因，本页只给一条样本，**不把"最大"冒充"最有代表"**。

**样本**：`src/passes/test/fault.cpp` 里那个"摘掉效果"的 pass（`fault.cpp:109-160`）。它给一个 `:io/` 节点的"等待"挂一条 order 边，锚点是这样选的：

```
camel::runtime::gc_node_ref_t anchor = camel::runtime::kInvalidNodeRef;
if (!draft.ctrlInputsOf(id).empty()) {
    anchor = draft.ctrlInputsOf(id).front();      // fault.cpp:125-126
} else if (!draft.normInputsOf(id).empty()) {
    anchor = draft.normInputsOf(id).front();      // fault.cpp:127-129
}
```

**① 它依赖控制边做哪个判断**：**"这个效果等的那个前驱是谁"**。第一分支读 `ctrlInputsOf`；**控制链被写成 sync 边之后这个分支永远空**，于是它退到 `normInputsOf`，锚点从"链前驱"变成"第一个值输入"，order 边挂错节点，用例期望的那条诊断不再出现 ⇒ 红。

**② 还能靠什么判（两个候选）**
- **甲：读 `syncInputsOf`** —— `include/camel/runtime/draft.h:490` 已有这个访问器，**数据已经在图里，不缺**；改的是"先问谁"的顺序。
- **乙：读标记 `kGCNodeFlagSyncStatement`** —— `include/camel/runtime/graph.h:176` 定义、`src/compile/gir/builder_helpers.h:668` 打上，**数据也在**；但它是"我知道这里有等待"的布尔，回答不了"等的是谁"，**只能用来筛节点，不能单独当锚点**。

**③ 换判据之后的读数会是什么样**：**这一条从红变绿**是必然的；**同族一起变绿需要一次计数**——那个 pass 的用例清单本轮没有取，所以本页只写"这一条"，不写"这一族"。
## 换判据的收益率：177 → 40（≈77%）

改法（`1c97c4792`）：`src/passes/test/fault.cpp:125-129` 的锚点**先问 `syncInputsOf`**，`ctrlInputsOf` 那一支**删掉，不留兜底** —— 注释原文：**"a control link is written as a sync edge once P0-2 lands, and then the branch below would be empty for every node"**。留一个永不生效的分支比不留更糟：它让下一个读者以为还有两条路。

**读数**（一次性分支 `probe/ctrl-off2`，与改动分属两笔，探针那笔从未进车道）：
- 改前：`[FAIL] fault.*` **177 条**；
- 改后：`faults.plan.toml` **176 → pass 152 / fail 24**、`fault_preservation.plan.toml` **28 → pass 12 / fail 16** ⇒ **剩 40 条**。
- **`fault` 族里这个根因占 137/177**；**剩余 40 条另有根因。**

**P0-2 不是删一条边，是把"先问谁"问对。**

## 剩下的 40 条：40 不是 40 个用例，是 10 个 × 4 个后端

| 计划 | 用例（每个 × `nvm`/`fvm`/`jit`/`prl`） |
| --- | --- |
| `feat/consistency/faults.plan.toml` | `fault.div_zero_in_arm`、`fault.in_tail_round`、`fault.in_sync_call`、`fault.in_map_many`、`fault.in_untaken_arm`、`end.error_reaches_main` |
| `feat/opt/fault_preservation.plan.toml` | `rules_drop_division.algebra`、`rules_drop_division.opt`、`dce_chained_division.opt`、`inline_faulting_callee_order.opt` |

**这 10 条里几个根因，本页不判**：判据是"同诊断首行同根因"，而探针日志只记了用例名、没有记诊断首行。**要判就得再跑一次带输出的探针，本轮没跑。**