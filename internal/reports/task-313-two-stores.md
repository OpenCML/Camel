# 两个事实 store 的逐项对照

`task-313` 的 (d) 产出。用途：给"乙"那一格（runtime store 的 `Revalidated` 生产）立卡时，按项看清
两份读数各自来自哪里、差别归谁。**两个 store 不是替代关系**：framework store
（`camel::facts::Store`，`src/kernel/facts/`）是本卡改写的那一处；runtime store
（`camel::runtime::fact::FactStore`，`src/kernel/runtime/facts.cpp`）是 `test::facts_node` /
`context_->facts()` 实际回答的那一处。读数先问"是哪一个给的"。

证据位置一律写符号，不写行号。

## 一句话

同一个判断（"图变了、我读的东西没变"）在两个 store 里**有两个不同的名字**，而且**有一类判断只有
runtime store 做**（`Dependency::Cone` 重算 cone 相等）；framework store 的对应物是
`Reported` 的逐读记号，而它的**区域一侧还没有 owner 填版本**，所以区域事实仍走保守律。

## 对照表

| # | 项 | runtime store 报什么 | framework store 报什么 | 差异归谁 |
|---|---|---|---|---|
| 1 | 键 | `{KindId, GraphId, node}`，`GraphId` 直接取自图对象 | `{KindId, identity, node}`，identity 由 owner 经 `GraphAccess::identity` 给 | 同一个概念；framework 多一层"owner 声明"。乙不需要动 |
| 2 | 图版本戳 | `(GraphId, closureRevisionOf)` | `VersionMark{identity, graphRevision}` | 同概念。`closureRevision` 与 `revision` 的差别落在第 5 项 |
| 3 | 保守律类 | `Dependency::Anchors`：任一 revision 变 ⇒ `stale` | `Dependence::Anchors`：任一 revision 变 ⇒ `stale` | 一致，两边都有 |
| 4 | 节点级事实 | `Dependency::Node`：节点内容哈希未变 ⇒ **`valid`**（revision 变化对它不可见） | `Reported` + `readsCovered`：revision 变了、报读的节点版本未变 ⇒ **`revalidated`** | **两边都有、语义同、词不同**：`valid` 与 `revalidated` 是同一个判断的两种说法。证据：`test::facts_node` 印 `FACTS node_other node=valid`；framework store 自己的检查印 `Revalidated` |
| 5 | cone 类 | `Dependency::Cone`：revision 变了、cone 哈希重算相等 ⇒ **`revalidated`** | **没有 cone 概念**；最近似的是 `Reported` 的逐读记号 | **只有 runtime 会报 `Revalidated` 的一类** ⇒ 乙卡核心 |
| 6 | 区域事实 | cone 覆盖到区域（`core.controlRegions` 一类） | `GraphAccess::regionVersion` 钩子在，但**没有 owner 填** ⇒ 保守律 `stale` | 乙：缺的是 owner 侧的区域版本，不是 store 的规则 |
| 7 | 算子声明纪元 | `OperatorTable::declarationEpoch` | `ops::Registry::epoch()`（`Reads::declarations`） | 同概念、两个注册表。乙若要合并得先对齐注册表 |
| 8 | 目标哈希 | `Stamp::targetHash` | `Entry::target` | 一致 |
| 9 | 状态词表 | `Valid` / `Revalidated` / `Stale` / `Absent` | 同名四词 | 一致 |
| 10 | 一个版本结束时 | `FactStore::invalidateGraph`：**按 `GraphId` 整条抹掉**（`erase_if(entries_, key.graph == graph->id())`） | `Store::graphGone`：版本对象结束**不清**，重锚到同线还活着的版本；**整条线都不可达才清** | **语义不同，framework 更宽**：事实活过图替换。这一条是本卡带来的；乙若要对齐，是把它当既有行为还是当待统一项，由乙的卡定 |

## 哪几项 `Revalidated` 只有 runtime 会报

只有第 5 项：`camel::runtime::fact::Dependency::Cone` 且 cone 重算相等
（`FactStore::getValue` 的精确规则分支）。第 4 项在两边的**判断相同**，只是 runtime 用 `valid`
说、framework 用 `revalidated` 说——**不是 runtime 独有**。

## 哪几项两边都有但语义不同

- 第 4 项：判断相同、**词面不同**（`valid` vs `revalidated`）。要统一是"改词"不是"改判据"。
- 第 10 项：**判据不同**。同一个事件（一个图对象结束）在两个 store 里后果不同：runtime 整条抹掉，
  framework 只在整条线不可达时才清。

## 读数（本轮实测，`two_functions.cml`）

```
# 与旧店普查（framework store 对 runtime store，逐节点逐 kind 比"值的文本"）
FACTS_COMPARE kind=core.kinds graphs=3 nodes=12 same=48 diff=0 no_new=0 no_old=0 ok=1
FACTS_COMPARE kind=core.effects graphs=3 same=3 diff=0 no_old=0 no_context=0 no_new=0
FACTS_COMPARE kind=core.arrangeable graphs=3 nodes=12 same=3 diff=0 no_old=9 conversions=0
FACTS_COMPARE kind=core.controlRegions graphs=3 regions=0 enclosing_same=0 enclosing_diff=0 chain_same=0 chain_diff=0 within_pairs=0 within_same=0 within_diff=0 nodes_same=12 nodes_diff=0

# runtime store 在同一个提交之后的读数（`test::facts_node`）
FACTS node_revision new
FACTS node_version kept
FACTS node_other node=valid graph=stale
FACTS node_self node=stale
```

普查逐项 `diff=0`：`same=48` 是 12 个可读节点 × 四类 core kind（shape / instance / reorder / permissions）的逐项计数，四类在两店之间**没有一处不同**。它比的是**值**，不是状态词——状态词的差别见上表第 4、5、6 项。

## 推不出来的部分

- "runtime store 的 `Revalidated` 生产搬到 framework store 之后，哪些既有读数会跟着变"——这需要
  一次带 runtime 侧 `counts().revalidated` 计数的实测，而那个计数现在没有打印入口；不想靠读代码
  推成结论。乙的卡若要求这一行，先加一行计数输出，再谈对齐。
- `core.arrangeable` 的 `no_old=9`：那是**旧店没有、新店有**的条目，属于新层的覆盖差异，不是状态词
  差异；本表不把它算作"两边语义不同"。