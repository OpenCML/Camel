# 2026-11-26 放置族离开 `graph.h`：一次搬家，三个读数

## 一句话

把"节点能否移动 / 能否放置"这一族**声明**从 `src/kernel/include/camel/runtime/graph.h` 搬进两个窄头
（`node_kind.h` 装词汇、`graph_placement.h` 装问题），`graph.h` 的传递包含 TU 从 **351 降到 323（−28，−8%）**，
而"改这一族声明"一次重编的单元从 **367 降到 9**（其中 2 个是每次都重编的 `build_stamp.cpp`，真实单元 7 个，
约 **52×**）。判据计划 `facts` / `arrangeable` / `arrangeable_families` / `frontend` 共 **29 条全过**。

## 换基准说明（读者先读这一段，否则会把 35 → 7 读成"优化了 5 倍"）

> 新基线把 arrangeable 搬到事实层（`camel/facts/arrangeable.h`），窄头因此只剩仍在 `graph.h` 里的四族，
> include 集合随之从 9 个 TU 收到 7 个 —— **35 与 7 的差不是测量差异，是基线变了**。

具体说：上一版窄头还装着 `graph::may_be_rearranged` 与 `nodeIsFreelySchedulable`（35 个 TU 经由它到达），
换底时那两条已经被抽成事实层的 `nodeIsFreelyArrangeable`（`read.cpp` 的 `graph.may_be_rearranged` 内建现在
转调它）。所以窄头**不照抄旧声明**——照抄等于重新声明两个已删除的函数，是语义回退——只留下仍在 `graph.h`
里的四族：`castMayFault`、`nodeMayFault`（kind 型与 graph 型）、`shapesAreFixed`、
`graph::is_chain_statement`（kind 型与 graph 型）及两个 `isChainStatement` 转发。`chain_exit`、`is_chain_exit`
仍在 `graph.h`：它们回答的是"链的形状"，不是"节点能不能动"。

## ① 传递包含 TU 数（同一脚本、两棵树各跑两遍，数字一致）

```
树 236d795eaf（换底基线）: graph.h              transitive=351 direct=84 headers-in-own-closure=59   (两遍 351)
树 本分支尖端            : graph.h              transitive=323 direct=84 headers-in-own-closure=60   (两遍 323)
                          graph_placement.h     transitive=7   direct=7  headers-in-own-closure=3    (两遍 7)
                          node_kind.h           transitive=351 direct=0  headers-in-own-closure=1
```

`node_kind.h` 的 351 是它**继承** `graph.h` 的扇出（`graph.h` 含它），所以它不是窄头：它进 `graph.h` 是
为了让"只要一个 kind、一个 node id"的头不必持有整张图的布局，收益在别处（`derivative.h`、`stdlib/graph/nodekind.h`
这两条边：原来经 `graph.h` 各带 27 / 1 个 TU，现在只带 1 个）。

## ② 实测重编单元（同一棵树、同一构建门，数 `[k/N] Building CXX object` 行）

```
基线 236d795eaf + 在这一族声明旁插一行注释 → 367 行     (完整构建 479 个对象)
本分支尖端      + 在 graph_placement.h 同位置插一行 →  9 行     (完整构建 468 个对象)
  2 × build_stamp.cpp                                  ← 每次必重编
  7 × {facts_compare, graph_edges, read, edges, facts, graph, builder_expr}
```

⇒ 真实单元 **365 → 7**。这 7 个正是 `graph_placement.h` 的 7 个直接包含者：`graph.cpp`（实现）、
`facts.cpp`（`nodeMayFault`）、`graph_edges.cpp` 与 `test/edges.cpp`（`graph::is_chain_statement`）、
`read.cpp`（`nodeMayFault`(graph)）、`facts_compare.cpp` 与 `builder_expr.cpp`（`castMayFault`）。
另外两处 include 是多余的，已删：`engine.cpp`、`builder_helpers.h` 现在只剩 `draft.h` 的 draft 型重载。

## ③ 受影响的计划（真退出码）

```
[SUMMARY] total: 29, pass: 29, fail: 0      RUN_EXIT=0      [TREE] clean
  facts 10/10（含 arrangeable_every_operator_answers 117 s）
  arrangeable 2/2、arrangeable_families 7/7、frontend 6/6 + syntax 4/4
```

换底前那份交付在这一批 29 条里有四条红（`frontend.ast_record`、`ast_snapshot`、`format_keeps_ast`、
`fuzz_lossless_tree`），是解析器记录与 formatter 规则的缺口，已在整合分支上补齐；本尖端**一条红都没有**。

## 怎么做（下次撞同样的冲突照这个来）

1. 换底撞在**自己改过、别人也改过**的文件上时，不在冲突标记上手工缝合：把冲突文件整份取基线侧
   （rebase 期间 `git checkout --ours` 就是"被换底到的分支"，等于退回未拆分状态），再**重做一遍搬家**。
2. 重做之前先看基线**动过什么**：这一次基线把 `may_be_rearranged` 整族搬去了事实层，照抄旧窄头会重新声明
   已删除的函数。搬家的对象是"现在还在 `graph.h` 里的那一族"，不是"上次那一族"。
3. 搬完把指针改对：声明搬走之后，`draft.h`、`read.cpp` 里指 `graph.h` 的句子改指 `graph_placement.h`；
   `gc_node_ref_t` 的名字进了 `node_kind.h`（`graph_view.h`、`node_order.h` 的指针跟着改）。
   同一个基线里还留着一处陈旧指针：`operator_table.h` 仍说 `core.arrangeable` 在
   `camel/runtime/graph.h`，其实在 `camel/facts/arrangeable.h` —— 换基准时顺手核一遍这类句子是值得的。

## 这份读数会在哪里偏

```
· 两项 TU 数是**源码包含图**口径（`*.cpp` 逐个解析 `#include`，两遍同值），不是 `-H` 的深度分层口径；
  两种口径的绝对数不同，只有同脚本的差值可比。
· ② 的两个数是"同一构建门下的对象重编行数"，含 2 个每次必重编的 build_stamp.cpp；完整构建的规模
  因树不同而差 11 个对象（479 对 468），所以只比增量、不比总量。
· "改这一族声明"的探针是一行注释，插在 `castMayFault` 声明之后，数完即回滚；它量的是"这一个头被谁包含"，
  不量编译器内部对声明体的依赖。
· diff 的 churn 口径：`git diff -w --numstat` 与不带 `-w` 逐项相同（17 个文件、209/132）才算"无 token 变化"；
  这次两处预提交 clang-format 对既有行的折行（`derivative.h`、`engine.cpp`）也算在这 209/132 里，
  因为它们改的确实是行。
```

## 与「头文件的分量」那一页的关系

`2026-11-26-header-weight.cn.md` 量的是"把 `graph.h` 拆成接口 + 定义值不值"，结论是不值：83 个按名字包含它的
TU 里 82 个要 `GCGraph` 的**完整定义**，接口拆完那一行 359 → 358。那个结论仍然成立，而且与这一页不冲突：
这一页搬的不是 `GCGraph` 的定义，是**四个不需要 `GCGraph` 的自由函数声明**，它们的调用方（7 个 TU）
只给了 kind、node id、operator id 和一个类型，属于那一页说的"只读声明就能编过"的那一类，
只是数量不在那 83 个里——它们本来经别的头拿到这些声明。