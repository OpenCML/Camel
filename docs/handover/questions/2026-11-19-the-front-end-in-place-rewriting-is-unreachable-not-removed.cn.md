# 前端那条原地改写：它已经不可达，但还在

**日期**：2026-11-19
**提出者**：车道 I（`lane-s3b-inplace`，基线 `91f708389`）
**状态**：待裁（本页写死一条默认）
**触发**：作者 2026-11-18 的答复（`docs/handover/reviews/2026-11-18-an-in-place-call-now-builds-as-ssa-rebinding.cn.md`）把「删掉 `Builder::inPlaceRebindingOf` 与 `Builder::inPlaceCalls_`」记在 S3b 名下；而 S3b 这次的读数由执行计划一侧给出，两件事落在同一个 commit 上就分不清谁动了哪条数。

## 1. 现象

`src/compile/gir/builder_operators.cpp:2212`：

```cpp
if (unusedStatement) {
    if (node_handle_t rebound = inPlaceRebindingOf(gct)) { ... }
}
```

它只在这条赋值语句的**值被丢弃**时才被问到；而在此之前 `ssaAssignmentOf`（同文件 `:2194`，S1 `1e8e390b6`「同区域的局部 `var` 是值不是单元」）已经把同一条 `x = e` 认领了。于是：

- 这条路由在任何程序上都不再运行；
- `inPlaceCalls_` 在整棵树里为空，它在 `:3365`、`:4140` 的两处「把算子换成原地形式」也就没有输入；
- `sort_rebinding.cml:26:12` 那个 `by-count` **不是它给的**：那是 `var` 形参的单元，由 `pathAssignmentOf` 的 cell 分支写成 `:var/take` + `array:sort_l_in_place` + `:op/assn_owned`（同名 fig 里能看到这三个节点）。删掉 `inPlaceRebindingOf` 不会动它。

**一句**：这条机制没有在决定任何东西，它只是还在。

## 2. 两条读数（同一棵树，S3b 前后）

| 程序 | S3b 之前 | S3b 之后 |
| --- | --- | --- |
| `test/cases/linear/byref/element_assigned_append.cml` | `:11:18 append copied`，main `updates 1 copied 1` | `:11:18 append in-place`，main `updates 1 in-place 1` |
| `test/cases/modules/stdlib/array_sort_kernels.cml` | main `updates 10 copied 10` | main `updates 10 in-place 4 copied 6`（那一行 `in-place 0 by-count 0` 的旧读数不在本页） |

这两行都由执行计划（`ExecPlan::storageReuseAt` + `operatorUriFor`）给出，与前端那条改写无关——这正是「就地是执行计划的复用判定，不是前端按语句形状认领」（`docs/handover/questions/2026-11-19-in-place-the-two-orders-cannot-both-hold.cn.md` 的默认甲）落到读数上的样子。

## 3. 问题

作者原话把它随 S3b 删除。它今天不可达，删它不改变任何读数（因为它不参与决定），但它也不该以「已删」的样子留在树里。要现在删，还是等 cell 时代（S4b/S7）一并删？

- **(甲)** 本轮保留，改由一次**纯删除** commit 收口（放在 S4b/S7 收 cell 的时候）。
  代价：树里多留一个不可达机制；`docs/handover/reviews/2026-11-18-...` 把它记在 S3b 名下的那句话，本轮不兑现（需要在这一页留档，说明为什么）。
- **(乙)** 本轮就删。
  代价：`inPlaceRebindingOf` 的定义（`:1301` 起、到 `:1500` 附近）与 `inPlaceCalls_` 的 7 处使用（`:1441`、`:1661`、`:2213`、`:2230–2231`、`:3365`、`:4140`）要一起删，`builder.h` 两处声明（`:3574`、`:3588`）跟着删；同一个 commit 里既有「计划开始选原地形式」又有「删掉旧机制」，任何一条读数动了都分不清是谁的。

## 4. 默认（写死：甲）

**默认甲。** 理由：S3b 是**第一次**让 `in-place`/`by-count` 不为 0 的那一版，读数要从一棵只差这一处逻辑的树上读；纯删除和它混在一个 commit 里，等于把「谁让它变绿」这件事交给人去猜。它既然不可达，就不会与计划同时决定——而这句话是可以被下一版复核的：`inPlaceCalls_` 在整棵树的每个程序上都是空的（`:3365`/`:4140` 是它唯一的两个消费者）。

**若作者选乙**，本页就是那次 pure-delete 的依据，且删除后 `element_assigned_append.in_place_form` 那一条不会因此变红——它现在读的是 `camel inspect --copies` 的判词（作者 11-18 的答复要求的读法），而那条判词由执行计划给出，与本节这条前端路由无关。