# 一个格子「有活着的读者」是怎么算出来的？

## 问什么

裁定的判据里有一句「没有端口读它」。上一页的读数说明：照字面实现这句话，会删掉程序还需要的一次写
（`linear.var_reference` 少一行），而加一句「格子没有别的读者」又把已经死掉的节点当成读者。
所以要定的是：**「活着的读者」怎么算**，以及这句话该由哪一层来答。

## 定义

> **一个格子有活着的读者**：在写这个格子的那次写之外，读这个格子的节点（它的 norm / with 用户，
> 沿 GATE 向前走——GATE 没有自己的格子，读它就是读格子）当中，**至少有一个在该图的活节点集合里**；
> 活节点集合 = 从图的根（entry / exit / output / return、`normPorts`、`withPorts`、`closureNodes`、
> 每个区域的结果）出发，沿 norm / with / ctrl / sync 输入**向后可达**的全部节点。

与「有读者」的差别只有一处：**读者自己也可能是死的**——没有任何根能走到它，程序永远看不到它做的事。
今天判据数的是「有读者」，于是这种节点替它背后那个格子续了命。

**可判定**：图有限，一次 O(V+E) 的向后走；每个图走一次、之后按集合查成员，不是每个节点走一次。

## 五处现状

| 位置 | 它真正回答的问题 | 今天怎么答 |
| --- | --- | --- |
| `include/camel/runtime/effect_liveness.h:82-124` | 判据的**接口**：什么叫写的程序状态、有没有根、有没有人读、是不是死写 | 五个声明，`deadWrite = isWriteOfProgramState && !rooted && !read`（read 半边见 `:124` 的 `deadWriteOnceUnread`） |
| `src/runtime/draft_writes.cpp#writeIsRootedWith:338`、`#deadWriteWith:356` | 根持不持这次写（值或位），有没有人读**这个写的值** | `isNamed` 只问端口/根/区域结果；**写的操作数本身**是不是根，**不沿 GATE 走**（`:348`） |
| `src/passes/opt/generic/rewrite.cpp#isObservedRun:202` | 去掉一个节点的最后一个读者时，**这次运行本身**还算不算程序的 | `!nodeIsFreelySchedulable`（3.4），写永远算「被观察」；`keepThroughValue` 于是为它写 GATE keeper |
| `src/runtime/writings_deletion.cpp#deletionAllowed:45` | 这条规则改写时**能不能撤掉这个节点的效果**（准入） | 已问 `camel::runtime::deadWriteOnceUnread`（去掉 read 半边，因为读者正是本规则要删的） |
| `src/runtime/writings_extract.cpp#select:235`（`writtenInPlace` 根） | **谁写过这个位置**，所以它的值没有唯一书写（抽取） | 根的是「位置」，不是「死写」；护准入之后的凭证 |
| （判据的第六个读者）`src/execute/pass.cpp#nodeRunsForItsEffect:2446` | 越过 pass 边界时，这个效果站不站得住 | 问 `deadWrite`；死写放行 |

## 三个必答

1. **今天的树里能不能算**：**能**。`camel::compile::gir::liveDraftNodes`（`src/compile/gir/graph_read.cpp:518`）
   就是上面那条向后可达，根集合、四种输入、Loop/Brch 的区域一并算，`graph_encode.cpp:415` 已经在用；
   冻结图侧有同源的 `liveNodeCount()`。代价是**分层**：它在 `compile::gir`，判据在 `runtime`，`runtime`
   不能反过来依赖 `compile`。所以接线要么把活集合作为参数传给 `deadWrite*`（两个调用点都在 `passes/`，
   两边都能看见），要么把这条走法下沉成 `runtime` 的 `liveNodesOf(draft)`。**新建不是必需的。**
2. **`linear.var_reference` 少一行，是判据错还是删除动作错**：**判据错**。缺的是
   「位置**连着根**」，而 `writeIsRootedWith:348` 只问操作数自己是不是根：`var` 形参的格子在内联后
   到达时**隔着一个 GATE**，于是 `rooted=0`；写的返回值又没人用（`read=0`），判据把它判成死写。
   证据是输出形状：`9` 变 `3`、`15` 变 `3`、`7` 变 `0`——都是**旧值**，即写被删、读者读到的是删之前的内容；
   若是删除动作错（删对了节点、接错了链），症状会是顺序或崩溃，而不是稳定的旧值。
3. **`std::opt` 之后读者集合是不是脏的**：**是**。带「格子没有别的读者」那条时，pass 边界当场报
   `graph '__root__' has effect node 11 (':op/assn_l') with …`——这说明折掉 `p.1` 之后格子上**还有一个非 GATE、
   非本次写的用户**（唯一候选是闭包 `h` 的 `Func` 节点）。它活得下去是因为清扫不认这种节点：
   `screen.cpp:239-258` 的 switch 只有 Gate / Cast / Copy / Accs / Fill / Data / 纯 Oper，**没有 Func、Call、Loop**，
   `rewrite.cpp#isDeadWhenUnread:87` 也一样。所以「最后一个读者走了的闭包节点」永远留在图里，替格子续命。

