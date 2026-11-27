# 规则丢掉的操作数，谁把它从链上摘下来

## 这件事是什么

`rules::simplify` 把 `x*c1 + x*c2` 改写成 `x*(c1+c2)` 之后，**被丢掉的两个乘积仍留在图里**，而且它们不是"忘了删的死值"，是**顺序链上的成员**：

```
test/cases/opt/algebra_rules/tensor_forms.cml:13-17
func array_combine(x: Vec) => Vec sync { let a = x * 3.0 + x * 5.0; println(a); return x }
```
`opt/algebra_rules` 的 `tensor.table` 因此红：`array_combine` 9 个节点 / `tensor_mul 3`，参考列 8 / 2
（`test/results/log/…/algebra_rules.tensor.table/stdout.txt`，tip `2363f06174`）。值是对的
（`tensor.same_values` 绿，八行输出逐字相同）。

实际图（`std::jgir` + `std::cxx::dce values_only=true` 之后的读数，本次任务实测）：

```
g2n0 PORT x : f32[3]      g2n1 DATA 8.0      g2n2 DATA 5.0      g2n3 DATA 3.0
g2n4 OPER tensor:multiply (x*3.0)     g2n5 OPER tensor:multiply (x*5.0)
g2n6 OPER tensor:multiply (x*8.0)     g2n7 OPER :io/println      g2n8 GATE

E n0[0] -> n4[0] normal          E n3[0] -> n4[1] normal
E n0[1] -> n5[0] normal          E n2[0] -> n5[1] normal
E n4[0] -> n5[0] control         E n4[undefined] -> n5[undefined] sync
E n0[2] -> n6[0] normal          E n1[0] -> n6[1] normal
E n5[0] -> n6[0] control         E n4[1] -> n6[1] control        ← 见下
E n5[undefined] -> n6[undefined] sync
E n6[0] -> n7[0] normal   E n6[0] -> n7[0] control   E n6[0] -> n7[1] control   (+sync)
E n0[3] -> n8[0] normal    E n7[0] -> n8[0] control   (+sync)
```

三个乘积：`x*3.0`、`x*5.0`（被丢掉的）、`x*8.0`（替身）；`edges` 之所以仍是 12，因为
**表里的 `edges` 只数 norm/with/sync/order**：本图 8 条 norm + 4 条 sync = 12，与参考列同数
（参考列是同一批 norm 8 条 + sync 4 条，只是那 4 条 sync 原本指向 add）。多出来的 6 条 control 不计数。
`3.0+5.0` 在规则实例化时就折成了 `8.0`，图里没有标量加节点。

正确的形态是 `x`、`8.0`、`tensor:multiply`、`:io:println`、`GATE` —— **5 个节点 / 6 条边**，
由同文件的 `array_double`（`sync { let a = x + x; println(a); return x }` → `x*2.0`）实测钉住：
它正是 `nodes 5 edges 6` 且绿。所以这次增长是 **+4 节点 / +6 边**。

## 为什么删不掉：两个条件都不成立，原因却只有一个

替换之后引擎确实去收被替换节点的输入（`src/kernel/runtime/writings_driver.cpp:354` 调
`:168-200` 的 `eraseUnused`），但它对每个输入问四件事（`:173-175`）：

```
!draft_.alive(node) || kept(node) || !unused(node) || (shapeOf(node) == 2 && !deletionAllowed(node))
```
其中 `unused = normUsersOf.empty() && withUsersOf.empty() && ctrlUsersOf.empty() && syncUsersOf.empty()`（`:159-162`）。

**关键：`x*3.0` / `x*5.0` 不是"没有读者"，它们成了替身的顺序前驱。** 换位发生在删除之前一步：
`writings_driver.cpp:331` 的 `draft_.takeStatementPlace(node, made)`，其文档原文
（`src/kernel/include/camel/runtime/draft.h:1022-1027`）：

> The node `newId` takes the place `oldId` had in its block's chain: the statements written after
> `oldId` are written after `newId`, and `newId` is written after what `oldId` followed
> (docs/design/effects/edges.cn.md 4: **a replacement transfers the replaced node's orderings to its
> replacer**).

