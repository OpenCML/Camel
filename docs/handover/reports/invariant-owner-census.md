# 不变式的家谱：全仓普查（不是闸门）

## 怎么跑、什么算通过

```
npm run check:invariant-owner                      # 本分支自 origin/develop 的合并基点以来新增的注释
node scripts/check-invariant-owner.mjs --since <sha>   # 某一笔自己的改动
node scripts/check-invariant-owner.mjs --census        # 全仓普查表（就是下面这张）
```

**它只打印、退出码 0，不进闸门、不拦提交。** 「通过」的定义是：这段改动里凡是新增了「某某必须……」这类规则
注释的文件，**其中至少有一条明说拥有者**——判据句形如 *the owner of this rule is the finished graph*，
词表只有完工的图 / 草稿 / 编码器 三加一（安全网只作兜底）。**只提到 draft、graph 这些词不算点名**，这是
这条检查存在的理由。`--census` 用来回答「我们的不变式都住在哪」，不是用来判红绿的。

**一句话**：一条判据只有在对**完工的节点**问得出来的地方可查；写在「还在写」的地方，问到的只是半成品
的答案。今晚那条顺序边为这句话付了一夜：四处写者守卫与一个 choke point 全部无效，能用的那处过滤在
planDraftPayload 里——**拥有者是完工的图，不是写边的地方**。

这张表是把那句话拿去照全仓的结果，用 `scripts/check-invariant-owner.mjs --census` 生成：
每一条「某某必须……」的注释，它有没有点名拥有者，若没有，按今天的事实它应该属于哪一类。
**词表就三加一，不再扩**：完工的图 / 草稿 / 编码器 / 安全网（只作兜底）。

**43 invariant comments in 7 files; 26 name no owner**

## 表

| file:line | first sentence | owner named | should belong to |
|---|---|---|---|
| src/runtime/graph_edges.cpp:5 | * The edge invariants of an encoded graph, checked at every commit (docs/design/graph-store 5.4). | finished graph | finished graph (the seal check) |
| src/runtime/graph_edges.cpp:254 | /// The other half of 5.3, and the one a splice can break: a block's statements are one chain, /// so one stat | finished graph | finished graph (the seal check) |
| src/runtime/graph_edges.cpp:364 | /// The control region an order edge would cross, 0 when it crosses none. | finished graph | finished graph (the seal check) |
| src/runtime/graph_edges.cpp:439 | /// The rule of 4.6's third clause, read off the shared classification so that the seal check /// and the pass | seal check (backstop only) | finished graph (the seal check) |
| src/runtime/graph_edges.cpp:444 | // Whether a conversion can fail depends on the types it converts between, and a pass that // refines the type | finished graph | finished graph (the seal check) |
| src/runtime/draft.cpp:1352 | // A predecessor of the node this chain closes over that belongs to another block cannot become // the success | **none** | draft |
| src/runtime/draft.cpp:1375 | A sync edge joins two statements of one block, so a predecessor of the node this chain // closes over that bel | **none** | draft |
| src/runtime/draft.cpp:1499 | // What ordered the replaced node orders its replacer (docs/design/effects/edges.cn.md 4). | **none** | draft |
| src/runtime/draft.cpp:1511 | // What the replaced node ordered is ordered by its replacer. | seal check (backstop only) | draft |
| src/runtime/draft.cpp:1822 | // Both are the statement's own place in the chain: a node already in one chain cannot be // put into another, | **none** | draft |
| src/runtime/draft.cpp:2065 | // A value of `never` never exists (design principles 3.9): the expression that has this type // gives no valu | **none** | draft |
| src/runtime/graph.cpp:874 | // A GATE forwards its (last) norm input: FastVM reads the producer, NodeVM reads the gate's // slot, so the s | finished graph | encoder (the payload plan) |
| src/runtime/draft_session.cpp:441 | The type of the draft's static area. | draft | draft |
| src/runtime/draft_session.cpp:481 | Encodes `draft` into `target` (`bytes` reserved), rewriting every graph reference through // `rewritten`, whic | draft | draft |
| src/runtime/draft_session.cpp:654 | * The commit encoded `after` from the draft of `before`, and they have the same content: the * same records, e | draft | draft |
| src/runtime/draft_session.cpp:792 | ifndef NDEBUG // --------------------------------------------------------------------------------------------- | draft | draft |
| src/runtime/draft_session.cpp:935 | // An assertion, not an exception: the rewrite kernels catch std::exception and turn it // into an error value | **none** | draft |
| src/runtime/draft_session.cpp:1013 | // The session may replace the context's program only while the program it rewrites is one of // that program' | **none** | draft |
| src/runtime/draft_session.cpp:1142 | // The objects the collector moved, old slot to new: a specialization is cached under the // value it binds, a | **none** | draft |
| src/runtime/draft_session.cpp:1207 | // A type-only binding names a type (permanent, never moved); | **none** | draft |
| src/runtime/draft_session.cpp:1411 | // A conflict is a lost race for the context's program, and only a session whose commit could // replace that  | **none** | draft |
| src/runtime/draft_session.cpp:1427 | // The graphs the session read or wrote: it reads a graph when it decodes it (a draft) and // writes the ones  | draft | draft |
| src/runtime/draft_session.cpp:1500 | // The graphs of the session are the versions this session read; | **none** | draft |
| src/runtime/draft_session.cpp:1554 | ifndef NDEBUG // Before the draft is destroyed: the Debug record must never name a freed draft. | draft | draft |
| src/runtime/draft_session.cpp:1567 | // The program after the commit holds every graph of the closure: the version it has now, or // the one this c | **none** | draft |
| src/runtime/draft_inline.cpp:87 | The parameter type an instance of a declaration gets from one argument, as design principles /// 5.5 states it | **none** | draft |
| src/runtime/draft_inline.cpp:791 | // The order is part of the key, so both sides must place two bindings of one type the same // way: a type is  | **none** | draft |
| src/runtime/draft_inline.cpp:929 | // Ports are parameter placeholders. | draft | draft |
| src/runtime/draft_inline.cpp:931 | // So must the control uses. | **none** | draft |
| src/runtime/draft_inline.cpp:1177 | // A bound argument may have been a constant seen through a GATE that waits (the result of an // inlined sync  | **none** | draft |
| src/runtime/draft_inline.cpp:1309 | // Every capture must be filled here; | **none** | draft |
| src/runtime/draft_inline.cpp:1360 | A node whose work is inside the regions it owns (the arms of a branch, the parts of a loop): /// it carries no | **none** | draft |
| src/runtime/draft_inline.cpp:1368 | The nodes a spliced body's order edges have to cover (docs/design/effects/edges.cn.md 5.7): the /// effect and | seal check (backstop only) | draft |
| src/runtime/draft_inline.cpp:1410 | 5.7's extra constraint, written beside the splice: the chain the call stood in runs /// `before -> body -> aft | **none** | draft |
| src/runtime/draft_inline.cpp:1625 | // The chain end that grows from `pred`, walked among the statements of its block and never // through `n` its | **none** | draft |
| src/runtime/draft_inline.cpp:1822 | // An argument the body never reads still runs. | **none** | draft |
| src/runtime/draft_inline.cpp:1856 | // What the body waits for: the call's predecessors, and the effects of arguments it never // read. | **none** | draft |
| src/runtime/draft_inline.cpp:1902 | // Callee ports are now pure placeholders. | **none** | draft |
| src/runtime/draft_inline.cpp:1927 | // A gate waits on the predecessors themselves; | **none** | draft |
| src/runtime/draft_inline.cpp:2120 | // The chain the call stood in has to run through the body once the body is spliced in: its // statements are  | **none** | draft |
| src/runtime/draft_inline.cpp:2254 | // The statements written directly after the call keep their place in the chain of the block // they are writt | **none** | draft |
| src/runtime/control.cpp:99 | // A direct value input already orders ordinary users after its producer. | finished graph | finished graph (the scheduler reads it) |
| src/runtime/control.cpp:172 | // Epochs avoid clearing graph-sized scratch arrays for every target. | finished graph | finished graph (the scheduler reads it) |




