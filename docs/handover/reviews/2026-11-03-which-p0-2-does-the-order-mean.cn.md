# 排序里那条 `P0-2` 指 A 还是 B？A 的两处是不是同一个条件（逐分支量：不是）

**问：排序里的 `P0-2` 指 A（建图/封口谓词两件）还是 B（改写/内联等待两件）？若指 A：`src/compile/gir/builder_helpers.h#nodeTakesAnOrder` 与 `src/runtime/graph_edges.cpp#isChainStatement` 是**两个不同条件**（五个分支答案不同），合成以哪一侧为准？默认建议：指 A；不合成这两张表，只让写同步链的那一处改问 seal 的谓词；B 的第二件不再做（已有否证读数）。**

## 1. 同一个词，两条线上指的不是同一对东西

| 说法 | 出处 | 两件分别是什么 |
| --- | --- | --- |
| **A** | `docs/handover/reviews/questions-2026-10-29.md:1896`（`origin/develop`） | ① `builder_helpers.h#linkSyncStatement` 建链两头都问（`from` 侧新增 `chainTailBehind`；今天 `git grep chainTailBehind -- src include` = **0**）② `builder_helpers.h#nodeTakesAnOrder` 与 `graph_edges.cpp#isChainStatement`「同一个条件的两份拷贝」合成一处 |
| **A′** | 同文件 `:1954` | 「两处是链条件的两份拷贝合成一份」——只剩一件，与 `:1896` 的两件对不上；读数也不同（`:1896` `fail 3835 → 3497`，`:1954` `3757` 红） |
| **B** | `origin/fsh-dev` 同文件 `:2002`、`:2138` | ① 内联插 `GATE`（`src/runtime/draft_inline.cpp#spliceChainAnchor`，`a975a0591` 起已在树上）② 改写不把链位交给纯值（`f8e4ce438` 已否证：`873/871/0 → 873/869/2`，全量 `8441 pass / 80 fail`，66 新红） |

`origin/develop` 上「链位不交给纯值」**0 命中**：B 那套定义只活在 `origin/fsh-dev`，作者排序 ⑤ 倒序 ⑥ `P0-2` 两处改写（`:2201`）也只在 fsh-dev。**所以「一直没人做」有一部分原因是 develop 上没有 B 的定义。**

## 2. A 的两处不是同一个条件（逐分支对照）

两个谓词的全文：`src/compile/gir/builder_helpers.h#nodeTakesAnOrder`（builder，吃 `node_handle_t`，操作符按 target 的 `uri` 查）与 `src/runtime/graph_edges.cpp#EdgeChecker`（seal，吃 `gc_node_ref_t`，操作符按 `GCOperBody::oper` 查）。逐分支：

| 节点 kind | builder `nodeTakesAnOrder` | seal `isChainStatement` | 同否 |
| --- | --- | --- | --- |
| `Data`/`Port`/`Copy`/`Fill`/`Accs`/`Join` | false（自由调度） | false | 同 |
| `Brch`/`Loop` 且拥有区域 | true | true | 同 |
| `Brch`/`Loop` 不拥有区域 | false（走自由调度那一支） | true | **不同** |
| `Call` | true（默认支：不自由调度） | true | 同 |
| `Oper` 纯 + 可 fold + 不会故障 | false | false | 同 |
| `Oper` 纯 + 可 fold + 会故障 | true | true | 同 |
| `Oper` 有副作用 | true（`mayBeRecomputed` 假） | true（`!isPure`） | 同 |
| `Oper` 纯但 `declareUnfoldable`（`src/core/operator_table.cpp#OperatorTable::declareUnfoldable`） | true | false | **不同** |
| `Oper` 未声明 / `kNoOper` | false | false | 同 |
| `Cast` 会故障 | true（`mayFault(cast)`） | false（默认支） | **不同** |
| `Cast` 不会故障 | false | false | 同 |
| `Func` | true | false | **不同**（`:1896` 已量到这一条） |
| `Gate` | true | false | **不同** |

⇒ 两件问的不是一件事：builder 那件答「**这个节点上能不能落一条顺序边**」（`edges.cn.md` 4.6），seal 那件答「**这个节点是不是链上语句**」（`edges.cn.md` 1、4.6；04:50/05:30 裁定：`Call` 算，`Func`/`Join` 不算）。`builder_helpers.h:522-524` 的注释写「the seal check asks the same question through the same classification, so an edge written through this one cannot be refused」——**这句被上表证伪**：`Func`、`Gate`、会故障的 `Cast` 都是建边侧放行、封口侧拒。

**数量也对不上：今天不是两份，是三份，而且其中一份没人调用。** `git grep -n "isChainStatement" -- src include` 只有三行：`graph_edges.cpp:193`（定义）、`:223`（第二份的定义）、`:318`（调用）。也就是说 `isChainStatement`（193）**0 个调用点**，封口检查真正用的是 `isChainStatementNode`（223，只喂 `:318` 的那个 census）。这两份自己就已经不一致：`body == nullptr || body->oper == kNoOper` 那一支，`isChainStatement` 返 `false`，`isChainStatementNode` 返 `true`。builder 那一侧的 `nodeTakesAnOrder` 有 **9** 个调用点（`builder_expr.cpp:596/616/1735/1736`、`builder_operators.cpp:4343/4344`、`builder_resources.cpp:639/676`、`builder_helpers.h:604`）。

## 3. 为什么不能直接"合成一处"

`nodeTakesAnOrder` 今天同时服务两族调用：写**顺序边**（`builder_operators.cpp:4343`、`builder_resources.cpp:639`、`:676`）与写**同步链**（`chainLinkSaysSomething`，`builder_helpers.h:604`）。把两张表合成一处，等于替顺序边那一族决定"顺序边只落在链上语句上"——那是语义改动，不是合并拷贝；而判据只允许行为不变。

## 4. 要作者答的一句

**「排序里那条 `P0-2` 指 A 还是 B？若指 A：`nodeTakesAnOrder` 与 `isChainStatement` 是五个分支答案不同的两个条件，合成以哪一侧为准？」**

我方默认：**指 A**（`origin/develop` 上只有 A 的说法）。**不合并这两张表**——保留 `nodeTakesAnOrder` 回答「能落顺序边」那一问，只把写同步链的 `chainLinkSaysSomething` 改问 seal 那套（`Call` 算；`Func`/`Gate`/会故障的 `Cast` 不算），判据是 `feat/linear` 与 `feat/compile` 的用例**逐名不变**；若逐名有变，贴名再问。B 的第二件不再做：`f8e4ce438` 的 80 fail / 66 新红说明「占据链位」≠「纯与不纯」。