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