## 读法

- 「**none**」占多数不是坏事，是**账**：这些注释都只说了「必须怎样」，没说「谁来保证」。今天它们大多由
  封印检查（graph_edges.cpp）在提交处兜住，所以**实际拥有者是完工的图**的比例远高于表里写着的。
- 表里属于**草稿**的那些，是「在改写过程中临时成立」的约束：它们由 GraphDraft 的写函数自己维持，
  一旦图冻结就不再有人问——所以它们**不能**被当作提交前检查，也不必被搬去别处。
- 表里也有噪声（ifndef NDEBUG、类型说明这类因为出现 "must" / "never" 被扫进来的行）；普查要的是
  覆盖面，不是精确率，等词表定下来之后再谈闸门。
- **检查器不给自己面子**：它对今晚那笔 ec5fce53 也照报两条（graph.h、graph_edges.cpp 的注释只说
  谓词的意思、没说拥有者），这个脾气要留着——它不是为了让输出好看而存在的。

## 「属于草稿」的那一组，为什么一条都没动

表里属于**草稿**的二十几条（`draft.cpp` / `draft_session.cpp` / `draft_inline.cpp`）**有意不加拥有者**：
它们在改写过程中成立，图冻结后就不再被问——所以它们都不该变成提交前检查，也不需要 `GraphDraft` 写者之外的
拥有者。给它们补一句「拥有者是草稿」，等于把「改写期间的临时约束」写成「提交时必须成立」，那正是今晚那条
顺序边教过的错。**这一组的拥有者就是写它的那些函数，不必再说一遍。**

## 补过的那一组（2026-10-31）

「事实上已经由安全网执行、只是没说」的那一组补上了拥有者，一句一条：

* `src/runtime/graph_edges.cpp` 四处（文件头的边不变式、`5.3` 的链、`crossedControlRegion`、CAST 例外）；
* `src/runtime/control.cpp` 两处（派发前的直接值输入、控制边的播种）；
* `src/runtime/graph.cpp` 一处（GATE 的槽位）——这条归**编码器**，它就在 payload-plan 那一族里，
  下一次找顺序边过滤的人会顺着它找到 `planDraftPayload`。

**补完重跑：43 条里 26 条仍没点名（补前 33 条）。** 剩下的是草稿那一组（有意不补）与噪声。