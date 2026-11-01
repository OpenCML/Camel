# 在"构造期"发现的问题，为什么报不出来

> **问什么**：`std::opt settle_runs=0` 明明写了一句话（`settle_runs must be at least 1, got 0`），
> 为什么用户看到的是 `terminate called … DiagnosticsLimitExceededException` 和 **退出码 134**？
> **答**：因为**参数校验发生在 pass 被构造的那一段，而那里没有诊断接收方**。这一页把这条架构事实、
> 它的证据、以及它连带的第二个事实（"检查通过"不等于"构建期草稿自洽"）写死，免得下一个人重跑一遍。
> **堵着谁**：S1 的 `settle_runs=0` 拒绝（要么诊断、要么不许 abort）；以及 `lit2` 的 `result=10`
> 该在哪一层被抓住。

## 一、读数：名字和文本都对，但退出方式是 abort

`camel test/cases/modules/onnx/export_loop.cml "std::opt settle_runs=0" onnx::export`：

```
terminate called after throwing an instance of 'camel::core::error::DiagnosticsLimitExceededException'
  what():  Error[RuntimeDiag::InvalidArgument]: Invalid argument of 'std::opt': settle_runs must be at least 1, got 0
EXIT=134
```

**要区分两件事**：

1. **诊断对象造出来了**——`RuntimeDiag::InvalidArgument`，文本对，`stage` 对，`name` 对；
2. **没有人接收它**——`commit()` 在没有接收方的上下文里直接抛 `DiagnosticsLimitExceededException`，
   而那个异常在 pass 构造段没有被接住，走到 `std::terminate`。

所以这不是"错误码选错了"，是**通道缺失**：`src/execute/pass.cpp` 里 `optimizePass()` 这类 pass maker
在**构造期**被调用，此时既没有 `hasErrors()` 的读者，也没有把异常转成诊断的那一层。运行期的 pass 有
（`pass.cpp:2915-2927` 把逃逸的 `std::exception` 转成 `RuntimeDiag::PassInternalError`），构造期没有。

## 二、这条事实的推论（写给下一个人）

> **构造期没有诊断接收方；任何在构造期发现的问题，必须在 pass maker 之外接住才报得出来。**

**（补正，2026-11-02）这句话有前提，否则会被读成"参数错误报不出来"**：`src/execute/pass.cpp` 在
**构造 pass 之前**就已经有一整套参数通道——`bindArgs` 拒掉类型错、名字未知、重复、缺失，
`parameterDiagnostic` 把它们转成 `RuntimeDiag::InvalidPassParameter` / `SemanticDiag::ParamArgumentWrongType`，
**带 `<pipeline>:<line>:<column>` 位置**，走 `hasErrors()`。所以"构造期没有接收方"说的是
**在 pass 的构造体内部（或它调用的函数里）就地 `commit()` 没人接**，**不是**"参数错误天生没地方报"。

**三层要分开记**：

| 层 | 在哪 | 报得出来吗 |
|---|---|---|
| 取值/类型/名字校验 | `bindArgs`，**构造之前** | 有：`InvalidPassParameter` + 位置 |
| 构造循环兜底 | pass maker 外面 | 兜底，转 `PassInternalError`（**还没做**） |
| 运行期 | pass 的 `apply` 内 `rtmDiags` | 有：`hasErrors()` 停链 |

三条同族的先例，都是"造出来了但没人读"：

| 症状 | 位置 | 读的人在哪 |
|---|---|---|
| 抛 `std::runtime_error` 逃出 `apply` | `src/passes/opt/inline/engine.cpp` | `pass.cpp:2915-2927`（有，转 `PassInternalError`） |
| `failWithErrorValue(makeError(...))` | 同上 | **没有人读返回值**，静默通过 |
| 构造期 commit 诊断 | `src/execute/pass.cpp` 的 pass maker | **没有**，直接抛 |

第三种是最坏的：它连"失败了"都不说，进程直接 abort，而 abort 在脚本里和 `timeout` 的 `124` 一样
只是一个数字。**护栏必须红字 + 非零退出；崩溃不是拒绝。**

## 三、第二条事实：检查通过 ≠ 构建期草稿自洽

