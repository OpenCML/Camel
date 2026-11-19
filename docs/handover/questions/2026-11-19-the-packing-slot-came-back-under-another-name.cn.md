# 打包位换了名字回来：删它拆掉一半功能，留它把作者撤掉的位放回 develop

**日期**：2026-11-19
**提出者**：合并车道（`lane-mrg`，基线 `origin/develop` = `95fa98cde`）
**状态**：等作者裁定。本页记下事实与两难，**默认按「只挑不带打包位的提交」执行**（见第 4 节），`lane-ssa` 分支**保留不删**。
**触发**：合并 `lane-ssa`（`5559d3c6e`，`ahead=25`）时，`include/camel/runtime/draft.h` 冲突，而两侧的差别不是同一行的两种写法。

## 1. 事实：develop 上没有这个位，合并树上有四处真在调它

`origin/develop`（`95fa98cde`）上：

```
$ git grep -c packsVarParameters origin/develop -- src/compile/gir include/camel
（命中 0）
```

把 `lane-ssa` 合进 `develop` 的树（`merge --no-ff --no-commit lane-ssa`，只 `draft.h` 一处冲突）上，**四处不是注释、是真调用**：

```
include/camel/compile/gir/draft_graph_builder.h:87   bool packsVarParameters() const { return draft().packsVarParameters(); }
include/camel/compile/gir/draft_graph_builder.h:88   void setPacksVarParameters(bool packed) { draft().setPacksVarParameters(packed); }
src/compile/gir/builder_control.cpp:4633             graph->setPacksVarParameters(true);
src/compile/gir/builder_helpers.h:141                return callee != nullptr && callee->packsVarParameters();
```

冲突区（`include/camel/runtime/draft.h`，`HEAD` 侧 471–475 / 基线 `2d7c5c3a1` 476–477 / `lane-ssa` 侧 478–486）：

```
HEAD（develop）：
    void setDeclaredFuncType(camel::core::type::FunctionType *funcType) {
        noteOriginalTypes();
        declaredFuncType_ = funcType;
    }
lane-ssa：
    void setDeclaredFuncType(camel::core::type::FunctionType *funcType) { declaredFuncType_ = funcType; }
    /// Whether the body packed `var` parameters into the exit type, as against writing that tuple
    /// itself (`Builder::handBackVariableParameters`). ...
    bool packsVarParameters() const { return packsVarParameters_; }
    void setPacksVarParameters(bool packed) { packsVarParameters_ = packed; }
```

**所以两侧的差别不是「一行怎么写」，而是「`lane-ssa` 还在带整套打包位」。**

## 2. 那条线在一夜之间来回四次

```
ff09f8032  gir: the packing comes back, and the exit of a body that declares no result stops making a value
94e577538  revert: the packing leaves again, and the void guard goes with it because it never fires
5695e055c  gir: the packing returns, with the declaration as its source, so the failures it causes are visible again
15ab5f6ed  gir: the bit is named for what it answers -- whether this graph packed -- and not for what its exit is
```

`develop` 上是作者 2026-11-19 的那句决定的落点（`8f7b9687f` 的撤销，`draft.h:464-469` 的注释原文）：

> the graph keeps the declared function type; the packed exit type is computed from it; the packing bit goes

## 3. 两难

- **删**（按 `develop` 的方向消解冲突）：上面三处调用点全部编译不过；而引入它们的是 `lane-ssa` 自己的 `990ac4b23`（「every CALL site is constructed in one place」）与打包线，等于把那条分支一半的功能拆掉。
- **留**（两侧都留）：把作者在 `develop` 上撤掉的打包位放回 `develop`，与 `8f7b9687f` 的撤销直接对抗。

两种都不是合并车道能替作者做的：前者是删别人的功能，后者是驳回作者的决定。

## 4. 默认处置（本页写死，作者可推翻）

**只挑 `lane-ssa` 里不带打包位的提交**：`990ac4b23`（每个 CALL 点在一处构造，纯结构调整）+ 两页文档（`10e35a92b`、`265bee71b`）；
**不挑** `ff09f8032`/`94e577538`/`5695e055c`/`15ab5f6ed` 整条打包位线；`lane-ssa` 分支**保留不删**，等作者按新设计（声明为源、图上的位不留）重做。

**实测补充**：`990ac4b23` 今天在 `95fa98cde` 上**也冲突**（`src/compile/gir/builder.h`、`builder_calls.cpp`、`builder_operators.cpp` 三处），所以这一条的「只挑」目前同样落不下去 —— 它要改的调用点在这几天已经被别的提交改动过。**这一条也留给作者**，合并车道没有硬解。

## 5. 要作者回答的一个问题

打包位在新设计里到底还要不要「在图上记一个位」？如果答案是「不要」，`lane-ssa` 的 `990ac4b23` 与 `5559d3c6e` 需要在它自己的分支上按「声明为源、位不留、调用点仍集中在一处」重做一遍；如果答案是「还要」，那 `8f7b9687f` 的撤销需要重新裁定。两者都不是 cherry-pick 能解决的。