## 收敛顺序（建议）

1. 先修 `writeIsRootedWith`：根也沿 GATE 向前走（与 `isWrittenInPlace` 同一句「直接或经 GATE」）。这一处
   只动判据的一个半边，不动引擎，`linear.var_reference` 那一类应当立刻回绿。
2. 再把活集合接进来：`deadWriteOnceUnread` 多一个 `std::span<const gc_node_ref_t> live` 参数，由调用点给
   （`passes/` 侧本来就能调 `liveDraftNodes`），判据内部只做成员判断。
3. 然后才轮到删除：清扫认写、筛子认写、`isObservedRun` 问 rooted 半边。

**可测判据**：第 1 步做完，`feat.linear.var_reference` 259/259 必须绿、`dead_write.opt.still_there` 仍是
`dead=1`；第 2 步做完，`feat.opt.effects_kept` 180/180 必须绿、`dead_write_rooted_closure.opt` 仍是 `41\n41\n`；
第 3 步做完，`dead_write.opt.still_there` 翻成 `.gone`（`dead=1` 一行不剩）而墙两条不动。

## 第 1 步落地之后的三条结论

**一、`screen.cpp:239-258` 的 switch 缺 `Func/Call/Loop`，闭包节点收不走。这一条是独立缺陷。**
判定：**清扫该扩种类**，不该另立机制。理由是清扫已经是「没人读的节点可以走」的唯一执行者
（`rewrite.cpp#isDeadWhenUnread:87` 与 `generic.cpp#dceDraft` 的删除条件），`Func` 与 `Oper` 在这一点上
没有不同：一个没人读的闭包节点和一条没人读的纯运算一样，既没有效果、也没有读者。留着的代价是
活集合接入之后会立刻显形——`liveDraftNodes` 从根向后走，而这条闭包节点是**不在活集合里却在图里**
的节点，任何「还有没有别人读」的本地判断都会先看到它。所以第 2 步之前先把清扫的 switch 补齐
（`Func`、`Call` 至少；`Loop` 要它的区域端口先算完），比把活集合接进来更省事。

**二、第六处判断**：`src/execute/pass.cpp#nodeRunsForItsEffect:2446`（越过 pass 边界时这个效果站不站得住，
问的是 `deadWrite`）。五处表因此是六处——它不是 pass，却和 pass 一样地在回答「这个节点要不要跑」。

**三、`writings_extract.cpp#select:235` 不是死写问题**：它问的是「谁写过这个位置，所以它的值没有唯一书写」
（`writtenInPlace`），答案决定的是**抽取取哪一个书写**，与「这次写要不要执行」无关——一条死写的抽取
问题在它被删之前就已经答完了。保留这句判定，免得下一个人把它第五次算进来。

## 第 1 步的读数（`writeIsRooted` 沿 GATE 向前走）

- 判据只改这一边之后，全量 `-Changed -ChangedBase bb00e95c8`（41 计划）：**12278 / 12244 过 / 2 红**（两条 onnx）、
  24 已知红、8 skip；`feat.linear.var_reference` **259/259 绿**，`dead_write.opt.still_there` **仍 `dead=1`**，
  `dead_write.direct.answer` 仍 `dead=1`，墙两条仍 `41\n41\n`。
- **反证（临时把删除接回来、不提交）**：判据这一边单独修好之后，删除仍然不安全——同一计划 **70 红**，
  其中 `var_tensor.opt` 打出 `0.0 | 0.0 0.0 | 0.0 0.0`（张量格子的内容整体丢了），`var_rule_subpasses.opt`
  打出旧值。变成 70 而不是上一轮的 21，是因为这一版**没有**「格子有没有别的读者」那条：张量格子由后续
  算子读，`isRead` 只看写节点自己的值读者，于是判成死写。
- 所以两半边是独立的，缺一不可：**位置连着根**（本步，修标量 `var` 形参的旧值）、**格子有没有别的读者**
  （保护张量格子），而后者必须只数**活着的**读者（否则正例又翻成 `dead=0`、边界检查拒图）。
- 上一页写的「参考 15 行、实际 14 行」是错的，逐字是**三个值变旧**：期望
  `… 11 | 9 | [1, 2, 3, 4] 4 | 15 | 2 | 7 | 0 1`，实际 `… 11 | 3 | [1, 2, 3, 4] 4 | 3 | 2 | 0 | 0 1`。

## 默认建议（若不定）

**接到既有的活节点集合（`liveDraftNodes`），先只改 `writeIsRooted` 那一处，其余三处不动。** 理由是它单独
可测、单独可回退，而且它正是上一轮 21 条红的直接原因。
