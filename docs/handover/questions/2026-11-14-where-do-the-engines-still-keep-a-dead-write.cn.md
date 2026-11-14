# 死写「可以删」之后，两个引擎分别卡在哪一处？

## 问什么

裁定「改写让变量不再被逃逸闭包捕获时，它最后一次写是没人能读的死写，因此可以删」落地为
`camel::runtime::deadWrite`（被删的写不是根、也没有端口读它，设计原则 9.7）。pass 边界的检查、准入、
通用引擎的删除与筛选都已按它走，`std::opt` 路径上死写**已经真的从图里消失**。仍没落地的只有 Camel 引擎
（`policy::direct` 一侧）的两条，本页把两边的读数、这次新发现的一处判据缺口、以及剩下要裁的问题写清。

## 读数（本树，`test::deadwrite` 在改写之后读同一件事）

| 路径 | 写还在吗 | 判据怎么说 | 程序输出 |
| --- | --- | --- | --- |
| `std::opt` | **不在**：`probe`/`main`/`__root__` 里都没有 `oper=:op/assn_l` 这一行了 | 之前一次读数是 `rooted=0 read=0 dead=1`（`dead_write.judgement_dropped` 现在停在 `std::opt::algebra` 之后读它） | `1\n`，与不优化一致 |
| `std::opt`（有根的写） | 在：`dead_write_held_by_program` 仍是 `rooted=1 read=0 dead=0`，`dead_write_rooted_closure` 的写被闭包调用读着 | 不是死写 | `41\n41\n` |
| `policy::direct` | 在：`DEADWRITE graph=probe node=6 oper=:op/assn_l rooted=0 read=0 dead=1` | 可删 | `1\n` |

## 这次发现的一处判据缺口（值得作者看一眼）

「没有端口读它」这半句对**就地写**不够：写在格子里留下的是值，后面对变量的一次读拿的是**格子**，
不是写的返回值。所以一个 `dead=1` 的写可能仍然被程序看见。第一次把删除接进通用引擎时，`dead_write_rooted_closure`
（闭包被调用、读 `c`）的输出从 `41\n41\n` 变成 `0\n0\n`，就是这个缺口。

补过的写法之一（**现未保留，见新页的读数**）：`deadWrite` 里加一条「它写进去的格子除它自己之外没有别的读者」。
它让反例墙重新站住（`dead_write_rooted_closure.opt` 回到 `41\n41\n`），但把正例也弄坏了：`std::opt` 之后
格子上剩下的那个读者是**已经死掉的闭包节点** `h`，判据于是答 `dead=1` 变成 `dead=0`，pass 边界的检查
（`pass.cpp#nodeRunsForItsEffect`）立刻报 `graph '__root__' has effect node 11 (':op/assn_l') with …`，
整条优化路径 4 条用例红。所以正确的措辞不是「格子有没有读者」，而是「格子有没有**活着的**读者」——
那要可达性，不是一个本地判断。这一点按「只改判据」的范围没有做，留给作者定 9.7 的措辞。

## 通用引擎：删要在三处都问同一件事，缺一处就删不掉

1. `rewrite.cpp#isObservedRun`：`Oper` 原先一律 `return !nodeIsFreelySchedulable(...)`，写永远算「被观察」，
   于是 `keepThroughValue` 为它写 GATE keeper。现在对 `Oper` 先问 `deadWriteOnceUnread`（无根 + 格子没别人读），
   再问 `nodeMayFault`——可能出故障的运行仍然算被观察，keeper 照旧。
2. `rewrite.cpp#isDeadWrite`：DCE 的清扫原先只扫「值」（`isValueOnly || isUnanchoredPure`），写不在其中。
   现在这份清扫另外问一次死写，`values_only=true` 的遍**不问**（那一遍承诺只删常量与纯运算）。
3. `screen.cpp#mayRemoveDead`：遍的筛子原来只对「没人读的纯 Oper」放行，于是带死写的图根本进不了这一遍。
   筛子现在也认死写——筛子只决定「这里有没有东西要看」，不能说「没事可做」。

## Camel 引擎：仍是两条，未动

- `sync` 块外的写编成 `<__assn__>` 的**调用**，`deletionAllowed` 对 `Call` 一律答「有效果」（没有局部摘要
  能证明调用无害）；`sync` 块内的写是 `:op/assn_l`，但它在链上有位置（链 GATE 是它的 `sync` 用户），
  `eraseUnused` 的 `unused` 一半因此不成立。两处都不是死写判据：前者要「调用是纯的」证明，后者要
  「链凭证可以撤销」的裁定。
- 无读者时的孤儿抽取同理：`writings_extract.cpp#select` 根的是「被写过的值」这个**位置**，
  护的是准入之后的凭证，不是死写判据。

## 待裁

1. 「没有端口读它」是否包括「格子没有别的读者」（上面那处缺口）？若不按此实现，反例墙会塌，所以这次按
   「包括」实现了，措辞请你定。
2. Camel 引擎的两条要不要收窄？收窄 `Call` 分支要的是调用效果摘要，不在本判据内。
3. `values_only=true` 的 DCE 不删死写（保留「只删常量与纯运算」的承诺）：同意否？
## 这次试过的删除，以及它为什么站不住（读数）

把「删」接进通用引擎的三处（`isObservedRun` 的写、DCE 清扫的写、筛子的写）之后，正例确实变成
「`std::opt` 之后 `dead=1` 的行一个不剩」，反例墙（`dead_write_held_by_program`、`dead_write_rooted_closure`）
也站住；但 `-Changed` 全量读出来 **49 红**，其中我的一批在 `feat.linear.var_reference`：

| 用例 | 现象 |
| --- | --- |
| `var_param.opt` 等 12 条 | `verification exited 1: unexpected output: 1 \| 8 \| 10 \| 5000 \| 3 \| 11 11 \| 11 \| 3 \| [1, 2, 3, 4] 4 \| 3 \| 2 \| 0 \| 0 1`（参考 15 行，实际 14 行） |
| `callback.write_through_parameter_opt` 等 8 条 | 同类 |
| `defun.widened_parameter.merged` | 同类 |

即：**经内联后的 `var` 形参格子里，被读的值也会被判成「格子没别人读」**，删掉后少了一行输出。
两次补法都没止住：把格子读者扩到 `with` 端口（捕获）；把格子读者沿 GATE 向前走（`isWrittenInPlace` 的
「直接或经 GATE」）。所以缺的不是读者集合的宽窄，而是**这个位置到底是谁在写、谁在读**在内联之后没有被
判据认出来——`placeIsReadElsewhere` 很可能连「写的那个操作数」都没在正确位置找到。
这也是我停手的原因：再往里走就要动到内联/位置标记本身，不在「只改判据」的范围内。
（后续：`writeIsRooted` 沿 GATE 向前走已经补进判据并落地；删除仍不安全，两次独立测量分别 **70 红**与
**21 红**，读数与三条结论见 `2026-11-14-what-makes-a-reader-alive.cn.md`。）

当前树回到**不删**的状态：判据（`deadWrite`，与准入同一处实现 `deadWriteOnceUnread`）与准入保留，
通用引擎一行未动；正例于是又变回 `dead=1` 且节点仍在（`dead_write.opt.still_there` 就是这个读数）。