`sync` 块里 `x*3.0`、`x*5.0`、`add` 本来就是一条链（前一条的 sync/control 边指向后一条，见上图）。
`add` 被换成 `x*8.0` 之后，链被接成 `x*3.0 → x*5.0 → x*8.0 → println → GATE`：**链前缀留下来了**，
所以 `x*5.0` 有 `ctrlUsersOf = {x*8.0}`、`syncUsersOf = {x*8.0}`，`unused` 为假 —— 不是删除条件写漏，
而是"它还有用户"这个前提被上一步自己变成了真的。

`std::cxx::dce values_only=true` 也一样收不掉，而且同样是**照规矩**：
`src/stdlib/std/cxx/opt/generic/rewrite.cpp:115-127` 的 `isDeadWhenUnread` 落到
`src/kernel/runtime/graph.cpp:2009-2032` 的 `isUnanchoredPure`，最后一行（`:2025-2031`）要求
`ctrl/order/sync` 的输入**和**用户全空。带着链边界的节点在它的定义里就不是"可自由安排的值"。
所以 dce 的终止条件没有漏这批，它和 `unused` 用的是同一条"顺序不可忽略"的原则。

`graph.replace` 那条"什么也不擦"（`src/stdlib/graph/txn.cpp:247-318`，`5d4fb3e33c`）与本案无关：
那个分支走的是 `!isReplaceable` 的**锚点**（图的结果节点），本次 `add` 是被 `println` 读的普通值，
走的是另一支。**锚点不擦是设计意图，这里不是它的现场。**

对照（本地，不靠宿主）：同一个律、同一个元素类型、**非 sync 的 `return` 体**
（`test/cases/modules/rules/tensor_target.cml:11-13`）改写后计数是 `combine 1 2 → 0 1`
（`test/plans/feat/modules/rules.plan.toml` 的 `rules.tensor_laws_counts`，tip 上绿）——
那里没有 `sync` 块，`x*3.0`/`x*5.0` 不在任何链上，换位之后没人读它们，于是被删干净。
**缺陷只出现在"sync 块把子计算也串进语句链"的形状里。**

## 方向（这一步是设计问题，不是补一句条件）

引擎现在的做法忠于它自己的文档：替换要转移顺序（`draft.h:1022-1027`），删除要把穿过的顺序接起来
（`draft.h:1039-1042` 的 `spliceOrderEdgesOver`："no erasure leaves the ordering around the node
expressed by nothing"）。缺的不是这两条，而是**规则改写不是"一个语句换个节点"**：
模式左边被丢掉的操作数（`x*c1`、`x*c2`）是**整个语句的中间计算**，替身根本不读它们。
它们留在链上表达的是"这句话原来分几步算"，而那几步已经不存在了。

要补的动作，形状上和 `eraseNode` 对一条链中间节点做的事一样：**对右边不再读的操作数，先把它的链位
合起来（`closeChainOver` / `spliceOrderEdgesOver`），再看它是否 `unused`，再由 `deletionAllowed`
（`src/kernel/runtime/writings_deletion.cpp:45-112`）决定能不能删** —— 顺序守卫与 fault 守卫仍然是
最后一道闸：一条可能 fault 的语句，它的顺序是程序可观察的，不能因为模式丢了这个操作数就连顺序一起丢。

所以这条路要回答的只有一个问题：**被规则丢掉的操作数，它的顺序位是否随它一起消失**。答"是"，
`array_combine` 回到 5 节点 / 6 边；答"不是"（例如 `x*3.0` 在元素类型不准的数组上可能 fault），
今天的 9 节点 / 12 边就是正确行为，那么该改的是 `tables.mjs` 的判据而不是引擎。

判断这一点要用的是 `nodeMayFault(draft, id, byShape=true)`（`src/kernel/runtime/graph.cpp:1979-2007`）：
`f32[3]` 这类"类型把形状说全"的元素级运算答"不会 fault"，于是它的顺序不可观察；类型说不出形状时相反。
**这也正是 `lane-oper-shape-guard` 那条正在修的 `oper_shape` 守卫要给出的事实**，两件事应当一起定。

## 落点

改动在 `src/kernel/runtime/writings_driver.cpp`（`eraseUnused` 之前，`:331-354` 之间），不在
`src/stdlib/rewrite/*`：Camel 侧只是在 `writings_driver.cpp:1055` 构造 `DraftWritings` 并调
`:90` 的 `rewrite`，判据与删除都在内核里。`rules.cml`、golden、`dominance` 断言都不该为这件事动。

## 相关

