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

## 2a 的读数：清扫扩到 `Func` 之后，它一个都没收走

`isUnreadClosure`（`runtime/draft.h:1157` 旁，实现 `src/runtime/graph.cpp:1638` 上方）与 `screen.cpp` 的
`case GCNodeKind::Func` 都写好了，`feat.opt.effects_kept` 184 条里 180 绿、4 红全部是我加的探针（已删）。
但把清扫的删除按种类数出来（`test::deadwrite` 现在多打一行 `SWEEP func=… call=… gate=… oper=…`）：

```
SWEEP func=0 call=0 gate=0 oper=0
```

**一个都没收走，连 `gate`/`oper` 都是 0。** 原因不是判据写错，而是**这个图根本没被清扫**：
`screen.cpp#mayRemoveDead` 的循环先看 `valueUsers[ref]`，`p = (1, h)` 里元组读着 `h`，所以 `h` 不是
「没人读」，screen 直接 `continue`，图被判成「这里没有可删的东西」。也就是说：

> **让格子续命的那个读者不是闭包自己，是那个还提着它的元组。** `p.0` 用了元组，元组用了 `(1, h)` 的
> 第二个成员，而没有任何一步把没人读的元组成员摘掉——所以闭包在、格子被读、写永远是「活着的」。

要收走闭包，得先让 **`p.1`（元组里没人读的那个成员）** 被摘掉；那是 `:op/tuple` 成员的抽取问题，
不是清扫的种类问题。**所以 2a 按作者的说法做完了，但它的验收（边界那条痕迹消失）不可能靠 2a 达成**：
痕迹的根源在元组，不在清扫。下一步该先补「元组里没人读的成员可以走」，再谈 `Func` 的清扫。

## `Call` 的可移除性：签名不在手里（停手，问）

作者的判据是「签名上已有的 `pure` 与故障表」。查过一遍，**`Call` 节点手里没有这个签名**：

- `Call` 的载荷是 `GCCallBody`（`include/camel/runtime/graph.h:466`）——只有 `callKind` 和三个保留字段，
  没有 `pure`、没有故障位；`DraftNodeHeader::runtimeFlags`（`draft.h:136`）也没有这两个含义。
- `FunctionType`（`include/camel/core/type/composite/func.h`）只有参数与结果，**不带纯度或故障声明**。
- 草稿能给的只有 `calleeIdOf(id)` / `calleeGraphOf(id)`（`draft.h:646`），即被调图本身；而「这个图有没有
  写、会不会出故障」是 `effectSummaryOf(const Program &, const GCGraph *)`（`runtime/effects.h:44`），
  **需要一个 `Program`**，清扫手里只有 `GraphTransaction`（`graph_txn.h:33`，里面只有草稿）。

所以按作者的话做不到「不碰签名」；**加参数就是加第二份真相**，而这正是上一轮被否掉的做法。停在这里问：

> **`Call` 的可移除性判据该由谁供给？** 三个方向：(a) 让 `Call` 节点在构建时带上足迹位（`runtimeFlags`
> 的空位正好够），由构建者填、判据只读；(b) 让 `GraphTransaction` 能看见它所属的 `Program`（一个惰性指针，
> 不改任何判据签名）；(c) 等 R3 的 `NodeEffect` 落地，`Call` 的可移除性从那里取。默认建议 (b)：
> 一份真相在 `Program` 里，交易只是多一只眼睛，判据仍然只在一处。

## 下一件（未做）：「一个没人读的元组成员应当可以被摘掉」

独立缺陷，不叫「让闭包能被收走」。现在的证据链：`p = (1, h)` 的元组读着 `h` → 元组被 `p.0` 用 →
screen 认为没有可删节点 → 清扫一次都不跑。**摘元组成员要面对的难点不是判据，是编号**：删掉第 1 个成员，
后面成员的索引都要前移，所有读它的 `Accs`（`GCAccsBody::value`）都要改写；而在 `sync`/顺序边存在的图上，
「没人读」的定义又要回到本页开头那个「活着的读者」。所以这件要先定「摘成员时索引重编号」的规则，
再谈判据——它是 `writings_extract.cpp` 的事，不是清扫的事。

