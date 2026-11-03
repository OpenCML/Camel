# P0-2：哪些控制边还在算链位

**状态：调研页，没有代码改动。作者 23:30 的裁定 ② 说「ctrl 边还算链位，随 P0-2 一起去掉」。本页回答三个问题，第三问之前不许动手。**

## 一、清单：`DraftEdgeKind::Ctrl` 在 `src/ include/` 共 10 个文件 17 行

| 位置 | 在做什么 | 判 |
| --- | --- | --- |
| `include/camel/runtime/draft.h#edgeSlotOf` | 解码一个边种类 | 只是被读 |
| `include/camel/runtime/draft.h#appendInput` | `appendInput(Ctrl, userId, inputId)` —— **写入** | 写入 |
| `src/compile/gir/builder_support.h#toDraftEdgeKind` | 把某个种类映射成 `Ctrl` | **写入的源头之一** |
| `src/compile/gir/builder_support.h#linkNodes` | `unlink(Ctrl, from, to)` | 摘下 |
| `src/runtime/draft.cpp#GraphDraft::decode` | `decodeUsers(Ctrl, ctrlOutputsOf(source))` | 只是被读（传输） |
| `src/passes/opt/generic/gates.cpp#bypassValueGate` | GATE 的 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/generic.cpp#pruneBranch` / `:667` | 分支/汇合处的 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/rewrite.cpp#replaceNode` | 重写时 `unlinkInput(Ctrl, …)` | 摘下 |
| `src/passes/opt/generic/rewrite.h#handOverSyncWait` | `appendInput(Ctrl, user, pred)` | **写入（重写器自己造一条）** |
| `src/passes/test/fault.cpp#LeakBranchArmTestPass::apply`（以及同文件的 ShareBranchArmTestPass::apply、OrderOnlyEffectTestPass::apply） | 故障注入测试 | 测试 |

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
- **甲：读 `syncInputsOf`** —— `include/camel/runtime/draft.h#syncInputsOf` 已有这个访问器，**数据已经在图里，不缺**；改的是"先问谁"的顺序。
- **乙：读标记 `kGCNodeFlagSyncStatement`** —— `include/camel/runtime/graph.h#GCNodeFlag` 定义、`src/compile/gir/builder_helpers.h#isFailCall` 打上，**数据也在**；但它是"我知道这里有等待"的布尔，回答不了"等的是谁"，**只能用来筛节点，不能单独当锚点**。

**③ 换判据之后的读数会是什么样**：**这一条从红变绿**是必然的；**同族一起变绿需要一次计数**——那个 pass 的用例清单本轮没有取，所以本页只写"这一条"，不写"这一族"。
## 换判据的收益率：177 → 40（≈77%）

改法（`1c97c4792`）：`src/passes/test/fault.cpp#OrderOnlyEffectTestPass::apply` 的锚点**先问 `syncInputsOf`**，`ctrlInputsOf` 那一支**删掉，不留兜底** —— 注释原文：**"a control link is written as a sync edge once P0-2 lands, and then the branch below would be empty for every node"**。留一个永不生效的分支比不留更糟：它让下一个读者以为还有两条路。

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
## 那 40 条的分组：一个诊断，不是两组

带输出的探针（`-Jobs 1`，`probe/ctrl-off3`，已撤净）把每一条的诊断首行取到了。**40 条全部是同一句**：

```
[INFO] stdout does not start with "<N>\n"
[INFO] expected diagnostic RuntimeDiag::DivisionByZero
```

`<N>` 分别是 `1`（`fault.div_zero_in_arm`）、`1437`（`fault.in_tail_round`）、`5`（`fault.in_sync_call`）、`before`（优化器那一组）等 —— **同一句话，变量不同位**。

⇒ **按"同诊断首行同根因"，这 40 条是 1 个根因**：**除法从来没有被执行（或没有到达），所以 `DivisionByZero` 没有发出；前向值本身也没打出来。** 我在上一轮按用例名猜"两组（抛错次序 vs 优化器纯度）"，**诊断否掉了这个猜测**——名字像两件事，读数是一件事。

**因此这一族不是"袋子里装的不是同一样东西"**：它装的是一样东西，只是从两个计划的入口进来。
## 剩下 40 条的机制：真正挡住它的是边不变量，不是某个 pass 的读法

**三问的答案（读代码 + 探针跑出的 stdout 原文）：**

