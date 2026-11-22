# 调用构造的十个位置，与出口类型的三个来源

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `79b6959ea`）
**状态**：待裁（**本页不填默认**，理由见 §4）
**触发**：作者 `3a580d29a`「unify call construction first as a pure refactor, then apply the rule in that one place」。查这张表时查出：要统一的不是十处写法，是**出口类型的三个来源**。

## 1. 十个位置

| # | 位置 | 造什么 | 出口类型从哪儿取 |
| --- | --- | --- | --- |
| 1 | `src/compile/gir/builder.cpp:155` | 根图（入口）对 main 体的那一次调用 | `entryGraph->funcType()->exitType()` |
| 2 | `src/compile/gir/builder_calls.cpp:200` `addCallNodeOf` | 直接调用的 FUNC 节点 | `callValueTypeOf(callee, writtenCaptures)` |
| 3 | `src/compile/gir/builder_calls.cpp:251` | 函数值自身的 FUNC 节点 | 同上（`addCallNodeOf`） |
| 4 | `src/compile/gir/builder_calls.cpp:328` | 经函数值调用的 CALL 节点 | `graphExitTypeOf(graph->funcType())` |
| 5 | `src/compile/gir/builder_calls.cpp:874`（返回在 `:902`） | 转发图里对被调者的调用 | `addCallNodeOf(target)` |
| 6 | `src/compile/gir/builder_operators.cpp:3203`（取第 0 份在 `:3210-3214`） | 算子调用的 CALL 节点 | `graphExitTypeOf(factoryType)` |
| 7 | `src/compile/gir/builder_operators.cpp:4493` | 经函数值的 invoke 节点 | `graphExitTypeOf(funcType)` |
| 8 | `src/compile/gir/builder_operators.cpp:5050`（取第 0 份在 `:5061`） | `visitLinkNode` 的「交不交回」决定 | `callHandsBackResult(targetGraph)`（图上的位） |
| 9 | `src/compile/gir/builder_control.cpp:3528` | 循环 `T..` 的 pull 调用 | `pullType`（步签名，自己定的） |
| 10 | `src/compile/gir/builder_decorators.cpp:354` | 装饰器 invoke | `decoratedGraph->funcType()->exitType()` |

不是构造点、但是**全部调用值的唯一出口**：`src/compile/gir/builder_expr.cpp:759` `callValueTypeOf`、`:792` `callValuePartOf`（`:869` 是「读被一个调用写过的变量时取第 `1+k` 份」）。规则最终落在这两条上，所以它们不属于「要合并的十个」，而属于「合并之后唯一的那一处」。

## 2. 出口类型的三个来源

- **裸声明型**（`funcType()->exitType()`，不打包）—— **2 处**：`builder.cpp:155`、`builder_decorators.cpp:354`。
- **派生型**（`graphExitTypeOf` 及其两个包装 `callValueTypeOf` / `addCallNodeOf`）—— **6 处**：`builder_calls.cpp:200`、`:251`、`:328`、`:874`、`builder_operators.cpp:3203`、`:4493`。
- **图上的位**（`callHandsBackResult` → `GraphDraft::packsVarParameters`）—— **1 处**：`builder_operators.cpp:5050`。
- **自定**（以上都不是）—— **1 处**：`builder_control.cpp:3528` 的 `pullType`。

⇒ 十处写法里，**取出口类型的口径有三个**（裸声明 / 派生 / 位），加一处自定。同一件事在前两处会给出 `int`，在后六处会给出 `(int, int)`——而 `linear` 与 `compile` 的红正是这两种答案在互相打。

## 3. 可能还有（没找全，这不是「已找全」）

1. 部分应用：`builder_calls.cpp` 的 `partialApplicationOf` 一族。
2. 标签算子/存储算子：`applyTagOperatorCall`、`makeStoreOperator`。
3. autodiff 的规则展开（`modules/` 里造图的那些）。
4. `modules/` 自带的图构造（模块可以自己造 CALL/FUNC）。
5. GIR 之外的 4 处：`src/runtime/definitions.cpp:556`、`src/runtime/draft_outline.cpp:310`、`src/passes/opt/defun/defunctionalize.cpp:580`、`:618`。

## 4. 要作者答的一句（本页不填默认）

> **三个来源里，哪一个成为那一个？**

因为「纯重构、零行为改动」与「把 2 处裸声明型改成派生」**不可能同时成立**：后者会改行为（`builder.cpp:155` 一改，入口那次调用的值就从 `int` 变成 `(int, int)`，那正是这四步一开始量到的那一格）。所以只有两条路，而它们互斥：

- **先统一到裸声明型**：零行为，但把六处的派生**降级**（`var` 打包在调用侧就不成立了）；
- **先统一到派生**：口径一致，但**不是纯重构**（至少 2 处的行为会动）。

**本页不填默认**：这一格没有「更安全的那一条」——一条是零行为但降级，一条是升级但改行为；而这一步的判据（六计划逐条相同）只有第一条能满足。选哪条是作者的判断，不是车道的。

## 决策者答（2026-11-23）：被重构取代

作者问"哪一个来源成为那一个"，新前端里的答案是：类型检查阶段算出的函数类型。十处构造点、三个来源随前端分阶段收成一处。这些页出自同一个根子：GIR 建图器在一个阶段里同时定类型和建图，并用 cell、打包位、就地改开放签名来表达 `var` 形参。核心抽象第 8 节定了前端分阶段：类型检查阶段"函数类型在这里定下来，此后不再改"；建图"只做机械的翻译……`var` 只降低成 SSA（原则 3.5），`Ref` 是唯一的单元"；第 5 节定了"函数类型有唯一的主人"，第 17 节定了"'开放类型'随前端重构消失"。于是本页问的机制在新前端里不存在：调用表达式的值永远是声明的结果，`var` 形参的终值是调用的另一份输出，由调用处重绑。lane-ssa 分支不合，归档。

带进 前端分阶段 的验收输入：本页 §1、§3 的构造点清单，作为"建图只有一处造调用"的核对表
