# 判据说死了，删除为什么不敢动

问题：`dead_write.opt.still_there` 在接上活集合前后都是 `dead=1`（判据一直判它死），但清扫删掉它之后，
进程被自己的边界检查拒了：

```
graph '__root__' has effect node 11 (':op/assn_l') with neither a read of its value nor a place in its chain
```

这一页回答三件事：那个检查对 `assn_l` 返回什么、依据是什么；那条效应删掉之后去了哪里；以及在什么条件下
可以证明它「已经被替代」。

## 一、`nodeRunsForItsEffect` 对 `:op/assn_l` 返回什么

`src/execute/pass.cpp:2446` 的走法只有四步，最后一步才涉及死写（`:2469`）：

1. `nodeHasEffect(graph, ref)` 为假 ⇒ 不是效应，不归这个检查管；
2. `resourcesOf(...).external` 为真 ⇒ 外部效应（stdout/文件/时钟/环境/生成器），**必须保留**；
3. 有任何 `Ambient` 资源 ⇒ 同上，**必须保留**；
4. 没有任何写访问 ⇒ 纯读，不归这个检查管；
5. 否则**完全交给判据**：`return !camel::runtime::deadWrite(graph, ref);`

所以对 `assn_l`（写的是 `var` 的格子，属于程序自己拥有的状态，`:op/assn_l` 的第 2、3 步都不命中），
返回值就是 `!deadWrite(graph, ref)`。**它自己不含任何关于「这个节点该不该留」的独立主张**；它保守的
地方不在这函数里，而在它调的那个判据上——而这个边界检查跑在**冻结图**上：

- `deadWrite(const GCGraph&)` 走 `FrozenGraphView`（`src/runtime/draft_writes.cpp:264`）；
- `FrozenGraphView::isRead`（`:299`）**任何普通/`with` 用户都算读者**，理由写在代码里：冻结图没有
  活集合（`liveNodeCount` 数的是 `kind != Dead`，那是 payload 大小，不是程序还能看见什么）。

于是链条是：`assn_l` 的格子在**启动时**被一个没人调用的闭包定义读着（`kind != Dead`，但在活集合之外）
⇒ 冻结图判据说「有人在读，不是死写」⇒ `nodeRunsForItsEffect` 说「它为效应而运行」⇒
`effectStandsOnSomething`（`:1960`）看它：没有普通/`with` 用户、不是 exit/output/return、没有 sync 或
control 边 ⇒ **它站在空气上** ⇒ 拒图。

**换句话说：拒图的不是「删除太保守」，是「冻结图的判据认不出那个读者已经没人能调到」。**

## 二、那条效应去了哪里

`assn_l` 写的是程序自己的格子，不是环境。「删掉它会丢掉效应」这个担心要成立，必须有一个**程序还能看见
的**读者读到那个格子；而按第一问，唯一还「读」它的那个闭包定义**没有任何节点调用**（`isUnreadClosure`
判的正是这个：`Func` 且未被导出、两侧没有 ctrl/order/sync 边）。所以：

- 它写的两个版本元组，没有任何**可运行的**节点会读；
- 删掉它之后，程序的可见行为不变——效应不是「消失」，是**本来就没有观察者**；
- 这正是原则 3.1 的话：一个效应有没有用，在构图时就定了；没有用的效应不是需要保留的东西。

**不是 2 的后者（效应会消失），是 2 的前者（没有任何观察者）。** 但这句话今天**只在草稿侧能被证明**。

## 三、结论：今天删不掉它是对的，缺口是「冻结图没有活集合」

判据要回答的是「这个格子的读者里，有没有程序还能看见的」，而这条检查跑在冻结图上，冻结图侧没有这个
集合。**所以今天拒绝它是对的：没有人能证明那个读者不存在**——唯一能证明的机器（`liveNodesOf`）只认识
草稿。

要证明「效应已被替代」，条件写清楚就是：

> 该写不是任何根（`writeIsRooted` 为假），**且**读它的节点全部落在冻结图的活集合之外。

这条判据今天对草稿可判、对冻结图不可判。**把它变可判的最小改动是**：在 `runtime` 里补
`liveNodesOf(const GCGraph&)`（与草稿那份同一走法：从 entry/exit/output/return、norm/with 端口、
closure 节点出发，沿 norm/with/control/sync 输入向后可达，循环端口与分支 join 一并算），
让 `FrozenGraphView::isRead` 用它——这与我上一轮刻意留下的那半张网正好接上，而且现在它**有了调用者**
（就是这条边界检查）。

补上之前，正确结论是：**`assn_l` 删不掉，因为还没有人能证明读它的人不存在。**
补上之后，如果 `still_there` 翻 `.gone`，那就说明这条链通了；如果仍然 `dead=1` 且仍被拒，那说明还有一个
我不知道的读者，届时再查。