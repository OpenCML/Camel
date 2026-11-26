# `replace` 一个锚点，改接的是「它交出的那个节点」的读者

2026-11-26，`lane/editview-d`，任务 `task-24`，裁定 D-1126-114 点 (3)。

## 一句话

`replace(exit(d), v)` 要改的不是 exit 的输入边，而是**锚点交出的那个节点的读者**。写反了不会崩，会给出一个看起来对、运行时是垃圾的值。

## 事实一：函数返回值不是从 exit 的输入读的

第一版实现：

```cpp
// 锚点走的这路：把它交出的那个值换成 to
draft.replaceInput(camel::runtime::DraftEdgeKind::Data, fromRef, handed[0], toRef);
```

`replace` 之后读回来一切「对」：`inputs(exit(d))[0] == seven`、`old_alive=true`、`muls=1`，断言全绿——除了程序本身：

```
[run] the edited function:
first
130215774782456
```

`130215774782456` 是一个未初始化槽。也就是说：**exit 的输入边不是运行期取返回值的路**。frame 取函数结果时读的是返回节点实体本身，`exit` 这个 gate 只是一条等待链上的汇合点。

## 事实二：哪个节点被交出，是一件「读者」的事

设计原则 3.1：图把某个节点交出去，这件事是由**谁读它**指定的，不是由 exit 读取什么指定的。读者至少两处：

* frame 取返回值那条（运行期真正被读的）；
* exit gate 等着这个值的那条。

所以正确的改法是改接**读者**，而读者挂在「它交出的那个节点」上，不是挂在锚点上：

```cpp
const std::vector<gc_node_ref_t> handed(
    draft.normInputsOf(fromRef).begin(), draft.normInputsOf(fromRef).end());
// ...
draft.replaceAllValueUses(handed[0], toRef);   // handed[0] 是锚点交出的那个节点
```

同样的读法立刻变成：

```
[before] handed=:op/mul_l muls=1
[after] anchor_alive=true old_alive=true old_users=0 handed= muls=1
[committed] muls=1
[run] the edited function:
first
7
```

`old_users=0` 才是「读者改接了」这件事的读数；`handed=` 空掉是因为 exit gate 本来也是旧值的读者之一，被一起改接了。

## 为什么不是「把 exit 擦掉、用 v 顶上」

那会删掉锚点（`erase` 的活），而且锚点是图的，不是值的：删了它，图就没有「从哪儿交出值」这件事了。裁定要求的两件事——旧节点留着、删锚点仍拒——都指向同一个答案：**改接读者，不删节点**。

## 下一个人要记住的一句

写 `replace` 的锚点路径时，先问「谁在读这个节点」，不要问「锚点读了什么」。前者是运行期的真相，后者只是边上的一条记录。

## 相关读数

* `test/cases/modules/graph_function/replace_exit.cml`，`test/plans/feat/modules/graph_edits.plan.toml` 的 `replace_the_exit_points_the_graph_at_another_value`
* 拒绝侧：`test/cases/modules/graph_function/erase_anchor_refused.cml`（`erase` 仍报 "the node anchors its graph …"）