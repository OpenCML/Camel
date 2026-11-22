# S1-1 点名的那个程序并不合法：要收窄的是哪一边

**日期**：2026-11-19
**提出者**：车道 S1-1（`lane-s1-1-capturewrite`，基线 `origin/develop` = `9db2972af`）
**状态**：已按 (A) 落地（用例改写成 `fold`／`let`，规则一字未动），本页记下为什么不是「收窄规则」，等作者确认口径
**触发**：0.2.45 阻断清单 S1-1 把 `test/cases/feat/mm/capacity/perm_query_types.cml` 判为「合法程序被拒」。

## 1. 事实：读数逐字

计划条目 `test/plans/feat/mm/capacity.plan.toml` 的 `capacity.perm_query_types`，`expect_exit = 0`，
主机实测（树 `9db2972af`）读到：

```
perm_query_types.cml:55:9: [Error]: 'c17' is assigned inside a closure, and a closure holds the
  value the name had when it was made, not the name. A closure holds...
perm_query_types.cml:59:9: [Error]: 'c17' is assigned inside a closure, and a closure holds the
  value the name had when it was made, not the name. A closure holds...
```

外加 `expected exit 0, got exit 1`，`L3/L5/L6/L7/L8` 五条 stdout 全部缺失。诊断是
`SemanticDiag::ClosureAssignsCapturedName`（0x03000213）。被拒的两处都在 `func e16(var c17: int[])` 里：
`55:9` 是 `c17 = concat(c17, [...])` 写在 `->foreach<(p19: int) => sync {…}>` 的元素函数里，`59:9` 是同一句
写在嵌套 `func g22` 体内。

## 2. 为什么这不是「判据判宽了」

现行规则写在 `docs/design-principles.cn.md` 3.5（198，负责人 2026-11-15），逐字：

> **闭包捕获的是值**……闭包提到外面的名字，拿到的是闭包创建时这个名字的值；**闭包不能给外面的名字赋值**
>（编译错误，诊断给三种改法：返回新值、写成循环状态或 `fold`、用 `Ref`）。

`e16` 的两处写就是「闭包给外面的名字赋值」，一个字都不差。三处独立记录也指向同一件事：

- `test/tools/progen` 生成它时（seed 3293，generator version 1）就带着这个形状；
- `internal/reports/2026-11-15-var-is-a-name-scope.cn.md` §5.2 已经把它归进「**改成 `fold`**」那一档
  （第 200 行），并在第 205 行注明「`rebinding_loop.cml` 与 `perm_query_types.cml` 写的是 `var` **形参**」；
- `internal/archive/red-baseline.cn.md:155` 与 `internal/archive/pending-work.cn.md:359` 把它登记成老账
  （「闭包写捕获」／「仍红（54，全部已被 A/B 或双侧证明为老账）」）。

⇒ 它是**用例欠一次改写**，不是编译器判宽。规则从 11-16 开始报之后它自然就红了。

## 3. 为什么「收窄到写穿闭包边界的捕获」做不到只让它过

最直接的收窄是让 `src/compile/gir/captures.cpp#Builder::requireCapturedNameWritable` 在拒之前先问
`captureByValue`：只有**按值捕获**的写才到不了（那样才配得上 197 的话），共享单元的写到得了，不该拒。
问题在于它分不开这两个程序：

| 程序 | 被写的名字 | 它是什么 | `decideCaptureByValue` 的答案 |
| --- | --- | --- | --- |
| `perm_query_types.cml` `e16` | `c17` | 函数的 `var` 形参 | 与下一行**相同** |
| `var_capture_reference.cml` `through` | `out` | 函数的 `var` 形参 | 与上一行**相同** |

`src/compile/gir/captures.cpp#Builder::decideCaptureByValue` 只看三件事：是不是模块顶层值、节点种类是不是
`Oper`、名字在程序里有没有别处的写。两行在这三件事上逐条一模一样——都是局部、都是 `var` 形参、都不是顶层值。
所以按捕获种类收窄，**要么两个一起放行、要么两个一起拒**，不存在只让 `c17` 过而 `'out'` 还拒。

而 `'out'` 那条是被钉住的：`test/plans/feat/linear/var_reference.plan.toml` 的 `var_capture.default`
期望 `exit 1` + `ClosureAssignsCapturedName`，`expect_output_contains` 里逐字有
`'out' is assigned inside a closure`。