```
· 实测：`opt/algebra_rules` 一次 run，`algebra_rules.tensor.table` 红、`tensor.by_cpp` 绿、
  `tensor.same_values` 绿，`agreement.*` / `divergence.*` / `library_only.*` 全绿（23/25，仅 tensor.table
  与临时 dump 条目红）
· `internal/reports/` 的 `2026-11-27-promotion-19-reds.tsv:61` 记着 `opt.algebra_rules  algebra_rules.tensor.table`
· 同形对照：`algebra_rules.agreement.cpp.txt:1` 的 `add_zero nodes 4 edges 3`（`sync { let a = x + 0;
  println(a); return 0 }`）—— DATA 操作数被丢掉时**删得掉**，只有 OPER 操作数留在链上
· 设计原则 9.7（删除要过权限）、3.4（可安排性四个条件）、3.9（fault）；edges.cn.md 4（替换转移顺序）
· 触发这条的改动窗口：`internal/reports/2026-11-26-promotion-8-reds.tsv` 无此行（`73aebe80b3` 绿），
  `2026-11-27-promotion-19-reds.tsv:61` 红（`065bfb1690`），窗口 820 笔
```

状态：**已定（2026-11-27 决策者裁决），改为引擎里做。** 裁决原文与改法见下；`tables.mjs`、`dominance`
断言、`array_combine` 用例、golden 都不动。

## 裁决

决策者原文：

> 不看会不会 fault。设计原则 :684 已定 —— 遍保留什么、删什么由遍自己决定，同步块里结果没用、但可能
> 故障的纯计算被化简遍删掉不算缺陷。改写遍的契约是：规则说右边等于左边，**替身接替的是整个被匹配的
> 左边，不只是根**。所以替换时，被匹配项内部节点在语句链上的位置**一并合到替身的位置上**
> （closeChainOver / spliceOrderEdgesOver），之后没有别的用户的内部节点由删除判定照常删。
> **期望 5/6 是对的，改引擎不改判据。**

所以上面那个"要回答的只有一个问题"由裁决直接答了：**被丢掉的操作数不保留顺序位**，`array_combine`
的正确形态是 5 节点 / 6 边，红的是引擎而不是判据。

## 改了什么

契约落在替换路径上，不在删除路径上（删除判定的语义一行未改）：

```
src/kernel/include/camel/runtime/writings.h    matchFrom 多收一个 termOut：左边在根之下读到的算子节点
src/kernel/runtime/writings_match.cpp          匹配时把走过的算子项记进 termOut（失败回溯时缩回）
src/kernel/runtime/writings_driver.cpp         换位（takeStatementPlace）与等待边都落好之后、eraseUnused
                                               之前，把"除了左边自己没人读"的项逐条 closeChainOver，
                                               并摘掉 control 里点名它的那几对；之后 eraseUnused 照常
                                               把它当无读者的节点删掉
```

合链放在换位之后，是因为换位本身要把根的链位（含它当时的前驱）交给替身；先合链会把那条前驱从根眼
前拿走。顺序反过来之后，替身先按原样接过根的位与等待，再让链从前驱直接连到替身，替身对那几项的
control 等待同时被摘掉（`closeChainOver` 已经把"它等过什么"交给了替身，见 `draft.cpp:1606-1676`）。

关键点：合链的条件是**除左边自己（根与其他项）之外没有 norm/with 用户**。右边仍读到的项自然落在
这个集合之外（替身就是它的用户），所以"规则把某一项留作自己的值"时那一项原地不动；这同时就是新加的
反控 `algebra_rules.tensor.shared_term_stays`（`test/cases/opt/algebra_rules/shared_term.cml`：
`let p = x * 3.0; let a = p + x * 5.0; println(p)` —— 律照样匹配，但 `p` 另有一个读者，改写后
`shared` 图上必须仍是 `tensor_mul 2` / `tensor_add 0`）。

另有一条把数字点名：`algebra_rules.tensor.left_side_leaves_the_chain`（同一个 `tensor_forms.cml`）断言
`array_combine` 就是 **`nodes 5 / edges 6 / tensor_mul 1 / tensor_add 0 / println 1`**。`tables.mjs` 对
改写过的形态只做"节点、边、每种算子都不多于参考列"的界，单靠它一个"只缩小"的实现也能过；这条把数
写死在那张表上，引擎哪天不再合链，它按名字和计数红。