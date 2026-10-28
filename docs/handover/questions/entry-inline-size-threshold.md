# 入口的内联尺寸阈值：`main` 是 9，判据是 8

`pass_export_loop` 红在导出的最后一步：

```
(export_loop)…/test/cases/modules/onnx/export_loop.cml:26:1: [Error]: Translation pass
'onnx::export_model' refused the graph: the entry graph still contains a call to 'main' that was
not inlined, so no onnx.ex…
```

本页报的是这条拒绝背后的读数：被拒的调用是 `main`，"太小"判据数是 9、阈值是 8。

## 我量到的

**一处更正。** 我们先前把这段读数解成"第 9 格落在内联折出的 LOOP 的 `LoopCond` 区"，并据此判"计数口径漏了一条豁免"。那个判断**作废**：读数的 `r` 是**区域 id**、`k` 才是**区域种类**，`r2` 不等于 `LoopCond`（`GCRegionKind::LoopCond` 是 `k2`）。`include/camel/runtime/graph.h#GCRegionKind` 里 `Root = 0, Arm = 1, LoopCond = 2, LoopBody = 3, Inline = 4`。

诊断打在 `src/passes/opt/inline/engine.cpp#isSmallRuntimeSubgraphForInline` 的拒绝处前一行，同一个种子、同一棵树的**三次读数一致**：

```
[COUNT-DIAG] callee=main computed=9
  0:kind11:r0/k0 1:kind11:r0/k0 3:kind11:r0/k0 4:kind11:r0/k0
  6:kind11:r2/k1 7:kind7:r0/k0 8:kind6:r0/k0 12:kind11:r0/k0 14:kind11:r0/k0
```

据此：

- 被拒的调用者 `calleeGraph` 是 **`main`**，`computed = 9`；
- 9 格 = **8 格在 `Root`（`k0`）** + **1 格在一条 `Arm` 区（`k1`，owner 是一个 `BRCH`）**；
- 种类是 **7 × OPER（kind 11）** + **1 × JOIN（kind 7）** + **1 × BRCH（kind 6）**；
- 数里的剔除项（`src/passes/opt/inline/engine.cpp#inlinedComputeNodeCount`）：`DATA`／`PORT`／`GATE`／`SYNC`／`DEAD` 与 `src/passes/opt/inline/engine.cpp#nodeCameFromInlineSplice` 认出的 splice 产物——**对这 9 格全部返回 false**，即没有任何一格被误计或被漏计；
- 同一个 `main` 在 `std::macro` 之后是 **13 格**（OPER × 9 + BRCH + JOIN + FILL + FUNC，`FILL` 与 `FUNC` 也在计数里），被 `std::opt` 里后续的 pass 削到 9；9 是**削过之后**的数，不是初始的。

**结论 B。** 9 格都能对上"该算"：没有常量、参数、gate 或 splice 产物被误计，`Arm` 区里那一格也是真算子。所以 **`main` 真的是 9 > 8**，拒绝内联是按判据办事，不是计数口径的缺陷。真正被卡住的是**入口自己**：`main` 经 `model` 间接到达 `halve`／`iterate` 的递归，于是它落进"到达递归的函数只在够小时才内联"这条，而它 9 格、阈值 8 格。

## 要问的一句

这条判据对**入口**该怎么落地——**是抬高入口自身的阈值（入口不算递归体，它本来就只被调用一次），还是把 `main` 压到 ≤ 8（如果是后者，你指哪一格该被消掉）？** 我们只量到 9 格与 8 格这两个数，不替这条判据做决定。

## 没量到的

- 这 6 格是 `std::opt` 里哪一个 pass 削掉的（量的只是削前 13、削后 9）；
- `Arm` 区里那一格（ref 6）是谁放的：`macro` 之后的 `main` 已有一个 `BRCH`，本页只量到它落在 `Arm` 区、owner 是 `BRCH`，没有量它是源码里 `sync` 的臂还是某个 pass 加的；
- 判据对其它入口（别的用例里被 `export_model` 包住的函数）是否同样卡住，本页只量了 `pass_export_loop` 一条；
- 阈值 8 的来源（`InlineRewriteConfig::smallSubgraphMaxComputeNodes` 的取值与其依据）没有量。

本页只报测量与一句询问：不改代码、计划、断言或生成物。