## 前置任务：摘掉一个没人读的元组成员

### 今天的读数：提着它的是元组，不是闭包

`let p = (1, h)` 在图上是一个 **`Fill` 节点**（`GCNodeKind::Fill`，载荷 `GCFillBody`：
`slotCount` 加一个变长的 `slots()`，`include/camel/runtime/graph.h:500`），**成员就是它的普通输入，按顺序**。
`p.0` 读它是 **`Accs` 节点**（`GCAccsKind::TupleIndex`，`value` 是成员号，`graph.h:484`）。

所以：`Fill` 的第二个输入就是 `h` ⇒ **闭包有一个用户** ⇒ `screen.cpp#mayRemoveDead` 认为这里没有可删节点 ⇒
**清扫一次都不跑**（读数 `SWEEP func=0 call=0 gate=0 oper=0`）。**格子续命靠的是元组，不是闭包。**
而今天**没有任何规则**说「元组里没人读的成员可以摘掉」——`Fill` 的输入是它的一部分，没人问过其中哪个没人读。

### 判据该落在哪一层

三层，各管一件事，**不要把三件事塞进一处**：

1. **判据**（「这个成员有没有**活**读者」「这个元组有没有整体逃逸」）落在 `runtime`，就在 `liveNodesOf`
   旁边：`tupleMemberIsUnread(draft, fill, index)`。理由和 `deadWrite` 一样——它问的是活集合，而活集合在
   `runtime`。
2. **改图**落在 `generic` 的那一次重写里（常量折叠 `p.0` 已经在 `src/passes/opt/generic/generic.cpp:1120-1170`）：
   摘输入、重建 `GCFillBody` 的载荷（变长，`slotCount` 少一）、把 `Fill` 的类型换成少一位的元组类型、重推类型。
3. **多份写**（同一个值有几个 `WritingUnion`）是 `writings_extract.cpp#select` 的事：**先抽取，再重写**。
   **不要把「摘成员」塞进抽取**——抽取决定「取哪一份写」，摘成员改的是「这一份写里有几个成员」，
   两者混在一起会让 `select` 同时背两个语义（这也是作者对 `select` 的判断：它不是死写问题）。

### 编号前移要改几处

12 处提到 `GCAccsKind::TupleIndex`，其中**把 `body->value` 当成员号读的有 8 处**：
`nodevm.cpp:545`、`taskflow.cpp:2022`、`taskflow.cpp:2382`、`fastvm/compile.cpp:362`、`macro/eval.cpp:871`、
`generic.cpp:1135`（常量折叠）、`draft_types.cpp:133`（成员类型）、`graph_edges.cpp:759`（类型检查）；
另有 3 处**写**它：`draft_construct.cpp:192/196`、`draft_inline.cpp:1217`。

但**这 8 处一行都不用改**：成员号住在 `Accs` 节点的载荷里，编号前移是**改图**（逐个 `Accs` 改写 `value`），
不是改这 8 处代码。它们在这里的价值是**证明「成员号只有载荷一个来源」**——所以重编号必须走一次对整个图的
改写，而不是偷偷在某个读者里做偏移。

### 有没有「不需重编号」的部分情形：有，而且手上这条就是

`h` 是 `(1, h)` 的**最后一个**成员。**摘最后一个成员不需要任何重编号**：没有更高位的成员要前移，所有 `Accs`
的成员号都还指向原来那个成员。所以第一步不是「重编号规则」，而是：

> 判据：`Fill` 是元组、最后一个输入没有被活节点读、其它用户全是 `TupleIndex < 原 slotCount - 1` 的 `Accs`、
> 且这个 `Fill` 不是任何根 ⇒ 摘掉最后一个成员。

这条能拿到**第一份真读数**（摘完 `h` 变成没人读 ⇒ `isUnreadClosure` 收走 ⇒ 格子没有活读者 ⇒ `dead=1`
的写成真死写 ⇒ 才轮到边界检查那次「拒不拒」的实测）。**重编号留给作者裁**，不在这一步里混。
