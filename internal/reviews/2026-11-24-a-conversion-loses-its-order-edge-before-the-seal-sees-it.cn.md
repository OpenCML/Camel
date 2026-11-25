# 转换节点上的顺序边在封存之前就没了：「能不能丢」这个判据缺「会不会出故障」一项，缺在哪几处、由谁补

## 事实

`feat/opt` 上六条用例在合并树上红、在基线 `570fe6772a` 上绿（逐名对照，总数都是 1634）：

```
order_edges_follow_a_replacement / order_edges_survive_a_removed_node /
order_edge_orders_the_nodes_that_run / order_edge_survives_the_commit /
the_five_user_sets_read_the_same_on_both_sides_of_the_freeze   —— 五条同一个机制
copy_type_mismatch_is_refused                                  —— 另一条（见末节）
```

五条的读数都是同一个形状：pass 写进顺序边（`ORDER wrote=11`、`wrote=1 twice=0`），封存接受，
但封存后的图里 `ORDER edges=0` ——**边不是被拒，是节点连同边在封存之前就没了。**

四枪，每一枪都否掉了自己的假设，而每一枪把范围收窄一层（次序如下，下一个从「转换不能排序」
出发的人会重走一遍，所以记在这里）：

| 枪 | 改动 | 读数 | 结论 |
| --- | --- | --- | --- |
| 探针 | `anchorNode` 传真 `__cast_value__`（节点有算子身份） | 六条不变 | 与**身份**无关 |
| P1 | `:op/cast` 声明 `foldable = false` | 五条绿；`fold_cast.rewrites` 红（`a conversion of a constant was not folded: 4 CAST nodes left`） | 拿折叠换绿，否 |
| P2′ | `graph.cpp` `isUnanchoredPure()` 的 OPER 分支改问 `nodeIsFreelySchedulable` | 六条不变；折叠绿 | 不是这条（它唯一的调用者是 `writings_extract.cpp:164`，写集抽取，不是删除） |
| P3 | `graph_edges.cpp` `nodeTakesNoOrder()` 对 OPER 节点改问 `shapeOfOperator()`（对齐 `may_be_rearranged`） | 六条不变 | 不是「拒绝」这一侧：封存本来就接受 |
| P4 | `writings_deletion.cpp` 的 `deletionAllowed()` 加「会不会出故障」一项 | 六条不变；折叠绿 | 不是这条删除 |

**唯一被证明有效的杠杆是 P1**：让 `mayBeRecomputed(":op/cast")` 为假，五条立刻绿。两个方向都测过
（正向：它为真 ⇒ 转换被当成无 effect、可删；反向：它为假 ⇒ 五条绿），所以这条线上确实有东西把
「可重算」读成了「可丢」。

## 缺的是同一句话的第二个项，而它散在七处

`kernel/core/operator_table.h:396-400` 已经写明：

> Whether a pass may also **drop, move or reorder** the call depends on whether it can fault as
> well: that is `nodeIsFreelySchedulable`.

而树里读 `mayBeRecomputed` 的七处，**只有一处**接了第二个项：

| 位置 | 它拿这个答案去做什么 | 有「会不会出故障」吗 |
| --- | --- | --- |
| `kernel/runtime/graph.cpp:162`（`may_be_rearranged`） | 能不能重排 | **有**，紧接着问 `nodeMayFault` |
| `kernel/runtime/graph.cpp:2059`（`isUnanchoredPure`） | 能不能算「无锚的纯节点」 | 无（P2′ 改过，六条不变） |
| `kernel/runtime/writings_deletion.cpp:83`（`deletionAllowed`） | 能不能删 | 无（P4 改过，六条不变） |
| `kernel/runtime/writings.cpp:336` | 能不能取作一条写（writings 抽取） | 无 |
| `kernel/runtime/read.cpp:605` | 是不是纯值（`is_foldable_value` 一族） | 无 |
| `stdlib/std/cxx/opt/generic/rewrite.cpp:86`（`isPureOper`） | **它自己的注释就写着「fold it, move it, merge it or drop it」** | 无 |
| `stdlib/std/cxx/opt/generic/screen.cpp:81`（`isPureOperNode`） | 「the pass may fold or drop」 | 无 |

