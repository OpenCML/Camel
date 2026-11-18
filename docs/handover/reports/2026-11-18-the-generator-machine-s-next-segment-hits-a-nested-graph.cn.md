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

## 3. 墙：`yield` 的剩余流是一个**调回自己所在图的嵌套图**

```
靶子 g4（`__gen_step__`）n20 = DATA callee g5；g5 的 n4 = FUNC callee g4   ← 互递归
⇒ 每个 `yield` 的 answer 里那个 `T..` 是 `gct/builder.cpp` `streamValue()` 造的
   `makeFunc("")` + `return __gen_step__(state)`，由 `visitFunc` 建成一张**新图**
⇒ GIR 层建图只有一个入口 `enterScope()`（换 `currGraph_`），而它建的是**子图**；
   子图要引用**外层正在建的那张图**（g4），GIR 层没有写下来的机制 ——
   GCT 树能行，是因为 `makeDRef(stepName)` 在**整棵树建完之后**才解析。
```

**⇒ 这不是「还没写」，是「按 GCT 树的形状照抄会退回『状态型在 arms 之前定死』」，而手建图要先回答「子图何时能引用父图」。**
**⇒ 动笔前该先量这一处**：一张 `makeFunc("")` 合成的 FUNC 在 `currGraph_` 是 step 图时被 `visit()`，DRef 到 step 的名字解析成什么。**这是下一段的第一个读数，不是第 800 行。**

## 4. 三处靶子读数（比规格更细，或与规格不同）

```
① 区域编号（`std::jgir` 的 `regions`）：`id 1 = loop_cond`、`id 2 = loop_body`、`id 3..8` = 六个 arm
   ⇒ 条件区先建这条成立；但 `9d54a5b9a` 的注释写「arm 区是 g4r2/g4r4/g4r6」——实测是 **3/4/5/6/7/8**，
     2 是 body。注释那三个编号是别的树的读数。
② `:op/eq_l` 恰好 **4 个**（arm 区 3 + 条件区 1），而且**条件区那个在 region 1**，喂它的是 `DATA int` 字面量
   ⇒ 「两处同一个算子、都写 group 调用」这条实测成立。
③ 边数：g4 按 jgir 的 `inputs` 数组数是 **78 节点 / 115 边**（规格写 116）；
   `std::stats` 报整个程序 `edges: 163`（7 张图），**不能用它拆出 g4 的 116**
   ⇒ 「116」这个数**不是用 jgir 能量出来的**，谁写它都要说是哪把尺子。
```

## 5. 一处与规格形状不同、但不是缺陷

```
靶子 g4 的 `var s` 是 step 图的**捕获端口**（contextPorts: n16，类型 `var string`）：
  GCT 机器把体里的局部提升成格子、由 stream value 捕获。
GIR 机器按裁定应当是「局部就以值出生、类型是它第一个写入的类型」⇒ 那里**没有捕获端口**
⇒ ⇒ 这条差异是**目标**，不是偏差；但它会让 g4 的节点数与捕获边数与靶子不同，**④ 报差时要认它**。
```