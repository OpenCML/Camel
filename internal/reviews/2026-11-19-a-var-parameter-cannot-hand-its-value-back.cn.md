# 一个 `var` 形参没有通道把新值交回来

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `943d97aee`）
**状态**：待裁（本页写死默认建议）
**触发**：车道 N 按设计页做 S4b（删 `var` 形参的单元、调用处重绑），第一步量「被调者的新值怎么回来」时量到没有这条路。

## 1. 事实（读数，不是推理）

设计页 `internal/plans/2026-11-16-direct-ssa-construction.cn.md` §1 的最后一条写着：

> 「`var` 形参：值进（绑定成初值）+ 值出（随返回值打包，`callWriteMasks_`/`callValueTypeOf` 已经会打包被写单元）；
> 调用处把实参的声明重绑到那个新值。**写回就是调用处的一次重绑**，没有回写单元、没有 `:var/after`、没有 `:var/take`。」

前半条（「已经会打包被写单元」）是真的，但它打包的东西不是设计页以为的那个。唯一的打包实现是
`src/execute/graph_runtime_support.cpp` 的 `camel::execute::runtimeFuncValue`：

```cpp
// The versions are the call's With inputs at the callee's capture ports: the declared `with`
// arguments come first, then the captures in the order the callee made them.
const size_t at = declaredWith + k;
...
parts.push_back(withValues[at]);
```

`withValues` 是**调用方**传进来的 With 输入槽的值（`src/execute/macro/eval.cpp`：`frame->get(...withInputs...)`，
`frame` 是调用方的帧）。所以：

- 对**按引用捕获**它成立，因为交出去的就是那只共享的格子，被调者改的是同一个盒子，读回来自然是新值
  （`Builder::callValueTypeOf` 的注释也这么写：the version of a variable is a cell）；
- 对**按值的形参**它不成立，而且**不会报错，只会静默拿到调用前的值**：调用方槽里放的就是那一下传进去的值，
  被调者的终值在**被调者的帧**里，没有任何一条路把它带出来。

同一件事还有两条旁证：

1. 掩码只有 32 位、按**捕获表**编号（`src/compile/gir/builder_expr.cpp#Builder::captureWriteMaskOf`：
   `GUARD(k < 32, ...)`；载荷是 `include/camel/runtime/graph.h` 的 `GCFuncRef{ref, reserved}`），
   `var` 形参不在捕获表里（`captures.cpp` 的「门一」：形参不是捕获候选——设计页 §3 说这道门「随 S4b 消失」，
   今天还在）。
2. `var` 形参今天是**格子**：`src/compile/gir/builder.cpp` 给它 `TupleType::cell(type)` 的端口并记进
   `variables_`；调用处 `Builder::passArgument` 交出去的是 `asParameterCell(version)`。整条链路成立，正因为
   双方共用一只盒子。S4b 要拿掉的正是这只盒子。

## 2. 所以 S4b（连同它的前置 S4a）不落在车道 N 的授权文件里

「值出」要有一条通道。能选的就两条，代价都落在 `src/compile/gir/` 之外：

- **路 A：执行层加一条「形参终值随返回打包」的协议。** 打包点从调用方的 With 槽改/增为「被调帧的形参终值」。
  要动的文件：`include/camel/runtime/graph.h`（`GCFuncRef` 载荷）、`src/runtime/draft_construct.cpp`、
  `src/runtime/graph.cpp`、`src/runtime/draft_inline.cpp`，再加 `runtimeFuncValue` 的四个调用点
  （`src/execute/macro/eval.cpp`、`src/passes/sched/nodevm/nodevm.cpp`、
  `src/passes/sched/fastvm/runtime_support.cpp`、`src/passes/sched/taskflow/taskflow.cpp`）与
  `std::cpp` 发射器（`src/passes/trans/cpp/function_emitter.cpp` 读同一个掩码）。
  **代价**：跨 `src/execute`、`src/passes/sched`、`src/passes/trans/cpp`、`include/camel/runtime`——
  与 F（执行计划归位）、H（`exec_plan.cpp`/`storage_reuse`）同一批文件，得排先后；而且它是一个**执行协议**改动，
  出错的形态是静默算错，不是编译错误。
- **路 B：GIR-only，被调者体在出口把形参终值拼进返回值。** 调用点把它当调用值的一部分读，`callValueTypeOf`
  拼同一个元组。**代价**：被调者图的返回类型不再等于 `FunctionType::exitType()`，而 `exitType` 是导出、内联、
  类型推断、`std::cpp` 一起读的声明；若改成让**语言级签名**带上写回（`(x: var int) => int` 变成
  `(x: var int) => (int, int)`），那是改语言类型，不是改实现——`camel inspect` 与诊断里的签名会跟着变。

## 3. 默认建议（写死，按它开工）

**走路 A 的最小切片，并且只先做一条后端**：`GCFuncRef` 的 `reserved` 旁边加一个「哪些形参要交回」的掩码，
`runtimeFuncValue` 多收一份「被调帧的形参终值」槽数组，先在 `std::nvm` 上做通、让 `feat/linear/byref` 全绿、
`var_new` 从 214 掉到 188，其余三个 VM 与 `std::cpp` 各补一处，最后才删 `:var/after`/`:var/take`。
路 B 只留作对照实验，不动签名。

**若不采纳这个默认，就请回一句「S4b 押后」**：按现协议它在 `src/compile/gir/` 里做不完，谁做都得先改第 2 节列的那些文件。

## 4. 复现

```
git grep -n "parts.push_back(withValues\[at\])" -- src/execute
git grep -n "callWriteMasks_\|callValueTypeOf" -- src/compile/gir
camel test/cases/linear/byref/var_param_reference.cml std::jgir     # 形参端口今天仍是 `var int`
```

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

走"多返回"，不走路 A 的执行层旁路。原则 3.5："`f(x)` 而形参是 `var y`，等于 `x = f'(x)`，`f'` 多返回 `y` 的最终值"；3.1："`var` 形参的最终值是函数多返回的一个值，由调用处写回"。终值是图上的一份返回值，语言签名不变（作者 `8f7b9687f`："the graph keeps the declared function type"）。实现归前端分阶段：核心抽象第 8 节说"`var` 只降低成 SSA（原则 3.5），`Ref` 是唯一的单元"。
