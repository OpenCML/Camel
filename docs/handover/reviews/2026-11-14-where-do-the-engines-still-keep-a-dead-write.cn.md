# 死写「可以删」之后，两个引擎分别卡在哪一处？

## 问什么

裁定「改写让变量不再被逃逸闭包捕获时，它最后一次写是没人能读的死写，因此可以删」落地为
`camel::runtime::deadWrite`（被删的写不是根、也没有端口读它，设计原则 9.7）。pass 边界的检查已按它放行，
`DraftWritings::deletionAllowed` 已按它的 rooted 半边放行准入。**但删还没有真发生**：两条引擎各自还有一处
把写留住的判断，两处都不是「死写判据」，所以本次没有动它们。要定的是：这两处该不该、按什么条件收窄。

## 读数（本树，`test::deadwrite` 在改写之后读同一件事）

| 路径 | 参数 | 写还在吗 | 判据怎么说 |
| --- | --- | --- | --- |
| `policy::direct` | `order=false faults=false` | 在：`DEADWRITE graph=probe node=6 oper=:op/assn_l rooted=0 read=0 dead=1` | 可删 |
| `std::opt` | 无 | 在：`__root__ node=11`、`probe node=6`、`main node=10` 三处 `oper=:op/assn_l … dead=1` | 可删 |

两条都是 `dead=1` 而节点仍在图里；程序输出不变（`1\n`）。**判据与引擎在此不一致，且不一致的方向是保守的**
（引擎留、判据说可以走），所以没有产生错误行为，只是裁定要的那一步还没落到图上。

## 卡在哪两处

1. **`src/passes/opt/generic/rewrite.cpp#isObservedRun`**：`Oper`/`Cast` 一律
   `return !nodeIsFreelySchedulable(...)`；写永远不是自由可调度，所以永远算「被观察的运行」，
   `lastReadsGoingOut` 于是为它写一个 GATE keeper（`keepThroughValue`），DCE 之后它仍在图里。
   要收窄，就得在这里问「这个写有没有根」，那正是钉住 `keepThroughValue` 的那条链：

   ```cpp
   A write is never freely schedulable, so isObservedRun treats every write as observed and
   writes a GATE keeper for it (keepThroughValue).
   ```

   注意这里只能问 rooted 半边：`isObservedRun` 只在 `lastReadsGoingOut` 里被问，而那一刻这个写的读者
   正是**将要被删掉的**那个节点，读还在，问 `deadWrite` 的 read 半边永远答「有人读」。

2. **`src/runtime/writings_deletion.cpp` 之外的两条**：Camel 引擎里，`sync` 块外的写编成
   `<__assn__>` 的**调用**，`deletionAllowed` 对 `Call` 一律答「有效果」（没有局部摘要能证明调用无害）；
   `sync` 块内的写是 `:op/assn_l`，但它在链上有位置（链 GATE 是它的 `sync` 用户），
   `eraseUnused` 的 `unused` 一半因此不成立。两处都不是死写判据，收窄它们各自是另一个问题：
   调用的删除要的是「调用是纯的」证明，链位置的让出要的是「链凭证可以撤销」的裁定。

## 已做的（较窄选项）

- `deletionAllowed` 问 `isWriteOfProgramState` + `writeIsRooted`（同一判据的两个半边，draft 与冻结图共一处实现）。
- `writings_extract.cpp#select` 的 `writtenInPlace` 根**未动**：它根的是「被写过的值」（格子/对象），
  护的是「准入之后的凭证」——写被准入保留后它的操作数不能因为没有读者而被抽取剪掉，
  与死写判据不是同一件事（`src/runtime/writings_extract.cpp` 的注释原话：`orphan effects without an
  admission credential`）。
- 未触碰 `uses` 子句与闭包捕获语义（冻结区）。

## 待裁

1. `isObservedRun` 是否改成「无根的程序状态写不算被观察的运行」（问 rooted 半边）？这一处一改，
   `std::opt` 路径上 `dead=1` 的节点就会真的消失；风险是它对整条优化路径生效，需要一次全量读数。
2. `deletionAllowed` 的 `Call` 分支要不要求「被调用图的写全是死写」这种局部摘要？这已经不是判据问题，
   是调用内联与效果摘要的问题。
3. 若两条都不动，则裁定目前只在「检查放行」这一层生效，图的删除仍不发生——是否可接受，请作者定。
## 决策者答复（2026-11-14）

三条都按原则已有的话推进，不需要作者另裁；归第 3 路，优先级一。

1. **`isObservedRun` 改：没有根的程序状态写不算被观察的运行。** 问 `deadWrite` 的 rooted 半边，判据只在一处，这里只调用它。全量读数照常报；`std::opt` 路径上 `dead=1` 的节点消失、输出不变是验收，E9 慢层那一档（第 7 路）的三个数不得上升。
2. **`sync` 块内外的写是同一件事，用同一个算子。** 块外的写编成 `<__assn__>` 调用、块内编成 `:op/assn_l`，违反"同一类事各处同一条规则"。统一成 `:op/assn_l`，块外那条 `Call` 分支随之不再遇到写。调用节点能不能删，按 R3 唯一分析给调用节点的足迹判（被调函数的足迹即调用的访问，原则 3.3"握着什么，类型就提到什么"），不在 `deletionAllowed` 里另写一份局部摘要。
3. **（同日修正：块里的写是块的根，见问题表"同步块按块内的时间线资源实现"；下面这一条只对块外成立，块里的死写不删。）** **链上的位置不是"用到"。** 原则 3.1 已写明："用到"由区域的结果承担，顺序边只排序，不兼任"用到"。`sync` 块的链只排序，对程序自己的变量的死写，链上有位置也照样可删；删时把它在链上的前后接起来（原则 9.7"删除只经专门的操作"那一条操作负责接链）。对外部资源的写本身是区域的根，不受影响。

**另：判据要收进唯一的分析。** 现在"这个节点要不要执行"分在 `effect_liveness.h`、`deadWrite`、`isObservedRun`、`deletionAllowed`、`writings_extract` 的 `writtenInPlace` 几处回答。这一批先各自调用 `deadWrite`，R3 第 0 项落地时一起并进 `NodeEffect`（`regionRoot`、`freelySchedulable`），别处只调用；合并时报这几处还剩几个各自的判断。`dead_write_held_by_program` 用的是模块顶层 `var`，随顶层 `var` 迁移一起改写或删除。
