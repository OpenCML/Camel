# `sync` 块里「谁算语句」：建图与 tiles 用的是两个定义

日期：2026-10-30　现状分支：`lane-eff-rule`

## 现象

两个子系统都在判「这个节点是不是一条语句」，判据不一样：

- **建图**（`src/compile/gir/builder_operators.cpp:4317`、`src/compile/gir/builder_control.cpp:3624`
  与 `:4027`）：`sync` 块里**每个调用**都走 `linkSyncStatement`，也就是每个调用（**包括写在表达式
  里的嵌套调用**）都是「块里的语句」，据此给它一个链位。
- **tiles 区域资格**（`modules/tiles/src/graph_regions.cpp:106`）：

  ```cpp
  const bool ordered = !graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty() ||
                       !graph.syncPredsOf(id).empty() || !graph.uses().sync(id).empty() ||
                       inChain(chain, id);
  ```

  `ordered && !device` 返回 `"control_edge"`，把节点排除出 GPU 区域。即
  **「链可达」= 「这是一条语句」= 老实待在 host**。

链一旦完整（`sync` 块里带效应/可能出故障的语句都挂上链），`main` 里的 `reshape` / `chain` / `sum`
这些**表达式里的值**全被判成语句，退出 GPU 区域——`TILES_OUTLINED` 从 base 的 **10** 掉到 **5**，
`lane-eff-rule` 全量 fast 是 `total 7804 / fail 70`，其中 tiles 区域计数 22 条。

**这不是 tiles 期望写错**：`graph_regions.cpp:101-105` 的注释在 base 上就写着「`sync` 块的顺序
就是它的链，链可达的节点和 ctrl 点名的节点一样是语句」。两个定义在 base 上一致（链是稀疏的），
链一完整就分开。

补充实测：把 `nodeMayFault` 收窄到「显式声明了故障」，`TILES_OUTLINED` 只从 5 涨到 6——
10→5 里只有 1 个区域是「可能出故障」那半吃掉的，其余 4 个是 `isEffectNode` 的效应半吃掉的，
所以这个问题收窄故障判据修不好，落点只能是「谁算语句」。

## 为什么不能自己定

设计原则 1 说「同步链只连带效应的语句」，5.3 说「一个块的语句是一条链」，但**两处都没有说
「语句」在建图时以什么为边界**：是块里直接写的那一条，还是块里每个调用（含嵌套），还是
链可达的每个节点。这决定 `sync` 块里哪些值会被判成语句、要占链位，是链规则的语义边界，
不该由改 splice 或改 tiles 的人顺手定；两侧各改一边都会把不一致留在另一边。

## 可执行的选项

**选项 A（改建图）**：把 `sync` 块的「语句」限成块里直接写的那一条——`linkSyncStatement` 的三个
调用点（`builder_operators.cpp:4317`、`builder_control.cpp:3624`、`:4027`）只在被调调用的结果不被
同一块的另一个调用作为参数读走时写边；嵌套调用按值处理。派工影响：tiles 22 条回到 10；
`lane-eff-rule` 里因链变长而动的快照需要重录/复核；内联那边「链的段」会变少，
`lane-eff` 的 splice 判据（`bodyHeads == 1 && bodyTails == 1` 与其前置的链接步骤）要跟着复核一次。

**选项 B（改 tiles）**：`graph_regions.cpp:106` 的 `ordered` 去掉 `inChain(chain, id)` 一项，
改为「链上有 SYNC 前驱/后继**且**该节点带外部效应或可能出故障」才算语句；`device` 分支同步调整
（`:109-117` 两处）。派工影响：只动 tiles 一个模块，22 条回位；但同样「读链当语句」的消费者
（`spliceChainAnchor` 注释里点名的 ONNX exporter 那条路）仍是旧读法，不一致留在那里。

**默认建议**：**选 A**。它让「语句」只有一个定义，与 1、5.3 的字面一致，也不必再维护两套读法；
代价是要复核内联侧的链段判据，而那部分现在有守卫（`nn_grad_check.spliced_body_chain_is_one_chain`）
兜着。在定义定下来之前，不要把 22 条 tiles 期望改成 5——那会把一个定义冲突固化成记录。