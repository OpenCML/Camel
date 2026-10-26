# 审计：图是值，变换不改别人看得到的图（设计原则 9.1）

2026-10-26 · lane `generics`，分支 `lane/graph-values`（基于 fsh-dev `7cb99a8a6`）

> **更新（同日）**：F1 已按下文的修法修好，原来九项 `known_fail` 全部转绿，标记已去掉；附录里 `saturate` 的内部错误也已找到原因并修好。修好之后又暴露出另一个错误，见附录二。

审计对象是 9.1 新增的两条规则：

- 变换交出新函数，原来的函数对其他持有者不变；
- 原地改写只在图只被唯一持有时发生，或者用结构共享只复制改动的部分。

## 结论

| 项目 | 结果 |
|---|---|
| 违反 | **一处**（F1）：图 API 与重写引擎在程序内部被调用时，会把结果装成上下文的程序。之后 `current()` 指向结果，被 pass 丢弃的结果也会留在那里。 |
| 运行中的代码 | 不受影响：按名字调用、函数值、闭包、`graph_of`。 |
| 交给变换的 Program | 不受影响。 |
| 修法 | 改成 detached 提交，装入只由 pass 驱动来做（F1）。 |
| 其他 | 一处良性的缓存标记写在共享图上（F2）；9.1 的写法尚未全部实现，记作缺口而非违反（F3）；顺带发现 `saturate` 的一个内部错误（附录）。 |

## 底层模型：已经是持久化的

图在构造之后不可变：

- `GCGraphBuildAccess` 只在编码器里使用：
  - `src/compile/gir/graph_encode.cpp:695`、`:1038`
  - `src/runtime/draft_session.cpp:515`、`:564`
  - `src/runtime/draft.cpp:2130`
- 唯一例外是 F2 的标记位。

一切编辑都在 `GraphDraft` 上进行。提交时（`src/runtime/draft_session.cpp:1441` 起）把改过的图编码成新副本：

- `fresh` 表，`draft_session.cpp:1554`；
- 静态槽里的函数值逐个克隆，`cloneStaticSlot`，`draft_session.cpp:503`；
- 放进一个新的 Program，没改的图直接共享，即按图粒度的结构共享；
- 旧 Program 原样保留。

函数值记着自己所属的 Program，所以拿着旧函数值的代码永远解析到旧版本。

问题不在图对象，而在"谁当上下文的程序"：

- `RuntimeGraphDraftSession::commit` 在 `installing` 为真时（`draft_session.cpp:1460`）会调用 `installRuntimeProgramIfCurrent`（`:1741`），把结果装成上下文的程序。
- `installing` 的条件是：非 detached、会话是上下文程序上的会话、根相同。

## 测试

- 用例在 `test/cases/modules/graph_values/`，计划在 `test/plans/feat/modules/graph_values.plan.toml`。
- 共 32 项：23 项通过，9 项是标了 `known_fail` 的违反。

| 用例 | 内容 | 后端 / 流水线 | 结果 |
|---|---|---|---|
| `function_txn.cml` | `open_function` / `replace` / `to_function` 去掉 `target` 的 `+ 1`；新函数得 15，`target` 仍得 18。`target` 的图仍有 `+ 1`、节点数不变，变换前建的闭包和两个按名调用者都看到原函数。 | nvm、fvm、jit | 通过 |
| `program_txn.cml` | 在 `current()` 上 `open` / `replace` / `commit`：提交结果没有 `+ 1`；打开时的 Program 仍有；正在运行的 `target`（按名、`graph_of`、提交后新建的闭包）仍是原函数。 | nvm、fvm、jit | 通过 |
| 同上 | `current()` 在提交后应仍指运行中的程序 | nvm、fvm、jit | **known_fail**（F1） |
| `rewrite_rule.cml` | `rewrite(current(), [x + 1 => x])`：结果、给入的 Program、运行中的代码，同上 | nvm、fvm、jit | 通过 |
| 同上 | `current()` 不应变成重写结果 | nvm、fvm、jit | **known_fail**（F1） |
| `grad_holders.cml` | `grad(loss)`、`with_grad(loss)` 之后，`loss` 和调用它的 `user` 结果不变，`user` 的图大小不变 | 运行时构造：nvm、fvm、jit；`std::macro` 展开：nvm、fvm | 通过（两条路线输出逐字相同） |
| `attempt_holders.cml` | `attempt(ratio, x)` 之后直接调用 `ratio` 仍在原位置故障，图大小不变 | nvm、fvm | 通过 |
| `pipeline_inline.cml` | 命令行流水线是唯一持有者：内联并优化后，函数、两个调用者、闭包结果都正确 | `std::opt` 加 nvm，以及 inline 加 nvm / fvm / jit | 通过 |
| `discard_target.cml` + `discard_pass.cml` | 用 Camel 写的 pass 试做一次 `rewrite` 后返回原 Program；流水线按返回值继续，计算得 18 | nvm、fvm、jit | 通过 |
| 同上 | 运行中 `current()` 应仍是 pass 返回的 Program | nvm、fvm、jit | **known_fail**（F1） |

