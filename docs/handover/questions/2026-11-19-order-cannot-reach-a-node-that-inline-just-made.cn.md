# `order` 写不到刚 `inline` 出来的节点上，而文档说它写得到

2026-11-19 ｜ 来自 `eco/refused-passes` ｜ 对象：`graph::order`，`modules/graph/txn.cpp:423-433`（`orderKernel`）、
`stdlib/graph.cml:377-383`（`inline` 的文档）、`stdlib/graph.cml:365-375`（`order` 的文档）
｜ 读数在 `2d7c5c3a1`，Release，指纹 `0244fca0cbac…`

## ① 事实

**1. 今天：`order` 不接受事务造的节点。** `orderKernel` 只把**第 2 个参数**过 `draftOf`，而
`draftOf`（`txn.cpp:162-179`）要求 `node->program() == state.base`；`inline` 回来的句柄是
`Handle::createInTransaction(transaction, inlined.valueExit, …)`（`txn.cpp:580`），它的 `program()` 是
**事务**的草稿，不是 `state.base`。所以拿它当 `order` 的任一端都被拒：

```
error[RuntimeDiag::InvalidArgument]: Invalid argument of 'graph.order': the node belongs to another program than the transaction.
    --> /tmp/rp-eco/eco.cml:167:41
```

（`anchors->foreach<(n: Node): void => order(t, n, made)>`，`made` 是 `inline` 的返回值；这是本轮
`edits-ecoop.cml` 里甲对照的第一版写法，见 `docs/handover/reports/eval-round10/item-ecoop-refused-passes.md` §6。）

**2. 文档说写得到，而且是两处都在说。**

- `stdlib/graph.cml:377-383`，`inline` 的文档：「… a node that stands in no chain only selects the region,
  is left as it is, and **the ordering of the body's writes among the accesses already there is `order`'s to
  say**.」
- `modules/graph/txn.cpp:475-478`，`inlineKernel` 的注释：「* `at` stands in no chain -- a pure node -- it
  only selects the region: the body lands in an Inline region inside `at`'s, and `at` itself and its readers
  are left as they are. **Where the body's writes run among the accesses already there is `order`'s to say**,
  which is the caller's (the operator writes no order edge into that region).」

**3. 后果在本轮的量里是实的。** 案例程序上"在纯节点处 inline 一个打印体"必然要再带一条排序才可能合法
（不排序就是本轮甲的 `PassEffectOrder` 被拒），而**那条排序今天写不出来** ⇒ 一个用 Camel 写的 pass 想
"加一条打印并对好序"，唯一走得通的路是**让体接管一条已在链上的语句的位置**（`takesPlace` 分支）。本轮甲对照
就是这么做的，代价是被接管的那条语句（`main` 打印权重）被通知替换：输出从
`loss before / loss after / [[1.9999925], [-0.99998784]]` 变成 `loss before / loss after / announced`。

## ② 问题

**那句文档是承诺还是描述？**

- 若是**承诺** ⇒ `order` 应当接受事务造的节点（`inline`、`eval`、`add_constant` 的返回值），至少当它与另一端
  确属同一张草稿图时。
- 若是**描述**（即"调用者自己想办法"）⇒ 文档该改：它现在给了一个 API 做不到的操作，而 `txn.cpp:475-478`
  的括号里甚至写着"the operator writes no order edge into that region"，读者读成"所以你来写"。

请判是哪一种。

## ③ 默认建议

**按「承诺」处理**：`orderKernel` 里在 `draftOf` 之前先认"这是本事务造的节点"，按事务现有的草稿图取它的
`gc_node_ref_t`；两端同属一张草稿图就接受，`runsBefore` 的圈检查照旧。这样
`inline(t, pure_node, body)` + `order(t, existing, made)` 就是一条能写出来的"加打印并对好序"，本轮甲对照也不必
靠接管已有语句。

**但不在定版前动**：它在 `stdlib/graph.cml` 与 `modules/graph/txn.cpp` 两处接口上，触及"节点句柄属于哪个
程序"这条别处也在用的规则（`txn.cpp:617`、`txn.cpp:729` 各有一句同样的拒），而 `2d7c5c3a1` 是论文的定版 pin、
截止只剩不到一天。⇒ **这一轮只落这页；改法与它的回归（`test/cases/modules/graph_function/inline.cml` 与
`graph_function` 计划里应新增一条"纯节点处 inline 之后 `order` 那条体"的读数）等定版后另立一笔。**

论文侧的建议：这一处**不要写成"API 支持按需排序"**。今天支持的是"用位置换有序"，`order` 只能排在事务打开时
就存在的那些节点之间。