这一轮给 `EdgeChecker` 加了 `checkBranchBackReferences()`（`src/runtime/graph_edges.cpp`），
在 **commit 之后** 对冻结图检查四件事：

- `BRCH.join` 在 `[0, nodeCount)` 且目标是 **JOIN**；
- `JOIN.brch` 在 `[0, nodeCount)` 且目标是 **BRCH**；
- **互为反指**（`Brch(6).join=5` ⇒ `Join(5).brch=6`）；
- region 的 `owner` / `result` 在 `[0, nodeCount)`（`kInvalidNodeRef` 允许）。

**读数**：

| 项 | 结果 |
|---|---|
| 干净树全量 | `total 8337, pass 8325, fail 0, skip 10, known failures 2`——**零误报** |
| A/B（把互反指比较改成 `!= ref + 1`） | `graph 'halve' branch 6: its join 5 says its branch is 6`，`EXIT=1` |
| 能不能报出 `lit2` 的 `result=10` | **不能**——这条检查只跑在 commit 之后的冻结图上 |

**所以**：`lit2` 报的 `result=10`（一个连节点都不存在的 id）**若出现在 commit 之前的草稿里，这条检查按设计看不到**。
"全量绿"证明的是**冻结图自洽**，不证明**构建期草稿自洽**。这两句话不能互相代替。

## 四、已经划掉的嫌疑：builder 的写入时序

`lit2` 读到的 `Brch(4).join=4`（自指）最像"占位值没被回填"，所以先查了 `src/compile/gir/builder_control.cpp`：

| 行 | 做什么 |
|---|---|
| `:2707` | `setRegionOwner(arm.region, nodeIdOf(brchNode), i)` |
| `:2709` | `setRegionResult(arm.region, nodeIdOf(arm.output))` |
| `:2710-2713` | `DraftBrchPayload{.join = nodeIdOf(joinNode), .armCount}` → `rewriteNode(brch, …)` |
| `:2740` | `GCJoinBody{.brch = nodeIdOf(brchNode), .armCount}` → `rewriteNode(join, …)` |

**两个 id 在写之前都已解析**（`joinNode` / `brchNode` 都是已存在的节点引用），没有占位值、没有延迟修补。
**这条嫌疑划掉。**

## 五、剩下的候选

**（撤回，2026-11-02，`lit2` 给出四个坐标之后）**这一节原先说"症状只可能来自 encode / decode"，
**这个判断作废**。`lit2` 的四个坐标是：

| 坐标 | 读数 |
|---|---|
| ① 阶段 | builder 里 `instantiateCompileTimeTransforms()` 那一刻——**commit 前** |
| ② 读 region 的 API | `GraphRead::regionOf(id)` → `DraftGraphRead` 实现是 `txn_.graph().region(id)`（`graph_read.h:217`），**不重映射** |
| ③ 那 8 个节点 | 是**编码器编号**（`DraftGraphRead(txn, kept)`，`sourceIdOf(ref) = kept[ref]`，`graph_read.h:183-190`），不是草稿自有 id |
| ④ 于是 | `Join(3).brch=5` / `owner=5 result=7` / `result=10` 是**跨空间比较** |

**所以 `result=10` 不是"漏映射"，是"两张表按不同编号空间逐值比较"**：节点按 `kept` 编号、
region 仍是原始 id。**错在读数，不在代码。** `draft_inline` / `draft_clone` 不要按"漏映射"改；
`lit2` 那句"回指的来源在草稿里不可信"同样撤回。

**可查的口径**：**两张表的编号空间不同时，逐值比较没有意义——先统一编号，再比。**
这是"假数"的第七种形态：**假数不只是编出来的，也可以是跨空间比出来的。**

**回指自洽检查（`e33b52ee0`）不受影响**：它跑在冻结图上、只用同一个空间，四条不变量仍然成立。
要真正回答"构建期草稿自洽与否"，需要的是**同一份草稿、同一阶段、四个值同空间**的第二张表
（给 `DraftGraphRead` 一条 `kept` 恒等的读数路径即可），不是把同一个检查换个阶段挂。

## 六、已经做完的与还没做的一件

`settle_runs=0` 现在走**主路**（`403b9e869`）：`bindArgs` 旁的取值校验
（`ParamProblemKind::OutOfRange`）在 **pass 构造之前**拒掉它，

