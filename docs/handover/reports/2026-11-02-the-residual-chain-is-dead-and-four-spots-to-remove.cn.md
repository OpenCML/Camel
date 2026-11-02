# 残差那条链现在是死的：四处，读数，以及为什么由 `lane-resid` 删

**状态：读数是 `lane-resid` 的 `67a6420ea` 上取的。这一页不是待裁的页，是给"删机制的那一笔"留的账 —— 谁删、删哪四处、凭什么。**

## 一、读数（`67a6420ea`，`git grep` 全树）

| 名字 | 今天还剩什么 | 判 |
| --- | --- | --- |
| `TupleType::residuals` | 只剩**定义** `src/core/type/composite/tuple.cpp:188` 与枚举注释 `include/camel/core/type/intern.h:113` | **没有任何生产者**：`modules/transforms/split.cpp` 与 `split_operator.cpp` 里原先那三处调用（`split.cpp:842,849`、`split_operator.cpp:60`）在 `lane-resid` 这一轮里删空了 |
| `TupleType::isResiduals()` | 只剩**一个读者**：`src/compile/gir/builder_expr.cpp:2195` 的守卫；定义在 `include/camel/core/type/composite/tuple.h:139` | 守卫判断的是"这个元组类型是残差类型吗"，而**已经没人能造出残差类型的元组** ⇒ 条件恒假 |
| `SemanticDiag::ResidualsAreNotVisible` | 枚举 `include/camel/core/error/diagnostics/messages/semantic.h:478`、消息表 `src/core/error/diagnostics/messages/semantic.cpp:1800`、**唯一发点** `builder_expr.cpp:2196` | 发点就在那条恒假守卫里 ⇒ **已不可能发出** |
| `split_residuals_destructured` 的旧断言 | `test/cases/compile/transforms/split_residuals_destructured.cml`、`test/cases/transforms/split/split_forward_half.cml` 的注释、`test/plans/feat/compile/transforms.plan.toml` 那一条、`diagnostics/examples.tsv` / `docs/diagnostics.md` 的示例 | 断的是"残差元组不能被拆"；机制没了，这条拒绝再也构造不出来 |

## 二、为什么这一笔不是"作者说的删"而是"作者说的删，但要按它自己的次序删"

`include/camel/core/type/composite/tuple.h:135` 与 `builder_expr.cpp:2188-2194` 的注释都写着这条机制的**用意**：残差元组是变换自己的（成员数是图的答案，不是类型的答案），所以程序既不拆它也不读它的元素，而是整个交下去（设计原则 4.4，现在与以后的那一刀）。

⇒ 只要还有人在产这种类型，守卫就是**活**的，删它就是删一条活守卫。在 `67a6420ea` 上产它的那三处已经没了，**所以这一格从"活"翻成"死"是 `lane-resid` 自己这一轮的动作，不是我的判断**。

## 三、四处，按删除的次序（给 `lane-resid` 的收尾用）

1. `src/compile/gir/builder_expr.cpp:2195-2198` 的守卫（连同上面那段 2188-2194 的说明）；
2. `SemanticDiag::ResidualsAreNotVisible` 的枚举项、消息表行、`diagnostics/registry.tsv` 的那一行，并按仓库的规矩重生成 `docs/diagnostics.md` / `diagnostics/examples.tsv` / `docs/reference/camel-for-ai-diagnostics.md`；
3. `TupleType::isResiduals()`、`residuals_` 成员与 `TupleType::residuals(...)` 工厂（`tuple.h:135-139`、`tuple.cpp:188`），以及 `TypeCode::Residuals`（`intern.h:113`）；
4. 最后是那两处用例与计划条目（`split_residuals_destructured`、`split_forward_half` 的注释与 `feat/compile/transforms.plan.toml` 的那一条）。

次序的理由：1 依赖 2 的诊断还在（删 1 之前 2 会变成无人使用的枚举，但不会编译失败）；3 依赖 1、2 都空了才没有读者；4 依赖代码侧先没有拒绝，否则那条用例的"预期拒绝"会从一个真拒绝变成一个不存在的拒绝。

## 四、为什么不是我这支删

- 这四处**全在 `src/**`**，而本车道的边界是"只碰 `test/cases/**` + `test/plans/**` + 必要的文档"，`src/**` 属于 `lane-resid`；
- 把机制删死的人**只有它知道它为什么死**：是它在 `67a6420ea` 里把 `split.cpp`/`split_operator.cpp` 的三处调用删空的；
- 同一晚两个车道同时改 `src/**` 已经出过一次事故（按行切文件那一类），不再重复。

## 五、这一页的用处

`lane-resid` 收尾删这四处时，**这一页就是账**：删哪四处、按什么次序、以及"在 `67a6420ea` 上它确实已经没有任何生产者"这条读数。若它删的时候发现某处仍有读者，那说明这一页的读数过期了 —— 请以它当时的 `git grep` 为准并以它为准改这一页，而不是反过来。