## 发现

### F1（违反）图 API 与重写引擎在程序内提交时，会把结果装成上下文的程序

**位置**

- `modules/graph/txn.cpp:356`：`commit(t)` 对 `open(p)` 打开的事务调用 `session.commitChecked()`。
- `modules/rewrite/module.cpp:931`：`rewrite` 与 `run` 共用的 `applyRewrite`，`commitChecked()`。
- `modules/rewrite/module.cpp:1519`：`saturate`，`commitChecked()`。
- `src/runtime/writings.cpp:1208`：`extract` 经 `modules/rewrite/module.cpp:1541` 调用 `extractWritings`，`session.commit()`。
- 机制：`src/runtime/draft_session.cpp:1456-1460`（判定 `installing`）、`:1741`（装入）。

**现象**

在程序里写 `let q = commit(t) !! p`，或者 `rewrite(current(), rules)`，之后 `current()` 返回的就是 `q`。

最清楚的一例是 `discard_pass`：

1. pass 做了重写，但返回的是原来的 `p`。
2. 驱动看到返回值未变，于是什么也不装（`src/execute/camel_pass.cpp:253`，`produced == program`）。
3. 运行时 `current()` 里却是被丢弃的重写结果（`current: 0`；不加这个 pass 时为 `current: 1`）。

这正是 9.1 说的"变换改了别人看得到的东西"：

- 上下文是程序的一个持有者；
- 一个只该交出值的变换，改了它所持有的东西。

**不受影响的**（测试已证）

- 运行中的代码；
- 已有和之后新建的函数值；
- 交给变换的 Program；
- 流水线的下一个 pass（它用 pass 的返回值）。

**修法（已实施）：所有权检查，装入只在驱动里做**

- `commit`：`modules/graph/txn.cpp`，`commitKernel`，一律 `session.commit(true)`。
- `rewrite`、`run`、`saturate`：`modules/rewrite/module.cpp`，`applyRewrite` 与 `saturateKernel`。
- `extract`：`extractWritings` 增加 `detached` 参数（`include/camel/runtime/writings.h`），Camel 的 `extract` 传 `true`。
- `GraphConflict` 从 `commit` 去掉：两个事务开在同一个 Program 上，提交出的就是两个 Program。`graph.plan.toml` 的 `txn_conflict` 因此改为 `txn_two_commits_are_two_values`，期望第二次提交也成功；`stdlib/graph.cml` 与 `docs/stdlib-reference.md` 随之更新。

下面是当初提出的修法：

1. 这四个从 Camel 侧可调的提交点一律 detached 提交：`session.commit(true)`。`extractWritings` 增加一个 `detached` 参数：流水线里的提取照旧，Camel 的 `extract` 传 `true`。
2. 装入程序只由 pass 驱动来做。用 Camel 写的 pass 已经如此（`src/execute/camel_pass.cpp:254`，`installRuntimeProgramIfCurrent`），C++ 的 pass 由流水线自己提交。这样唯一持有者（流水线）原地发布，其余调用只交出值。
3. `GraphConflict` 原本表示"上下文的程序在事务期间变了"。detached 提交只对照事务打开时的 Program，在 Camel 侧已不可能发生。按"不考虑兼容成本"，从 `stdlib/graph.cml` 的 `commit` 文档里删去这个错误，`commitChecked` 只留给驱动用。
4. 测试：上表三项 `known_fail` 修好后会被运行器报成"标了 known_fail 却通过了"，到时去掉标记即可。

### F2（良性，不是违反）共享图上的缓存标记

**位置**：`src/runtime/draft_session.cpp:1524`

```cpp
GCGraphBuildAccess::addFlags(graph, kGraphFlagGatesDissolved);
```

**行为**

- 提交时，对一张没被编辑、也没有可消解门的图，直接在现有的、可能被别的 Program 共享的图对象上打这个标记。
- 下次提交据此跳过解码（`:1518`）。

**为什么不算违反**