剩下的差别只有两条，而两条都不是 197/198 的话：

- **闭包离不离开那次调用**：`through` 的闭包绑到 `let add` 并被调用，`perm_query_types` 的闭包写在
  `foreach` 的实参位置（`g22` 根本没人调用）。这正是 11-14 的规则二「不离开的闭包可按引用捕获」，
  **已被 11-15 取代**；把它放回来是改语义，不是收窄判据。
- **写的新值后面还有没有人读**：`perm_query_types` 两处写都是死写（`e16`、`g22` 都没人调用）。
  这是设计 3.5（206）「没人读的绑定不计算」的口径，也是 `f92e5246f` 明确拒绝过的方向
  （「a write to a capture's name either reaches the graph or is reported，擦掉它设计没有这个答案」）。

⇒ 「写穿闭包边界的捕获」这句话在**捕获种类**上不可实现；能做到它的只有上面两条之一，都要先改裁定。

## 4. 这一笔做了什么（(A)）

用例改写成 3.5(198) 指的第一种改法，**规则与它的诊断一个字没动**：

- `:58-62`：`foreach` 闭包里的 `c17 = …` 变成 `c17 = (…)->reduce<(q36: int[], p19: int): int[] => …， c17>`
  —— 赋值落在 `e16` 自己的体里，元素函数只返回值；
- `:65`：`g22` 里的 `c17 = concat(…)` 变成 `let n51 = concat(…)`；
- `var c17: int[]` 形参保留（`w29(a30, var c31)` 那条递归调用照旧压 `var` 形参的重载查询，
  32 MB 永久空间这个考点不受影响）；
- 用例头加了一段说明它被手改过、为什么；
- `test/plans/feat/parse/golden/ast_hashes.txt` 那一行按 `-UpdateAstHashes -AstOnly` 重录
  （`46fa40e4e6db6a4e` → `9961fcb0e2ee103b`，只动这一行）。

读数：`feat/mm/capacity` 18/18（`capacity.perm_query_types` 238.71 ms 通过）；`var_capture_reference.cml`
仍 `exit 1` + `ClosureAssignsCapturedName`；六计划与基线逐条相同。

## 5. 需要作者答的一句

**3.5(198) 的「闭包不能给外面的名字赋值」，覆盖不覆盖「写的是函数的 `var` 形参」这一形状？**

- **覆盖**（树里今天就是这么实现的，本页默认按这个做）⇒ 那么 `perm_query_types.cml` 是欠改写，
  这一笔已经还清；`var_capture_reference.cml` 的 `through` 也继续拒，一切照旧。
- **不覆盖** ⇒ 那是 197 的判据要改，而且**两个程序必须一起动**：`perm_query_types` 与
  `var_capture_reference.cml` 的 `through` 同时放行，
  `test/plans/feat/linear/var_reference.plan.toml` 里 `'out' is assigned inside a closure` 与那一条的
  `expect_exit = 1` 一起改，并且要说明按值捕获口与 `var` 形参单元从此各自算什么。
  只改一半就会变成「让一个用例绿」。

## 默认建议（写死）

**默认建议**：按 §5 的「**覆盖**」办（也是树里今天的实现）：3.5(198) 的「闭包不能给外面的名字赋值」
**覆盖**「写的是函数的 `var` 形参」这一形状，于是 `perm_query_types.cml` 是欠改写、已按 (A) 还清，
规则与诊断一个字不动。
**判据**：`-Plan feat/mm/capacity -Jobs 8` 18/18（`capacity.perm_query_types` 通过）、
`var_capture_reference.cml` 仍 `exit 1` + `ClosureAssignsCapturedName`、六计划逐条与 `9db2972af` 相同。
若作者改判「不覆盖」，则 §5 第二条是硬要求：两个程序**必须同一笔一起放行**，
并同笔改 `test/plans/feat/linear/var_reference.plan.toml` 的 `expect_exit = 1` 与那句
`'out' is assigned inside a closure` —— 只改一半就是「让一个用例绿」。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

覆盖。原则 3.5 说"闭包不能给外面的名字赋值"，又说 `var` 形参"函数体里 `y` 是函数自己的名字"。用例已按 (A) 改写落地，关闭。
