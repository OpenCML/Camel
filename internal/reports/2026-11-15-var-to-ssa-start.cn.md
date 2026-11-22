# 新排第 1 项：`var` 一律化成 SSA —— 起步报告

日期 2026-11-15。分支 `lane-varscope`，基线 `76071f818`（`origin/develop` `10f959a76` 并入后）。

## 0. 这一笔之前的状态（已落）

| 提交 | 内容 |
|---|---|
| `bd4404d2a` | 代价清单页 `internal/reports/2026-11-15-var-is-a-name-scope.cn.md`（262 行，7 个数、C1–C4 切点、扫描规则）；此前只在工作树里，`develop` 上查不到 |
| `96d8c9ea3` | C1 部分迁移：12 个用例 + `stdlib/perf.cml`，按新排期顺延 |
| `76071f818` | 并入 `origin/develop`（含 §3.5 两条 11-15 新条款、同类型句柄名的书写顺序） |

C1 已改的 12 个用例：`default, fvm, jit, nvm, prl, opt-fvm, opt::inline fvm` 七条管线、
84 次比较，82 次逐字节相同，2 次只差诊断引用的源码位置。没改的那些按四类留在树里，理由写在
`96d8c9ea3` 的提交正文（拒绝对象 6 个、断言本身就是单元 9 个、progen 生成 7 个、`stdlib/stream.cml#tee` 待作者裁定）。

## 1. 基线读数（`76071f818f`，clean，指纹 `f94013c002f2`）

| 计划 | 总数 | 通过 | 失败 | 跳过 | 已知失败 |
|---|---|---|---|---|---|
| `test/plans/feat/compile` | 2464 | 2454 | 10（全部 `compile.stream.*`） | 0 | 16 |
| `test/plans/feat/opt` | 1412 | 1412 | 0 | 0 | 0 |
| `test/plans/feat/parse` | 115 | 115 | 0 | 0 | 0 |

`compile.stream.*` 是既有永久红（生成器形状、`generator_loop_two_states`），与 `var` 无关。

## 2. 现状：哪些 `var` 已经是 SSA，哪些还带着单元

建图器**已经**有一条"不逃逸的 `var` 化成 SSA"的路：`Builder::collapseNonEscapingVariableReads`
（`src/compile/gir/builder_expr.cpp:1780-1929`）。它按单元分组的读，把每个读的用户接到"最近
一次写的值"上（写节点的第二操作数），再删掉读节点。留下单元的条件写在 `builder.h:999-1013`：
被闭包捕获（`captures.cpp:123`）、作为 `var` 实参交出（`builder_calls.cpp:933`）、本图的 `var`
形参（`nodeKindOf(cell) == Port`）、或者单元有它没记录的用途（`:var/take`、调用、捕获口）。

于是"一律化成 SSA"要补的只有两类：**分支里写**、**循环体里写**。实测（`std::gir`）：

```camel
// q1
func main(c: bool): int sync { var x = 0; if c then { x = 1 }; println(x); return 0 }
```
```
N03 :var/new (var int)     N04 JOIN (void)      N07 :op/assn_l (var int)
N08 GATE                   N0B :var/get (int)   N0A :io/println
```
```camel
// q2
func main(): int sync { var x = 0; let _ = loop i = 0 while i < 3 do sync { x = x + 1; i + 1 }; println(x); return 0 }
```
```
N02 :var/new   N08 :var/get   N09 :op/assn_l   N0D LOOP while (int)   N0F :var/get
```

两个形状都还在用单元。原因在 `builder_expr.cpp:1877-1901`：折叠只承认"写节点是 Oper 且有
两个规范输入"的那种版本；而这两处的版本来源是 **JOIN**（分支合并点，`builder_control.cpp:2636`
一带）和 **LOOP**（循环携带状态），不是 Oper，于是 `continue`，读保留单元。另外分支这条还有
第二道门：JOIN 把单元自己当成一个输入（`normUsersOf(cell)` 里有 JOIN，`known=false`，
`builder_expr.cpp:1855-1869`），所以即使版本来源认出来了，单元也不会被删。

裁定原话给的两条正是这两处的目标形状：
- 分支：「`if c then { x = 1 }` 即 `x = if c then 1 else x`」——合并点要合**值**，不是单元；
- 循环：「循环体里给外层 `var` 赋值 = 循环携带状态」。

## 3. 三步走（每步一个提交、一组读数）

1. **`var` 声明/赋值 → SSA**（不含分支、循环）：确认线性写读已经全走 SSA，补上"版本来源是
   非单元类型的 JOIN"这一种，让 `q1` 的 `:var/get` 消失。读数：`q1` 的 `:var/new`/`:var/get`
   计数 1/1 → 0/0，`feat/compile` 2104 项不变。
2. **分支合并点**：臂里的写成为臂的结果（`readArmCellValue` 已经在做值化，缺的是让 JOIN
   不再把单元当输入、以及后续读以 JOIN 为版本）。验收 `q1` 的图与
   `let x = if c then 1 else 0` 的图同形；`feat/compile`、`feat/opt` 不比基线低。
3. **循环携带状态**：体里的写成为 LOOP 的 state，循环后的读读 LOOP 的 state 输出。验收
   `q2` 的 `:var/new`/`:var/get` 归零，`loop_collect` 那组计划仍绿。

## 4. 对 `stdlib/graph.cml`（作者新加的 9.5 项）的影响，先说一句

- `name(g)`：不变。这一步不改任何图的名字。
- `nodes(g)` 的**条数会少**：分支/循环里写的 `var` 不再有 `:var/new` 与 `:var/get`，`q1` 少 2
  个节点、`q2` 少 3 个。顺序本身（哪些节点在前）不变，但下标/序号会整体前移，任何按序号断言的
  用例都要改。
- `:op/assn*` 那 47 个执行器里，分支/循环里写的那些在这两步之后不再出现在图上（线性写读仍然
  保留，因为原则 9.7 的"写要留在时间线上"没撤）。