- 这个标记只记录图内容本身的一个性质（"没有无序门"）；
- 写入是幂等的，不改语义，属于 9.1 说的"性能上的事实"，与数组计数同类。

**建议**：在标记的定义处（`include/camel/runtime/graph.h:204`）写明这条不变量："只能记录由内容决定、单调的事实"。这样以后不会有人在共享图上记别的东西。

### F3（缺口，不是违反）9.1 的写法尚未全部实现

- **pass 不能在源码里当函数调用**：`twice->std::opt` 报 `UnresolvedReference`；`import { opt } from std` 报 `ImportNameNotExported`。目前没有运行时路径，也就谈不上违反。
- **以函数为中心的事务已有，但名字不同**：`open_function(f)` / `to_function(t, sample)`（`modules/graph/txn.cpp:371`、`:380`），走 detached 提交（`:395`），行为合规。
- **写 pass 的接口里仍有 `Program`**：`current`、`open(p)`、`commit(t): Program!`、`rewrite(p, rules)`；9.1 说 Program 是内部表示，不出现在这个接口上。
- 将来实现 `f->std::opt` 和 `open(f)` / `commit(t)` 时，提交必须 detached，即 F1 的修法；否则同样的问题会出现在新入口上。

## 已核实不就地改动的路径

| 路径 | 依据 | 测试 |
|---|---|---|
| `grad` / `with_grad` / `vjp`（运行时构造） | `modules/autodiff/pullback.cpp:1106`、`:1121`：在自己的会话工作副本上解码草稿，产出新的图组，从不向上下文提交 | `grad_leaves_loss_and_its_callers` |
| `std::macro` 展开 | `src/execute/macro/macro.cpp:21-76`：每轮在草稿上改写、提交新 Program；是流水线的唯一持有者，允许原地发布 | `grad_expanded_by_macro_*` |
| `attempt(f, x)` | 运行结果 | `attempt_leaves_the_function` |
| ONNX 导出 | `modules/onnx/exporter.cpp:4628`：会话以函数自己的图为根，`installing` 为假，不改运行中的程序 | — |
| 规则检查的采样会话 | `modules/rewrite/module.cpp:3454-3481`：以规则左侧为根，不装入 | — |
| 流水线里的 pass（`tensor` 融合、`tiles` 区域、通用重写） | `modules/tensor/passes/fuse.cpp:221`、`modules/tiles/src/region_outline.cpp:541`、`src/passes/opt/generic/rewrite.cpp:402`：唯一持有者 | `pipeline_rewrites_its_only_holder` |
| 执行计划 | `src/execute/exec_plan.cpp:629` 的 `const_cast` 只为调用常量接口，不写图 | — |

## 附录：`saturate` 的内部错误（已修）

**原因**

- 规则 `x + 1 => x` 的右边是被绑定的 `x` 本身。
- 当 `x + 1` 位于 `&&` 的臂里、而 `x` 在臂外时，饱和会把两个不同区域的节点并成一个值的两种写法（`src/runtime/writings.cpp`，`saturate`，`assertEqualDeferred`）。
- 这违反了"一个值的各种写法同在一个区域"的前提：提取可能把臂里的节点放到臂外的使用处。于是抛出 `std::logic_error`，执行它的遍报 `PassInternalError`。
- `str::ends_with` 正是这种形状，所以凡是导入 `str` 的程序，用 Camel 的 `saturate` 都会出错。

**修法**

- 右边的结果若与被匹配节点不在同一区域，饱和就不记录这条等价。
- 单向的 `rewrite` 照样做这次替换：在臂里读外层的值本来就是一条普通的边。

**测试**：`test/cases/modules/rules/saturate_outer_operand.cml`，以及 `rules.plan.toml` 里的两项：

- 用 Camel 的 `saturate` 加 `extract`，nvm、fvm；
- 走 `std::opt::saturate`。

## 附录二：`std::opt::saturate` 在含 `loop` 的库函数上出错（未修，另行处理）

**现象**：任何导入了 `str` 的程序，跑 `std::opt::saturate std::nvm` 都报：

```text
internal error: Runtime graph 'str::cut' has region 1 with a port the graph does not have.
```

**最小程序**：

```camel
module t
import { ends_with } from str

func main(): int sync {
    println("abc"->ends_with<"bc">)
    return 0
}
```

**观察**

- 把上面的区域修法临时关掉、重新构建，同样出错，说明这是另一个早已存在的缺陷。
- `str::cut` 里有一个 `loop`。错误出在饱和或提取之后的图校验，涉及循环区域的端口。
- 原因比这次的任务深，没有修，只记录在这里。
