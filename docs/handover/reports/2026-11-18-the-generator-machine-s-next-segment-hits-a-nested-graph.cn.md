# 生成器机器：下一个 segment 的那堵墙，以及靶子的三处读数

**日期**：2026-11-18
**车道**：`lane-loopblk`（`aed87cb5c` 之后）
**基线**：`17b26ff45`（develop），干净树
**触发**：`b62b76de5`（与 `9d54a5b9a` 两条更正）把「把生成器机器写进 GIR」变成了可写的规格；本页记的是照规格动笔时撞到的第一处不成立的假设。

## 1. 尺子：`buildGeneratorBody` 今天**不可达**，所以量它必须先开一扇临时门

```
`gct/builder.cpp`（`visitFunc` 的体）：
  `writesOwnYield(bodyAst) ? visitGeneratorBody(ast, funcType) : visitStmtBlock(bodyAst)`
⇒ 所有 `yield` 在 GIR 之前就被 GCT 机器吃掉，`builder_generator.cpp` 的分支**零调用**
```

**临时仪表（已复原，未提交）**：把那一行改成恒 `visitStmtBlock(bodyAst)`
⇒ 实测：`stream` 计划 **147 → 75 pass / 72 fail**，每一条都停在 `builder_generator.cpp`
自己的拒绝上（`the generator body of 'int..' reached the translation …`）。
**这就是「seam 而不是 behaviour」的读数**，也是「④ 与靶子比」唯一能动的路。

## 2. 成立的部分（已提交，`aed87cb5c`，`+103 / -3`）

轮状态初值、两个 region（**条件区先建**）、两个 region 的端口（`setNodeRegion` +
`setRegionPorts` **和** `setRegionParams`）、条件 `__eq__(state.1, 0)` 走 **group 调用**。
`stream` 计划 147/147、六计划 `[SUMMARY] total: 5385, pass: 5363, fail: 0, skip: 2, known failures: 20`，
与基线逐字相同。

## 3. 那堵「墙」**量了，不是墙**：答案是 (a)，互递归在 GIR 层可行

```
问题：一张 `makeFunc("")` 合成的 FUNC，在 `currGraph_` 是 step 图时被 `visit()`，
      它对 `__gen_step__` 的 DRef 解析成什么？
⇒ 探针（临时，已复原）：在 `buildGeneratorBody` 里、step 图仍是 `currGraph_` 时，
   ① 读 `graphsAt("__gen_step__")`，与 `stepGraph.get()` 比指针；
   ② 造 `makeFunc("")` + `makeType(stepType)` + `return __gen_step__(__gen_state__)`，`visit()` 它。

实测（`generators.cml` 每个生成器函数同一读数）：
  ① **(a) the graph being built** —— `graphsAt` 回来的就是 `stepGraph`，指针相同
     判据：`enterScope(name=…)` 在建图时立刻 `insertGraph(name, found)`（`builder.cpp#Builder::enterScope`），
     所以「名字」在**建完之前**就指向这张图。
  ② **`visit()` 成功**，返回的节点类型是 `(__gen_state__: (int,)) => (int, int..)?` —— 正是 step 的签名
⇒ **⇒ 嵌套图可以调回外层正在建的图，⑥ 不必手建：照 GCT 树的形状（`streamValue()`）写即可。**

一处更正要记住：探针**第一版报了 (c) `visit threw BuildAbortException`**，
原因是我给 `__gen_step__` 传了零个实参（它要一个）。**那是 arity 拒绝，不是解析失败。**
⇒ **「探针自己写错了」和「机制不存在」在读数上长得一样，区别只在有没有补上那个实参。**
   不含这一步，这一页会把「可行」写成「有墙」。

## 4. 三处靶子读数（比规格更细，或与规格不同）

```
① 区域编号（`std::jgir` 的 `regions`）：`id 1 = loop_cond`、`id 2 = loop_body`、`id 3..8` = 六个 arm
   ⇒ 条件区先建这条成立；但 `9d54a5b9a` 的注释写「arm 区是 g4r2/g4r4/g4r6」——实测是 **3/4/5/6/7/8**，
     2 是 body。注释那三个编号是别的树的读数。
② `:op/eq_l` 恰好 **4 个**（arm 区 3 + 条件区 1），而且**条件区那个在 region 1**，喂它的是 `DATA int` 字面量
   ⇒ 「两处同一个算子、都写 group 调用」这条实测成立。
③ 边数：g4 按 jgir 的 `inputs` 数组数是 **78 节点 / 115 边**（规格写 116）；
   `std::stats` 报整个程序 `edges: 163`（7 张图），**不能用它拆出 g4 的 116**
   ⇒ 「116」这个数**不是用 jgir 能量出来的**，谁写它都要说是哪把尺子。std::stats 报全程序 163，
     也拆不出它 ⇒ **「116 从哪来」没有被查到；查清之前任何规格不许再用它当判据**，否则它会在下一份规格里复活。
```

## 5. 一处与规格形状不同、但不是缺陷

```
靶子 g4 的 `var s` 是 step 图的**捕获端口**（contextPorts: n16，类型 `var string`）：
  GCT 机器把体里的局部提升成格子、由 stream value 捕获。
GIR 机器按裁定应当是「局部就以值出生、类型是它第一个写入的类型」⇒ 那里**没有捕获端口**
⇒ ⇒ 这条差异是**目标**，不是偏差；但它会让 g4 的节点数与捕获边数与靶子不同，**④ 报差时要认它**。
```
## 6. ⑥ 的第一个形状类不该是 `generators_cell_type.cml`

```
`generators_cell_type.cml` 的体：`var s = "one"; yield s; s = "two"; yield s`
⇒ `s` **跨过了两个 `yield`** ⇒ 它是「格子在两段之间活着」这条，正落在本 segment **不许**碰的
  `T?` 槽那一块（文件头写死「状态只载 `pc`」）。
⇒ **而 `test/cases/compile/stream/generators.cml` 里 `quad()` 的体是两个纯 `yield`，没有局部：**
     `func quad(): (int, int).. sync { yield (1, 2); yield (3, 4) }`
⇒ **那才是「能出一张图的最小路径」**；⑥ 的第一个形状类应当按它划线，
   再按 `count()`（局部跨 yield）→ `sign()`（臂里的 yield）→ `reading()`（闭包）逐类往上铺。
   **先拿 `generators_cell_type.cml` 当第一类，会把「最难的形状」当「第一个读数」。**

实测（`generators.cml`，`std::jgir canonical=true`，25 张图）：
  g7 = 97 节点 / 148 边、g9 = 70/94、g11 = 113/212、g13 = 80/119 —— 四台生成器机器的读数，
  与靶子同一形状的机器要拿这几行去比，**不是 `quad` 单独一张**（`quad` 是 g13 或 g9 之一，
  下次开工第一件事是把它钉死，别猜）。