七处里六处没有第二个项，而它们的注释都写着「drop / move」——**判据的原文在一处，用法在七处，
而六处只照抄了一半。**

## 需要裁的三件

### ① 「能不能丢」是做成一个 kind，还是各处把第二个项补上？

`graph.cpp:162` 已经是一个形状正确的复合判断（重算 + 不故障），`nodeIsFreelySchedulable` 也是它的
draft 版本。若裁决是「一个 kind」，那六处应当改成问它，注释里那句「depends on whether it can
fault as well」才只有一处实现；若裁决是「各处自己拼」，`operator_table.h` 那段注释要收回，因为
它现在描述的是一个没有地方执行的规定。

### ② 若做成一个 kind，它落在 `kernel` 还是 `ops`？

`nodeIsFreelySchedulable` 在 `runtime/graph.h`，而六处里两处在 `stdlib`（generic 的 screen/rewrite）。
上一页（`2026-11-23-who-owns-freely-arrangeable`）问的是同一件事的另一半：那个谓词该由事实层给，
还是各处自己拼。这一页是它的第二个实例，形状相同而落点不同（那一页是「33 处拼纯 + 外部效应」，
这一页是「7 处只拼了重算」）。

### ③ `:op/cast` 声明 `foldable = false`（P1）能不能作为替代？

代价是量出来的：`fold_cast.rewrites` 从绿变红，常量转换不再被折成常量（4 个 CAST 节点留着）。
按「不为让检查变绿而改语义」，这一条要由设计原则裁，不是由测试绿不绿裁。

## 为什么停在这里，而不是打第五枪

四枪里三枪是排除，一枪（P1）是反向证明；剩下的候选都在上面那张表里，**而它们属于同一个判断：
「可重算」与「可丢」是不是一件事、这句话该由谁实现。**这是 `operator_table.h` 那段注释的归属问题，
不是再测一次能分开的——第四枪之后需要的是这份因果链，不是第五个假设。

## 另一条，不在这个机制里

`copy_type_mismatch_is_refused` 要的是**拒绝**（`EDGES refused`、`copies a f32 as a i32`），而上面
五条是**丢失**。它可能同源（转换不再是 CAST 种类，于是类型检查走了另一条路），也可能独立；
在 P1–P4 里它一次都没动过，所以按「失败消息在 P1 前后是否相同」单独读，不要并进这一页的结论。
---

## 决策者答（2026-11-24，`D-1124-65`）

**这三问 11-23 已经定过**（`internal/reviews/2026-11-23-who-owns-freely-arrangeable.cn.md` 决策者答 ①②）。这次是**没落实的第二个实例**，不是新问题。

### ① 做一个 kind，不是六处各补一项

**「可自由安排」是事实层的一个 kind**，等于：**纯 ∧ 不会出故障 ∧ 按节点已知会终止**（设计原则 3.4）。**读者只问这个 kind；事实层之外不许再拼这几个原子。**

### ② 落在事实层，不落 ops

**与效应事实同处（`kernel/runtime` 的 fact）。** ops 的算子记录只放**声明的原子**（纯、可能故障）；**组合与按节点的终止判断是事实。**

### ③ `foldable=false` 不行

**它让常量转换不再折叠 —— 那是为了让检查变绿而改语义。**

**折叠是另一回事：**在编译期把一个常量算出来，判据是「**纯，且这次求值没有出故障**」（原则：**必然出故障的常量表达式不折叠、给警告、留到运行时**）。所以 `:op/cast` 可能故障**不妨碍** `cast(1)` 被折叠。**移动、合并、删除、移出循环**才问「可自由安排」。

**七个读者按动作分：折叠的问前者，其余问 kind。**

### 车道的下一次（第五次）

**照此做，验收两条：**
- `new=6` **清零**；
- **常量转换仍然折叠** —— 并**加一条用例钉住**这一点（不是靠 `fold_cast.rewrites` 一条既有用例替它作证）。