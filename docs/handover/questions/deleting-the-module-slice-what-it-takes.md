# 删除模块的 `tensor:slice`：实测红清单与剩下的前置

车道：`handover/h585-slice-all-calls`（族 = 核心接管全部 `slice(...)` 调用）。

## 一、是什么

裁定第 2/3 条要把切片调用整体交给核心，模块的 `tensor:slice` 随之删除（`stdlib/signatures/tensor.cml` 的声明、`modules/tensor/ops/layout_ops.cpp` 的 OpDef/内核/推断/伴随、`modules/tensor/passes/index_eval.cpp` 的折叠项）。删除本身是机械的，难的是删掉之后谁答：今天有一批用例用 `import { slice } from tensor` 把模块的算子绑到 `slice` 这个名字上，模块一旦不再导出，这些调用全部落到核心的规则上，读法与类型都换主人。

本页记的是**实测**：删掉之后有多少红、每条为什么、哪些已在本轮闭合。

## 二、逐字证据（同一命令的两个读数）

命令：`-Plan test/plans/feat/modules,test/plans/feat/exemplars,test/plans/feat/opt -Tier fast -Jobs 8`

- 删除 + 前向 ONNX 落点补齐**之前**：**32 红**。
- 删除 + 前向落点补齐（本车道这一轮）之后：**17 红**。

剩下的 17 条，按根因：

**(a) 反向还缺一条 ONNX 落点 —— 2 条。** `export_gated_gru_step`、`ort_gated_gru_step`，逐字：

```
onnx.export_model: in 'gated.pb': operator ':tangent/scatter_slice_arr' has no ONNX lowering
```

核心切片的**伴随**是 `:tangent/scatter_slice_arr`（`src/builtin/operators/derivative.cpp`）。前向的 `:op/slice_sub` 落点本轮已补（`modules/onnx/lowering.cpp` 的 `lowerSliceSub`，注册 `add(":op/slice_sub", lowerSliceSub, 10)`），反向的落点（`ScatterND`/`Pad` 一类）还没有。

**(b) 结果类型不写 extent —— 7 条。** `train_step.loss.nvm/.fvm/.jit`、`si_computed_slice`、`instances`、`declaration_evaluator`、`declared_numeric_semantics`。逐字（前者）：

```
train_step.cml:182:5: [Error]: This value has type 'int[?, ?]', which is not the declared type 'int[8, 16]'.
```

后一条逐字：`operator tensor:slice is not listed`。**硬前置，等作者的裁定**（`slice-result-type-states-no-extent.md`）。

**(c) 模块读法消失，同批必须改语义的用例 —— 4 条。** `in_range`（负常数界：核心在书写处故障）、`slice_runtime`（步长 0：同样在书写处故障）、`shape_diagnostics/slice`（措辞换成核心的 `Invalid argument of '[]': a slice axis must be between 0 and 1, not 3.`）、`slice_bounds_rejected_before_cuda`（逐字 `tiles: outside node 4: unsupported or effectful operator ':op/slice_sub'`：tiles 的索引后端只认模块的切片节点）。另有 `ort_slice_start` 的 MODE=constant 也用负常数界（`slice(x, 0, -2, 3)`），必须重写。

**(d) 生成物 —— 2 条**（`registry.docs_complete`、`registry.reference_is_generated`）：脚本重出。

**(e) 本轮已闭合。** 前向 ONNX 落点闭合了 9 条里的 7 条（`export_slice_loop`、`export_gated_gru_forward`、`ort_slice_bounds`、`ort_time_axis`、`pass_export_slice_loop`、`exemplars.04_staticness` 等）；核心伴随改成收 4–6 词闭合了 `tensor_grad_check.*` 8 条（它们原来是 `Derivative of ':op/slice_sub' expects 6 inputs ..., got 5`）。

## 三、今天的读数

删除态的四个读数（`-Check all -NoBuild`）：`stdlib-reference --verify-ops` **绿**，算子重载数 614 → **613**（声明与注册同时删，两侧仍一致——这就是"先 #2 再删"的顺序约束今天仍然成立的样子）；`camel-for-ai` **112061 字符 / 1 problem，与删除前同数**（不减少）；`registry.docs_complete` 仍是 `19 documentation problem(s) in 425 compiled example(s)`（前后同数）；8 项检查里仍只有 `camel-for-ai` 一条红。

**一条新暴露、本车道未定位到行的现象**：删除态下 `ort_slice_start` 的 MODE=constant（`constants(x) = slice(x, 0, -2, 3)`）不是被书写处拒绝，而是导出时逐字报

```
onnx.export_model: ':op/slice_sub': argument 1 must be an export-time constant integer (it depends on the model input)
```

该调用的轴是字面量 `0`，不该非常量；同一轮里 `export_slice_loop` 的函数形式（轴是字面量 `1`、两个界是运行期值）导出正常，所以差异落在**负常数界**这一支。记在这里，与 (c) 的那次重写一起查。

## 四、选项与代价

落地顺序只有一个可用的：

1. **（a）反向 ONNX 落点**：给 `:tangent/scatter_slice_arr` 一个 `ScatterND`/`Pad` 落点。代价：新 lowering + 一条 ORT 比对用例（像本轮 `ort_core_slice` 那样），仍属 `modules/onnx/**`。
2. **（b）等裁定**：结果类型要么看见 statics，要么把"精确 extent"从语言里去掉（7 条用例跟着变）。
3. **（c）(d) 同批改**：4 条用例 + 生成物 + `ort_slice_start` 的重写，与删除在同一个提交里。
4. 删除**不要**单独落地：它把 (b) 的 7 条红留在 HEAD。本车道把删除态做成"只在测量时应用"的状态，工作区保持提交态。

代价：删除那一个提交要同时改 5 个用例、2 个生成物、1 个 tiles 后端（或它的用例），并让 `--verify-ops` 的重载数从 614 落到 613；不先做 (a)(b)，这个提交不可能绿。