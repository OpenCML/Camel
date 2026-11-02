# 一问：新增一个**必须收 `GraphDraft`** 的函数（头里声明 + 源里定义），算不算棘轮要拦的"新引用"？

> 状态：**等作者裁决。已按"算、且一次记 2 处并在提交信息里点名"落地（默认），可撤回。**
> 相关：`docs/handover/reviews/graph-store-step2-b-ledger.cn.md` §61（这一处 2 的记账与 A/B）；
> `questions-2026-10-31-graph-draft-zero-or-the-floor.md`（天花板的另一问）；
> `test/cases/runtime/graph_draft_ledger/baseline.json`（棘轮的基线）。

## 发生了什么（数）

commit `be25353a8`（A10：Camel 侧 `opt::dce` 与 `std::opt::dce` 对齐）加了 **2 处** `GraphDraft`：

| 文件 | 那一行 | 为什么非收不可 |
| --- | --- | --- |
| `include/camel/runtime/draft.h` | `bool nodeMayFault(const GraphDraft &draft, gc_node_ref_t id, bool byShape = false);` | 声明：`modules/graph` 要问"这个 draft 节点会不会出错" |
| `src/runtime/graph.cpp` | 同签名的定义 | 定义：那 30 行的形状走查只写这一份 |

棘轮跟着从 `references` **304 → 306**，`baseline.json` 用 `check.mjs --record --force` 重记
（`--record` 拒绝写一个涨了的数，`--force` 是那页注释里写明的"响亮覆盖"，diff 会让复核看见）。
`check.mjs` 与 `runtime.graph_draft_ledger.plan.toml` **一行没动**：计划里没有容差，判据仍是
`current <= baseline`。所以"今天为什么允许 306"的答案只有一条 —— **基线被这一笔抬了 2**，
不是判据被放宽。

## 为什么这 2 处躲不开（三条都试过）

- **收 `GraphDraft` 的函数没法换成不收**：C++ 那一遍问的是 `nodeIsFreelySchedulable(draft, id, true)`，
  它是**合取**（算子可重算 **且** 不会出错），一个 `bool` 的假分不出"不可重算"（规则的值、`fact`）
  与"会出错"两种情况；而 `may_fault` 是要单独回答、单独写进 `stdlib/graph.cml` 的问题。
- **把判据抄一份进 `modules/graph`**：那要 `read.cpp` 自己走一遍 `header()` / `shapesAreFixed` 才是
  `byShape` 的同一句。A10 这个缺陷本身就是"两处各写一遍、然后说法不一致"长出来的（`is_pure` 被当成
  `mayFault` 用），在被判 red 的改写任务里再抄一份，是把下一个 A10 提前写进来。
- **塞进已有声明**：`nodeIsFreelySchedulable` 加个 `bool *mayFaultOut = nullptr` 出参能躲开计数，但那是
  给一个是非查询挂出参 —— 躲的是棘轮，不是问题。

## 两句话的选项，要作者选一句

- **① 认这 2 处**（默认，已落地）：判据不变，基线 304 → 306，`§61` 记下为什么、A/B 量过、并写明
  `GraphDraft` 删掉那天它和其余 304 处一起消失。规矩随之写成一句：**同一概念"头里声明 + 源里定义"
  一次记 2 处，必须在提交信息里点名**（本笔已点名）。
- **② 不认**：那 `nodeMayFault` 就得给出一个不收 `GraphDraft` 的形状 —— 例如把形状走查的入参从
  draft 换成 `(kind, oper, outType, inTypes…)` 的一小包，由调用方（`modules/graph` 已有 `NodeView`）
  填。代价：`modules/graph` 里出现那份"本来只该有一份"的走查；或者把它做成 `GraphDraft` 的内联成员
  （0 处新增 `GraphDraft`，但类型自己的头再长一个方法）。

**要作者答的一句**：这一笔的 +2 是认（①，已在树上）、还是必须退回让 `nodeMayFault` 换签名（②）？

**判据**：**棘轮拦的是"又有一处直连 `GraphDraft` 的居民"，还是"又有一处在给 `GraphDraft` 重新起名"？
前者答"是的，所以我记账并说明为什么降不掉"；后者答"不算，声明+定义是同一个概念的两半"。**