```
error[RuntimeDiag::InvalidPassParameter]: Invalid pass specification 'std::opt settle_runs=0':
settle_runs must be at least 1, got 0.      EXIT=1，没有 terminate
```

**还没做**：构造期兜底——`pass.cpp` 构造循环外 `catch (const std::exception &)` →
`PassInternalError`（点名 pass、保留 `e.what()` 原文）→ `hasErrors()` 收尾。
**只兜 `std::exception`，不兜 `...`**：把编程错误也吞掉会掩盖 bug。

## 七、第四格：预检期连 `commit` 都不行（今天新学到的）

三层表要补一格，而且这一格比前三格都硬：

| 层 | 在哪 | 报得出来吗 |
|---|---|---|
| 取值/类型/名字 | `bindArgs`，**pass 存在之前** | 有：`InvalidPassParameter` + 位置 |
| **预检** | `passListTranslatesProgram` / `firstConsumingPass`，**运行开始之前** | **没有：`commit` 直接抛 `DiagnosticsLimitExceededException`，走到 `terminate`** |
| 构造 | `build` 返回 `optional`；里面 `catch (const std::exception &)` | `optional` 表达"构造失败"，`catch` 接住"构造抛出"，**两者转成 `buildProblem`（纯字符串），不 commit** |
| 运行期 | pass 的 `apply` 内 `rtmDiags` | 有：`hasErrors()` 停链 |

**实测**（探针：`std::opt settle_runs=7` 时让 `optimizePass()` 抛 `runtime_error`，提交前已删）：

- 只包 `applyRegisteredPass` 的 `factory(ctx)`（`pass.cpp:2522`）→ **仍 abort**，`gdb` 栈顶是
  `passListTranslatesProgram(...)`：**流水线开跑之前，预检会为每个 pass 构造一次**，只为 `dynamic_cast`；
- 再把两个预检构造点包成 `commit(PassInternalError)` → 文本对了
  （`Pass 'pass:std::opt settle_runs=7' failed while it was being built: …`），**退出仍是 134**，
  这次抛的是 `DiagnosticsLimitExceededException`——**预检期没有接收方**。

**所以**：兜底不能靠 `commit`；要么让预检**不构造** pass，要么把失败记成字符串，
交给**已有**的、有接收方的 `throwUnresolvedPass`。

## 八、兜底那一笔的图（可行性表，撤除落地后照此做）

| 项 | 现状 | 要做 |
|---|---|---|
| `PassDescriptor`（`include/camel/execute/pass/params.h:96`） | `{ PassMaker make; std::vector<PassParam> params; bool readsTags; }`——**没有 kind** | 加一个 `PassKind kind`（`Rewrite`/`Translate`/`Schedule`），由注册点填；或从 stdlib 声明读 |
| `resolvePass` 读 kind 的路径 | 声明里没有"这是翻译 pass"这种信息；`std::gir` 之类靠 C++ 类的 `dynamic_cast` 区分 | 若加 `kind`，注册点（`pass.cpp:583` 附近 `PassDescriptor descriptor;`）就要填，**34 个注册点都要看一遍**——这是这一笔真正的工作量 |
| 预检点 1 `firstConsumingPass` | `pass.cpp:2639` `factory(ctx)`，cast 到 `GraphSchedulePass` / `GraphTranslatePass` | 改问 `descriptor.kind`，不构造 |
| 预检点 2 `passListTranslatesProgram` | `pass.cpp:2624` `factory(ctx)`，cast 到 `GraphTranslatePass` / `GraphSchedulePass` | 同上 |
| 另两处 cast | `runPipeline` `:2848`（`schedules`）、`:2890`（`consumes`） | 这两处在**运行中**，已有 `rtmDiags` 接收方，可保留 |
| `throwUnresolvedPass` | `pass.cpp:1579`，`[[noreturn]]`，签名 `(spec, resolved, line, ctx)`；吃 `paramProblem` / `problem` / `missingPass` | 加一个 `buildProblem` 分支，**同一出口** |
| 它有没有接收方 | **有（实测）**：`settle_runs=0` 走 `paramProblem` → `parameterDiagnostic` → `error[RuntimeDiag::InvalidPassParameter] … EXIT=1`，无 `terminate` | 这条链路可以直接承载 `buildProblem` |