# 元素类型约定：本车道实测后仍红的项，逐项分类

车道：`handover/h459-flip-kernels`（族=内核与示例，题=「未述元素类型的值按数组/字面量规则取 `f64`，而模块内核按 `f32` 约定」）。
本文件只记录**实测**与**归属**，不为过渡类型发明语义。落点：`a0a113e69`（族基线）→ `e1de421b3`（本车道两处修复之后）。

## 1. 裸 `Tensor` 形参 / 返回值与写明元素类型的格子（`lrit`、`mnist`）

逐字读数（族基线，`test/plans/feat/modules/nn.plan.toml`）：

```
[FAIL] lrit
    [stdout] lrit.cml:78:14: [Error]: No matching function found for argument types: <> (f32[?, ?], Tensor);
             operator '=' is not defined for these operand types.
[FAIL] mnist
    [stdout] mnist.cml:92:14: [Error]: No matching function found for argument types: <> (f32[?, ?], Tensor);
             operator '=' is not defined for these operand types.
```

判定点：

* `test/cases/modules/nn/lrit.cml:71` `var w = zeros([num_features + 1, 1])` → 格子类型 `f32[?, ?]`；
  `:28` `sync func update(var w: Tensor): Tensor`、`:39` `sync func train(weights: Tensor): Tensor`
  → 结果类型是**未述元素类型**的数组（`ArrayType::numericUnknown()`）。
* `:78` `wait w = train<...>(w)` 落在 `=` 上：`assignableFrom(f32[?, ?], Tensor)` 为假——
  目标写明元素类型、来源未写明，是**收窄**方向，按终态语义应当拒绝。
  `mnist.cml:88,92` 同形。

两条可能的解法，本车道**都没有做，也不该由本车道硬修**：

1. **语言侧**：裸 `Tensor` 形参按调用点实例化（设计原则 5.5）。
   `docs/handover/reviews/questions-2026-10-23.md:375` 已裁定「必须。这是设计原则 5.5 本身」，
   `docs/handover/questions/bare-tensor-parameter-instantiation-measured.md` 是它的实测页。
   该轨落成后这两条自然变绿，**用例一行不用改**。
2. **用例侧**：把两个示例的整条签名链（`predict`/`update`/`train`/`train_step`/`build_one_hot`）
   改写成 `Tensor<f32, [?, ?]>`。本车道不做：那样会把两个教学示例写成非泛型，
   且 `train_step` 的递归返回也要一起写死——正是「为过渡类型发明语义」的反面（改示例去迁就别名）。

结论：**归 1 号轨（裸 `Tensor` 形参实例化）**，本车道只登记不改。

## 2. 五条「必须拒绝」的用例（拒绝已是死代码）

`slice_grad_refuse_reshape_array`、`slice_grad_refuse_concat_array`、`refuse_new_array`、
`refuse_cast_array`、`concat_array_part.skipped_and_refused`：`expect_exit = 1`，实测 `exit 0`。

判据：`modules/tensor/ops/vjp.cpp:67` `isTensorNode` = `isTensorType` = `isNumericArrayType`，
别名之后**数值数组就是张量节点**，所以
`creation_ops.cpp:153`（`new`）、`layout_ops.cpp:642`（`reshape`/`flatten`/`unsqueeze`）、
`:688`（`concat` 的数组部分）、`elementwise_ops.cpp:499`（`cast`）里的
`!isTensorNode(...)` 分支恒不触发。`refuse_cast_array` 已改由元素类型规则报出来：

```
[FAIL] refuse_cast_array
    [stdout] The gradient of a value of type 'float[]' has element type 'f32': operator 'tensor:cast_like' produced it
```

归属：`docs/handover/reviews/questions-2026-10-24-evening.md`（`adjoint-of-a-numeric-array-after-the-alias`）
裁定——**删掉这些恒不触发的拒绝的车道，在同一个提交里把这五条用例改写成梯度值与手写伴随的逐位比较**。
本车道不重复该轨，也不改这五条：它们断言的正是那批拒绝的存在。

## 3. bf16/f16 的 `%`（协调者 2026-10-25 转来的裁定）

裁定原文（转述）：「别名之后没有『张量参数』与『数组参数』之分，只有数值数组；不要找判据去区分它们……
bf16/f16 是与 f32 同等的浮点类型（7.3），数值数组上的 `%` 对它们应当支持（逐元素，在 f32 中算、舍入一次）
——今天的 `ReducedPrecisionUnsupported` 是如实标出的实现缺口，补上内核后消失。」

实测（未做）：

* `src/builtin/modules/globals.cpp:1596-1613`：`__mod__` 只有 `:op/mod_i`(i32)、`:op/mod_l`(i64)
  两个候选，**没有任何浮点 `%`**（f32/f64 也没有）。
* 张量模块没有 `%`：`modules/tensor/ops/elementwise_ops.cpp:522-528` 的二元算子只有
  add/subtract/multiply/divide/pow/maximum/minimum；
  `modules/tensor/kernels/elementwise.h:70` 的 `enum class BinaryOp` 里没有 `Mod`。
* 需要做的是：`BinaryOp` 加 `Mod` 一节 + 逐元素内核（bf16/f16 在 f32 中算、存回时舍入一次，
  与 `unaryInfer` 对 bf16/f16 的处理同一条）+ 在张量模块注册
  `binaryDef<k::BinaryOp::Mod>("mod", {"__mod__", "mod"})`。
  标量 `%`（`test/plans/feat/modules/stdlib_reduced.plan.toml:87-97`，`reduced_arithmetic_refused`）
  问的是两个 bf16 **标量**，裁定的「数值数组」不覆盖它，该条保持。

结论：**本车道已定位到判定点与内核清单，未实现**（本车道预算内没做），交接给补内核的人；
这不是需要再裁定的项，是待做的实现项。

## 4. `resident_loop_declares_its_blocks`（整层 `Compare-Object` 里唯一新增的红）

归属：**不是本车道**，A/B 已证。

* 整层基线（`a0a113e69`）**没有发现任何 `gpu.*` 计划**（`[PLAN]` 里 `gpu` 零条），
  修复后整层那一次发现了 `gpu.tiles` 与 `gpu.profile`（主机上设备可见了）。
* 带本车道改动跑 `test/plans/gpu/tiles.plan.toml`：`total: 101, pass: 88, fail: 13`，
  其中 `[FAIL] resident_loop_declares_its_blocks`；
  把本车道 5 个文件 `git checkout --` 掉重建后重跑同一条计划：`total: 101, pass: 88, fail: 13`，
  `[FAIL] resident_loop_declares_its_blocks` **逐字相同**。
* 读数：`TILES_PLACEMENT {...,"gpu":0,"gpu_required":0,...}`、`TILES_RESIDENCY {...,"body":"host",...}`，
  期望 stdout 含 `Resident loop: PASS`。归 tiles/放置那一轨。