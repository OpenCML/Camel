# 第 ②a 步：落点收窄到一个函数和一对 WITH 槽

日期 2026-11-15。分支 `lane-varscope`，基线 `2e3628b10`。这一轮没有改 `src/`，但把 ②a 的
落点从"分支形状"收窄到一处，并且回答了两条要核的问题。

## 1. 版本为什么会变成合并点：一个函数

`Builder::mergeArmVariables`（`src/compile/gir/builder_control.cpp:2914-2943`）就是那个决定
"分支之后这个 `var` 的版本是合并点"的地方，逐条：

- 每个臂都记着它碰过哪些变量（`arm->touchedOrder`），以及每个变量在臂里**最后一次写**之后的位置
  （`touched.writerAfter`）；
- 有写：`writeControlLink(writerAfter, joinNode)`，然后 `recordWrite(variable, joinNode, true)`
  —— 版本直接记成合并点本身；
- 没写：`pendingUsesOf(variable).push_back({joinNode, false})` —— 该臂事后补一条使用。

这解释了 `q1` 里那条 `N07 -> N04 [CTRL]`：它是这个函数写的，不是"合并点要单元"。

## 2. 新读数：臂的值**已经**到了合并点，缺的只有"另一边的值"

`q1` 的合并点两条 WITH 边（上一轮只看了 NORM 那条，这次看全）：

```
N08 GATE (int) --WITH slot 0--> N04 JOIN      N08 的值是字面量 1，N07 用 CTRL 闸着它
N09 null (void) --WITH slot 1--> N04 JOIN     没有 else，所以 slot 1 是 null
N05 BRCH --NORM slot 0--> N04 JOIN
```

也就是说 `if c then { x = 1 }` 的"then 分支的值"已经在那里了（GATE 出口、WITH 槽 0），
`joinsNoValue` 只是把它记成"没人要"。要得到裁定原话的 `x = if c then 1 else x`，缺的是三件：

1. **另一边的值**：slot 1 现在是 `null`，要换成这个 `var` 在分支之前的版本（`q1` 里是字面量 0）；
2. **合并点的类型与版本**：现在 `value_type="void"`，要变成值类型，并让 `recordWrite` 记的是
   这个值合并点（只对"臂里真写了、且分支之后还会被读"的 `var`）；
3. **折叠认它**：`builder_expr.cpp:1877-1901` 承认"版本来源是合并点"（并且合并点的 WITH 输入都是值）。

**卡点在这里**：WITH 槽是**按臂**分的，不是**按变量**分的。一个臂里写了两个外层 `var`，这一对
槽放不下两个值。所以要么给每个被写的 `var` 各产一个值合并点（每个臂一条 GATE 出口，再一条
WITH），要么让这个合并点收一个元组。前者干净、后者省节点——这就是 ②a 只能"窄做"的原因：
不做"所有语句形分支都带值"，只对**臂里真写了外层 `var`** 的那个分支、为那个 `var` 产一个值合并点。

## 3. 两条要核的事

**(a) `git grep 'takes a var argument'` 的结果**：8 处命中，其中**只有一处是测试断言**——

```
test/plans/feat/compile/diag_json.plan.toml:199
  ['warnings_visible.cml:16:5: [Warn]: The call of \'inc: (var x: int) => int uses var x\'
     is not in a sync block, and it takes a var argument.', 'SemanticDiag::IgnoredSideEffect, code=0x03100002']
```

它断言的是**把 `var` 实参交给一个函数**那一支（`inc(c)`），不是分支里的写，**②a 不会碰它**。
其余 7 处是文档：`docs/handover/**` 4 处（含上一轮我自己那页）、`docs/tutorial/10_effects_and_order.{cn,en}.md`
各 1 处，教程那段的例子是 `func ordered()` 里 `c = 5` 这种**没人读的直线写**，也不是分支写，不变。
`src/compile/gir/builder_operators.cpp:4566` 是产生它的地方。

**(b) `warnings_visible.cml` 那条 `line 18: a branch whose arm writes the captured c`**：它的分支是
`let r = if c > 0 then sync { c += 10; return 0 } else 0`——**带值**的分支，而且臂在 `sync` 里，
写仍然留在时间线上。②a 只动"没有值"的语句形分支，所以这条不该变；如果改后它变了，说明改动越界了，
要回退。

## 4. 这一轮为什么还是没落代码

②a 的三件事里第 2 件要改 `mergeArmVariables` 的调用点并新增一个合并节点，第 3 件要动折叠的版本
判定；一次成败仍然要走 build + `q1/q2` 四后端 + 三份计划读数。这一轮剩下的预算不够走完至少两圈，
所以交的是收窄后的落点：**`builder_control.cpp:2914-2943`（版本记成合并点的地方）+
`q1` 那对 WITH 槽（`N08` 塔 0 / `N09` 塔 1）+ `builder_expr.cpp:1877-1901`（认合并点为来源）**。
下一轮从第 1 件开始：把 slot 1 的 `null` 换成分支前的版本，只对"臂里写了、分支后还会被读"的那个
`var`，先把"分支产出值合并点"单独交出来（那时折叠还没接上，`q1` 仍是单元，但合并点已经带值）。