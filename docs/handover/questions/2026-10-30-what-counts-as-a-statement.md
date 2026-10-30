# 问：`sync` 块里「谁算语句」——建图与 tiles 用的是两个定义

日期：2026-10-30　分支：`lane-eff-rule`（本条规则在这支上，不在将合入的 `lane-eff` 上）

## 现象

两个子系统都在判「这个节点是不是一条语句」，判据不一样：

- **建图**（`src/compile/gir/builder_operators.cpp:4317`、`builder_control.cpp:3624/4027`）：
  `sync` 块里**每个调用**都走 `linkSyncStatement`，也就是每个调用（**包括写在表达式里的嵌套调用**）
  都是「块里的语句」，据此给它一个链位。
- **tiles 区域资格**（`modules/tiles/src/graph_regions.cpp:106`）：

  ```cpp
  const bool ordered = !graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty() ||
                       !graph.syncPredsOf(id).empty() || !graph.uses().sync(id).empty() ||
                       inChain(chain, id);
  ```

  `ordered && !device` 就返回 `"control_edge"`，把节点排除出 GPU 区域。也就是
  **「链可达」= 「这是一条语句」= 老实待在 host**。

于是：链一旦完整（上一轮那条规则把「带效应/可能出故障的语句」都挂上链），
`main` 里的 `reshape` / `chain` / `sum` 这些**表达式里的值**全被判成语句，
退出 GPU 区域 —— `TILES_OUTLINED` 从 base 的 **10** 掉到 **5**。

**这不是 tiles 的期望写错**（它的注释 `graph_regions.cpp:101-105` 明写
「`sync` 块的顺序就是它的链，链可达的节点和 ctrl 点名的节点一样是语句」，
这条注释在 base 上就有），**是两个子系统对「语句」的定义不一致**：

- 建图这边：`sync` 块里的**每个调用**都是语句；
- tiles 这边：**链上的每个节点**都是语句。

两者一致时（base，链是稀疏的）不出问题；链一完整，第二个定义就把第一个定义的外延
放大到了表达式里的值。

## 两条路的代价

**路 A：改建图——嵌套调用不算语句，只有块里直接写的那一条（`let`、被丢弃结果的调用、
`return`）算。**

- 得到：两个定义重新对上；`tiles` 的 `TILES_OUTLINED` 回 10；与 1「同步链只连带效应的语句」、
  5.3「一个块的语句是一条链」字面上一致。
- 代价：要动 `linkSyncStatement` 的调用点（三处）并给「块里直接的语句」一个新判据；
  内联那边也要跟着改——我这次修的 splice 是按「链的段」接的，语句定义一变，段会变多变少，
  `bodyHeads == 1 && bodyTails == 1` 这条判据和「段与段之间按数据序接」的接法都要重新核。

**路 B：改 tiles——链可达不再单独算语句，要求「链上**且**有外部效应或可能出故障」。**

- 得到：只动 tiles 一个模块，改动小；22 条 tiles 读数一次回位。
- 代价：tiles 的 `device` 分支和 `#gpu` 作用域的语义要重新核（`graph_regions.cpp:109-117`
  两处 `ordered && !device`）；更要紧的是**同样的「读链当语句」的消费者不止 tiles 一个**
  —— ONNX exporter 读 gate 的那条路（`spliceChainAnchor` 的注释里点名的就是它）也在用
  「链上有等待」当作「这个值只在语句跑过之后才存在」。只改 tiles 等于把不一致留在别处。

**记录代价（两条路都一样）**：`TILES_OUTLINED` 那 22 条期望要么回到 10（A 或 B 落地之后），
要么改成 5（若裁定「链完整是对的，tiles 的语句读法也是对的」）。「不许改期望」这条约束
使 A、B 成为唯一出口；**在定义定下来之前，我不改那 22 条期望**。

## 默认建议

先把「语句」定成**一个**定义：`sync` 块的语句是**块里直接写的那一条**，表达式里的调用是值
（这条路与 1、5.3 的字面一致，且不必再维护两个定义）。
定下来之后再让 tiles 只读这一个定义，而不是自己再从「链可达」推一遍。
在此之前不要把 `TILES_OUTLINED` 的期望改成 5——那会把一个定义冲突固化成记录。

补充实测（另一页的对照实验，用来分辨两条路的份额）：把 `nodeMayFault` 收窄到「显式声明了故障」，
`TILES_OUTLINED` 只从 5 涨到 6，说明 10→5 里只有 1 个区域是「可能出故障」那半吃掉的，
其余 4 个是 `isEffectNode` 的效应半吃掉的 —— 也就是说**只收窄故障判据修不好这个问题**，
必须落在「谁算语句」上。