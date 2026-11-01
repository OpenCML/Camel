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

症状（`result=10` 越界、`Brch.join` 自指）只可能来自 **encode / decode 这一步**：

- `src/compile/gir/graph_encode.cpp:289`：draft → GCGraph，读 `DraftBrchPayload` 写 `GCBrchBody`；
- `GraphDraft::decode`：GCGraph → 草稿，把 id 当成本图 id。

而两条 clone 路径（`src/runtime/draft_clone.cpp` 的 GCGraph→草稿 `:68-73`、草稿→草稿 `:134-139`、
`GCJoinBody.brch` 的 `:98-101` 与 `:145` 起）与 `adoptRegions`（`:204-240`）**都已重映射**，
所以"拷贝时漏映射"这条也**不成立**。要动 encode/decode，必须先拿到 `lit2` 的**四个坐标**
（builder commit 前 / 后 / decode 之后，以及读 `result` 用的是 `draft.region(id).result` 还是 `GCGraph::region`）。

## 六、还没做的一件

`settle_runs=0` 的**构造期通道**：在 `src/execute/pass.cpp` 的 pass maker 调用点
`catch (const std::exception &)` → `rtmDiags()->of(RuntimeDiag::InvalidArgument).commit(...)`
→ 让流水线按 `hasErrors()` 收尾。判据是"具名诊断 + 非零退出 + **不 abort**"。
它同时是**以后所有 pass 参数校验**的正路，所以值得单开一笔。