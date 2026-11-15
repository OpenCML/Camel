# 探针确认了假设：`source` 是单元（3），`recorded` 是合并点（4），两者不是同一个节点

日期 2026-11-15。分支 `lane-varscope`，基线 `173754268`。探针已删，`src/` 与基线逐字节相同。

## 三行原文（`q1`，`var x = 0; if c then { x = 1 }; println(x)`）

```
[DBG-H] var=3 recorded=4
[DBG-I] var=3 read=13 at-read-time=3 cell=3
```

- `[DBG-H]`：`mergeArmVariables` 把**合并点（id 4）**记成了这个变量的写；
- `[DBG-I]`：而构造那个读（id 13，`println(x)` 的读）时，它看到的"最新版本"是 **id 3 —— 单元自己**；
- 所以折叠取的是"单元造出来时的值"（0）。`source` ≠ `recorded`，**假设成立**。

`[DBG-G]`（折叠决定处）在这两个程序上没打出东西：折叠根本没走到"读一个合并点"的那一段——
因为读记录的 source 是单元，折叠走的是 `source == cell` 那一支，取 `cellInputs[0]` = 初值 0。

## 为什么"记了却没看见"

`mergeArmVariables`（`builder_control.cpp:2938`）用的是
`recordWrite(variable, joinNode, /*throughCallee=*/ true)`。`recordWrite` 里 `throughCallee`
的那一支只对 `Call` 出口做"版本经由调用"的处理，但它同时**决定了有没有把出口记成这个变量的
modifier**；对一个 `Join` 出口，modifier 没有被设上（`builder_expr.cpp:637-660` 一带：
`throughCallee && nodeKindOf(exit) == Call` 之外的通用路径没有在这里补 modifier）。
读在构造时问 `valueVersionOf(variable)`，拿到 nullptr，于是退回单元。

## 修法（下一轮第一件事，一处）

②a 那个窄分支里，把合并点**同时**记成 modifier，而不是只走 `throughCallee` 那一支：
在 `mergeArmVariables` 之后（或把那里的 `true` 在该窄条件下换成会让 modifier 生效的调用），
让 `valueVersionOf` 在该读被构造前返回合并点（id 4）。判据不变：

- `q1` 四后端 stdout = 基线 `1 / 0`，且 `:var/get` = 0；
- `q3` 直线 `1`；
- **`q2` 循环必须仍是基线 `3`、`:var/get` 照旧**（循环携带状态是 ③，这一笔只许动分支）；
- `feat/parse` 115/115，并跑闭包/捕获那几份。

## 读数

- 修前：`escaping=1`；本轮 `source=3(cell) vs recorded=4(join)`——**这是值错的原因**
- `q1`/`q2`/`q3` 的 stdout 与 `:var/get`：本轮只加探针，未改行为，三个程序与基线一致
- 树：探针全部删除，`git diff --stat` 为空