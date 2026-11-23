# 「读一个格子」算不算一个效应，以及算子记录的沉默在放置判断里值多少

## 事实

`core.arrangeable` 落地（`internal/questions/2026-11-23-who-owns-freely-arrangeable.cn.md`
的裁决）之后，逐节点的改前/改后答案对照跑在 6 个程序、224 个节点、70 个 OPER 节点上
（`test::arrangeable_probe`，临时探针，读数如下）：

| 程序 | 节点 | OPER | 可自由安排 同/异 | 效应 同/异 | `graph.is_pure` 同/异 | 放置（`touchesNothing`）同/异 |
| --- | --- | --- | --- | --- | --- | --- |
| `loops/recursion` | 27 | 5 | 5/0 | 5/0 | 5/0 | 5/0 |
| `effects/inferred_recursive_is_empty` | 17 | 2 | 2/0 | 2/0 | 2/0 | 2/0 |
| `facts/tensors_and_loops` | 40 | 11 | 11/0 | 11/0 | 11/0 | 11/0 |
| `linear/nvm/fib` | 27 | 11 | 11/0 | 11/0 | 11/0 | 11/0 |
| `compile/fold_calls/recursive` | 21 | 5 | 5/0 | 5/0 | 5/0 | 5/0 |
| `linear/byref/var_writes_through_recursion` | 92 | 36 | 36/0 | **23/13** | **23/13** | 36/0 |
| 合计 | 224 | 70 | 70/0 | 57/13 | 57/13 | 70/0 |

13 处不同的全部是 `:var/get`（图 47、48）：改前 `!isPure ∨ hasExternalEffect` 答「有效应」，
`EffectSummary` 答 `pure() = true`；改前 `graph.is_pure` 答 false，改后答 true。这正是 3.3
「读是一个依赖，不是一个效应」与 `OperatorTable::isPure` 的读法之间的差：前者说读格子不写任何
东西，后者说碰到格子就不算纯。

第二件，同一批探针读数里可以单独量出来：把放置判断的两个原子**只**从算子记录取
（`Record::pure()` / `Record::external()`，`uses` 没写就按 3.3 读成「写全局资源」），gpu 层
13 个已知红的原因从 `TILES-OUTLINED-10->12`（device）/`->5`（resident）整体变成
`TILES-OUTLINED-10->0`——`region_outline.cpp` 把几乎每个算子都当成宿主效应跳过，一个区域也
outline 不出来；同一棵树里 `nofallback.*` 另有 8 例由绿转红。改成「记录写了 `uses` 就按记录，
没写就按声明语义」之后，13 个已知红回到记录里的原因（`0 recorded for another reason`），
`is_pure`/效应/放置三列在 5 个程序上与改前逐项相同。

## 需要裁的两件

### ① 读一个程序变量，效应事实该答「有」还是「无」

裁决说「同一个格子别读第二遍；改问效应事实（`EffectSummary` 已经是答案）」。按那句做，
`:var/get` 的答案从「有效应」变成「无效应」——13 个节点、1 个程序。设计原则 3.3 支持后者
（读是依赖不是效应），而 `OperatorTable::isPure` 支持前者（碰了格子就不纯）。本笔按裁决交付
（事实层是唯一的家，`EffectSummary::pure()` 是答案），因此这 13 处答案确实变了；这条问题页是
把这个变化摆出来，不是把它判成对的。

要裁的是：`:var/get` 这类「读状态」的算子在 3.4 的第 1、2 项上算不算成立；若算成立，那
`graph.is_pure` 对它的答案就该是 true（现在的实现），若不成立，事实层要为「读」单独留一个项。

### ② 算子记录没写 `uses`，放置判断该读哪一边

3.3 说没写就按「写全局资源」读（`Record::external()` 的定义），而树里几乎每个算子的记录都没
写 `uses`，于是严格按记录读会让设备层整体停摆（上面 `TILES_OUTLINED 0` 的读数）。本笔为了让
每条改前/改后答案相同，取「记录写了就按记录，没写就按算子被声明时的语义」，也就是把
`OperatorTable::isPure` / `hasExternalEffect` 在事实层内部当作「声明语义」读一次。

要裁的是：这条「沉默就回落到声明」的读法是不是裁决里「两个原子从算子记录来」的意思。若不是，
则要么先补齐各模块声明里的 `uses`（谁补、按什么口径补），要么接受设备层停摆到补齐为止——
两条都不是本笔能自己定的。

## 本笔怎么处理

两件都按「改前答案 = 改后答案」的硬约束交付：①之外的每一处答案逐项相同（70/70 可自由安排、
70/70 放置、70/70 终止方向），①按裁决本身变化并把 13 个节点列在上面。②取回落读法，因此它
没有改任何答案，但它是不是裁决的本意，需要作者看过。

被删掉的临时探针（`test::arrangeable_probe` 与其 plan）只用来产出上面两段读数；读数记在这里，
探针不留在树上。