**① `DivisionByZero` 在哪一层发出**：**在执行的算子层** —— `src/builtin/executors/builtin_semantics.cpp#declareBuiltinOperatorSemantics` 给 `:op/div_*` 声明 `FaultSpec{FaultKind::DivisionByZero, 1}`（下标 1 = 除数），**除法真的执行时才发。**

**② 那一层看的是什么次序**：**它根本轮不到执行。** 探针跑出的 stdout 原文（`test/results/log/20261101_230542_745449/fail/.../stdout.txt`）：

```
camel: internal error: Runtime graph 'pick' breaks its edge invariants (2 violation(s)):
  graph 'pick': sync edge 5 -> 6 ends on a node no `sync` block wrote (kind 11, flags 0, data type 'int')
```

⇒ **把控制链写成 sync 边之后，`sync` 边落在了普通 oper 节点上，图的不变量直接否掉这张图**（"ends on a node no `sync` block wrote"），**程序在开跑前就中止**。`fault.in_sync_call` 的原文更直接：`A statement of a sync block already follows node 5, so it cannot follow node 7 as well.`

**③ `<N>` 那四个值说明什么**：**它们一个也没出现，因为 stdout 里是编译器内部错误文本，不是程序的输出** ⇒ **"程序根本没跑完"，不是"跑完但答案错"。** 优化器那组的 `before` 同理。

**⇒ 这就是 P0-2 真正的墙**：**不是"哪个 pass 读控制边"，而是"sync 边必须落在一个 `sync` block 写过的节点上"这条不变量。** P0-2 要做的是**让链位由 sync 承担的同时，把这条不变量该说的东西说对**（哪些节点允许是 sync 边的终点），**而不是把控制边悄悄换成 sync 边**。

**这一句是机制，不是我上一轮那个症状**（"除法没执行"是症状，上面才是机制）。
## 更正：4192 是上界，不是终局读数

**上一节那个 `Ctrl→Sync` 的 4192 条红，只证明"悄悄把控制边换成 sync 边会撞墙"，不证明"P0-2 的终局会红 4192 条"** —— 终局还包括"把这条不变量该说的东西说对"。

**⇒ 4192 的正确读法：它是"只换边、不改不变量"的读数，是 P0-2 的上界，不是它的终局读数。** 把实验读数当目标读数用过一次（`remat`），这一页不再犯。

## 那条不变量本身（原文）

**检查处**：`src/runtime/graph_edges.cpp#EdgeChecker`（`bad(...)` 造每条 violation），**汇总打印在 `:710`**：`Runtime graph '{}' breaks its edge invariants ({} violation(s)):`。

**它要求什么**（判据原文，`:226-241`）：

```cpp
if ((user->flags & kGCNodeFlagSyncStatement) == 0 && user->kind != GCNodeKind::Gate) {
    bad("ends on a node no `sync` block wrote ...");
}
if ((pred->flags & kGCNodeFlagSyncStatement) == 0) {
    bad("starts at a node no `sync` block wrote ...");
}
```

⇒ **一条 sync 边的两端都必须带 `kGCNodeFlagSyncStatement`；终点额外允许是 `Gate`。**

**为什么这条要求曾经是对的**：sync 边原本只由 `sync` 块产生，而 `sync` 块写过的节点由 `src/compile/gir/builder_helpers.h#isFailCall` 打上这个 flag —— **"sync 边"与"sync 块写过的节点"是同一件东西的两种说法。**

**P0-2 之后它该变成什么**：谓词要回答的是 **"这条链的终点是不是某个等待的承担者"**，而不是"它是不是 sync 块写的"。

**两条候选**：
- **甲：放宽不变量** —— 终点条件改成"带等待标记 **或** 是 Gate"。**代价**：判据从"谁写的"变成"谁承担的"，需要一个新的谓词；**风险**：放宽后旧图里真正非法的边也过关，`breaks its edge invariants` 从此少报。
- **乙：建图时给等待链终点打上 sync flag** —— 不加新谓词，只让 `builder_helpers.h:668` 的标记覆盖等待链的全部终点。**代价**：建图阶段要知道"等待链的终点"，而那是 P0-2 的核心知识；**风险**：标记扩散，`kGCNodeFlagSyncStatement` 从"sync 块写的"退化成"大概有等待"，标记的语义被稀释。

**本页不改它**：这条不变量判的是"图能不能成立"，改错会让整棵树在开跑前崩——今天的探针已经演示过。