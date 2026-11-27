# `lane/blockroot` 交接卡（`task-blockroot` / 承接 `D-1128-21`）

写于 2026-11-28，作者：`lane-editview-commit`（上下文将尽，按 `D-1128-17` 交卡不交半笔改动）。

## ① 当前一步

- 分支：`lane/blockroot`
- tip：`f8da4ebc9f` — *A citation names the symbol, not the line: a line number moves with every edit.*
- 工作树（`D:\Projects\Camel-wt\editview-commit`）未提交三处，**全部是下面第三节那条"下一步"的探针，不是已完成的工作**：

```
 M src/stdlib/graph.cml                               1 +1/-1   （`export func commit_of` → `func commit_of`）
 M test/plans/probe/editview.plan.toml               12 +12/-1 （新增 editview.commit_of_user_visible 一条）
?? test/cases/probe/editview/commit_of_user_visible.cml        （用户直接写 `commit_of(...)` 的负例）
```

- 已落的是什么：**块能提交了**。`view d = f { … }` 的 `d` 是 `f` 的只读图（`graph_of`），`edit d = f { … }` 的 `d` 是 `open(f)` 的草稿根图，块的提交由降级发射 `commit_of(d, f)` 完成（`src/compile/src/gct/builder_expr.cpp:218-240`）。此前那几轮把"块的根句柄"从"块体里的一个 `let`"改成"闭包形参 `(d: Graph)`"，形参类型那一半已经正确。

## ② 读数（真退出码 / stdout 原文；没跑的就写"未跑"）

**已跑（`internal/tools/camel-remote.ps1`，88 主机，`test/plans/probe/editview.plan.toml`，plan 全文）**

```
[SUMMARY] total: 21, pass: 14, fail: 6, known failures: 1
[PASS] editview.commit_of_user_visible                       139.40 ms
[FAIL] editview.edit_value_is_the_function                   81.59 ms
[FAIL] editview.edit_value_is_not_the_body.nvm               79.24 ms
[FAIL] editview.edit_value_is_not_the_body_twin.nvm          88.36 ms
[FAIL] editview.body_fault_keeps_function.nvm                89.04 ms
[FAIL] editview.body_return.nvm                              87.05 ms
[FAIL] editview.head_evaluated_once.nvm                      96.62 ms
```

六条失败**同一个原因**，原文（`edit_value` 那条）：

```
[Error]: Reference 'graph::commit_of' is declared as imported but is not exported
```

⇒ `commit_of` 一旦不导出，**降级发射的 `graph::commit_of` 名字就解析不到**。这不是缺陷，是下一步的证据：**降级不该再走这个名字，应当直接调 `commit(草稿)`**。

**用户面那一半已成立**：`commit_of_user_visible` 绿，即用户程序写 `commit_of(...)` 被拒（`Unresolved reference: 'commit_of'`），与"内部提交入口不给用户"一致。

**未跑**：`feat/compile`、`feat/parse` 的新增红核对；`graph.cml` 的 `commit_of` 收窄之后 `std::gir` 的对照读数；`task-366` 的六条判据（`root_observable_as_graph` 仍红——`graph_of`/`block_root_graph` 在块表达式调用点都 `Unresolved reference`，见下）。

**块根句柄那一线的三条读数（供接续 `task-366` 用，不重测）**

1. 形参类型原文：`Function of type '(d: Graph) => Node[] pure'` ⇒ 形参那一半正确；
2. `graph_of` 在**块表达式的调用点**同样 `Unresolved reference: 'graph_of'` ⇒ **块头的 import 只作用于块体**，调用点对它完全不可见；换个名字也一样 ⇒ 别名只能**按算子身份改投**，不能按名字引用；
3. 带 id 的 dref 走 `gir/builder_variables.cpp:71` `internalOperatorGroup`，找不到组时报 `The built-in module did not register the operator the compiler emits` ⇒ **id 是表里的位置，还得有一个 native 组坐在上面**；`attempt` 的实现不是 kernel 而是 builder 改写（造值），我们要的是**改投**（`builder_call.cpp:1343/:2443` 的 `addOperNode` 之前）。

## ③ 下一步怎么接（决策者 `D-1128-21` 原话照抄）

> `commit_of(d: Graph, f: F)` 是**第二个提交入口**，靠多传一个 `f` 当见证来推 `F`，这正是 `Draft<F>` 解决的事。

完整路径：

1. 修"**原生类型注册与算子登记表初始化先后**"——这是 `Draft<F>` 上次被撤回的根因，根因已查清；
2. 重新落 `Draft<F>`（草稿的类型记着它是从哪个函数打开的，于是 `commit(d: Draft<F>)` 自己就能推出 `F`）；
3. 然后**删 `commit_of`**（连同 `graph.cml` 的声明与降级里的那次发射），并让 `edit` 块的降级直接调 `commit(草稿)`。

工作树里那三处未提交改动正是第 3 步的**前半段探针**（`export` → `func` 已完成，六条红就是"降级还在叫旧名字"的证据）。接手时可以直接沿用，或按你的判断 `git restore` 掉重做。

另：`task-373` 已转派，`builder_call.cpp` 那类改动（`lane-editview-a` 的 `native-constas` 线）**不要动**；本卡作者未碰该文件。

## ④ 边界

- 不 push、不 merge、不开新枝；只在这棵工作树里改文件。
- 不动冻结目录（`src/stdlib/autodiff/`、`rule_templates.cpp`、C++ 版优化遍）。
- 不改 `known_fail`、不改 `baseline.json`；红要么修掉、要么报出来。
- 改名 / 风格迁移之前先 `git grep` 文档，别只改代码。
- 构建与测试只走 88 主机（`internal/tools/camel-remote.ps1`），本机不构建。