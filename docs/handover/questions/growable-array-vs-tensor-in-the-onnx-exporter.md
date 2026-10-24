# 可增长数组与张量在 onnx 导出器里已无法区分 + 10 条 X 桶（h449 lane，2026-10-25）

本文件记两件与本 lane 的 16 条 red 有关、但**判定权不在 onnx 模块**的事。

## 一、翻转抹掉了导出器赖以区分「可增长数组」与「模型的张量」的唯一标记

翻转的终态（`docs/handover/reviews/questions-2026-10-23.md` 的
`unstated-element-type-scalar-and-print 与 tensor-alias-transition`）是 `int[]` ≡
`Tensor<int, [?]>`：**类型层面没有判别器**（`ExtentForm::Kind::Unknown` 同时覆盖 `[]` 与 `?`，
`ArrayType` 只在打印时用 `tensorSpelling` 区分）。而 onnx 导出器必须区分这两件事：

- `export_loop_carried_array_field`：`int[]` 做 Loop 的携带状态（或结构体字段）→ **必须拒绝**
  （裁定：等 H17 的 2、4 项把定长数组形状定下来）；
- `export_gated_gru_step`：GRU 的 `h` 是 `f32[?, ?]`（动态轴张量）→ **必须接受**，否则 26 条
  本来是绿的导出全红（本 lane 实测：把「任何未述 extent 一律拒绝」写进 `isCarriable` 后，
  `export_gated_gru_step`、`ort_gated_gru_step` 立刻从绿转红）。

本 lane 暂时用的**代理判据**是类型打印器自己的规则（`ArrayType::shapeText`：`!anyRank_ &&
extents_.size() == 1 && extents_[0].isUnknown()` 打印成 `[]`，即
`modules/onnx/exporter.cpp` 的 `shapeIsCarriable`）：秩 1 且唯一 extent 未述 = 可增长数组，拒绝；
其余（`Tensor` 本身、`anyRank`、写出的形状含动态轴名）接受。实测这套判据同时让
`export_loop_carried_array_field`（拒绝）与 `export_gated_gru_step`/`ort_gated_gru_step`
（接受）在**同一个**构建里通过（`[SUMMARY] total: 139, pass: 123, fail: 16`）。

请作者定：`T[]` 与 `Tensor<T, [?]>` 是否**应该**在类型上留下一个语义标记（而不是只差打印）？
若应该有，本 lane 的 `shapeIsCarriable` 应换成它；若不应该，请确认「秩 1 未述 extent = 可增长
数组」这条代理判据可以长期留在导出器里（它与
`model-dependent-recursion-unrolls-after-the-flip.md` 第二节的类型精化是同一个歧义）。

## 二、`ArrayType::castSlotFrom` 的报错措辞从 `tensor` 变成了 `array`

`src/core/type/composite/array.cpp:514-516` 的注释写的是「The diagnostic format is the one the
tensor module used for the same check, so moving it here changes no message」，但实测变了：

- 现在：`conversion to 'f32[3, 2]' failed: the array has shape [2, 3]`
- plan（X 桶）期望：`this conversion of a constant always fails: conversion to
  'Tensor<float32, [3, 2]>' failed: the tensor has shape [2, 3]`

`f32[3,2]` → `Tensor<float32, [3, 2]>` 是 P4 拼写迁移的事（本 lane 不改），但
`the tensor has shape` → `the array has shape` 与上面那句注释矛盾，请裁定以哪个为准。

## 三、10 条 X 桶（本 lane 逐条列出，交回给 P4 拼写/消息 lane）

这 10 条**行为全对**（exit code、诊断名、诊断码、行列号都与 plan 一致），唯一不符的是
`expect_output_contains` 里写的是**旧拼写**：`pass` 与 `operator` 两种入口各 5 条。

| # | 用例 | plan 期望（旧拼写） | 当前实测（新拼写） |
| --- | --- | --- | --- |
| 1 | `as_refusal.unproven.pass` | `the conversion to 'Tensor<float32, [2, 3]>' of a value that depends on the model input cannot be checked when the model is exported` | `the conversion to 'f32[2, 3]' of a value that depends on the model input cannot be checked when the model is exported: the value is only known to be a 'f32[?, 3]' …` |
| 2 | `as_refusal.unproven.operator` | 同上 | 同上（作为 `export_model` 调用的错误，带 `as_unproven.cml:13:13`） |
| 3 | `as_refusal.always_fails.pass` | `this conversion to 'Tensor<float32, [3, 2]>' always fails: for the exported inputs the value is a 'Tensor<float32, [2, 3]>'` | `this conversion to 'f32[3, 2]' always fails: for the exported inputs the value is a 'f32[2, 3]'` |
| 4 | `as_refusal.always_fails.operator` | 同上 | 同上（`as_always_fails.cml:12:13`） |
| 5 | `as_refusal.constant_fails.pass` | `this conversion of a constant always fails: conversion to 'Tensor<float32, [3, 2]>' failed: the tensor has shape [2, 3]` | `this conversion of a constant always fails: conversion to 'f32[3, 2]' failed: the array has shape [2, 3]`（另见第二节） |
| 6 | `as_refusal.constant_fails.operator` | 同上 | 同上（`as_constant_fails.cml:16:13`） |
| 7 | `example_refusal.tuple.pass` | `the example given for parameter 'x' is a '(Tensor<float32, [2, 3]>)', but the parameter is declared 'Tensor<float32, [2, 3]>'` | `the example given for parameter 'x' is a '(f32[2, 3])', but the parameter is declared …` |
| 8 | `example_refusal.tuple.operator` | 同上 | 同上（`tuple_example.cml:15:5`） |
| 9 | `example_refusal.shape.pass` | `the example given for parameter 'x' is a 'Tensor<float32, [2, 3]>', but the parameter is declared 'Tensor<float32, [4, 3]>'` | `the example given for parameter 'x' is a 'f32[2, 3]', but …` |
| 10 | `example_refusal.shape.operator` | 同上 | 同上（`example_mismatch.cml:14:5`） |

这 10 条按裁定里「相关用例按终态写法改写（显式泛型或具体元素类型）」的同一条思路，应该把
`expect_output_contains` 改写成终态拼写；本 lane 没有改，因为那属于 X 桶（拼写迁移），
而且改 plan 期望必须与全层其它 lane 的拼写迁移一起做，不能在本 lane 里单独落。