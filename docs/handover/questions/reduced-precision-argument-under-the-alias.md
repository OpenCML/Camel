# 别名之下，"约简精度数组"的排除前提已假（`operator %` 的 bf16/f16 分叉）

Author: 协调者（读数与判定点由 `handover/h457-flip-passes` 车道实测，2026-10-25）
Status: open

## 一句话

`Tensor` 成为数值数组的别名之后，`src/compile/gir/builder_operators.cpp` 里"张量参数不是 `ArrayType`、
所以张量不算约简精度数组"这个前提不再成立；`Tensor<bf16, [3]>` 与 `bf16[]`/`f16[]` 现在同型，
代码分不开它们。今天没有测试钉住这一分叉（潜在行为变化），需要一句裁定。

## 逐字读数（主机 `out/latest/bin/camel`，翻转线 = `handover/h443-flip-survey` + develop）

- 判定点：`src/compile/gir/builder_operators.cpp:1104-1119` 的 `reducedPrecisionArgumentOf`
  （判据 `TypeCode::Array` + 元素类型码），其注释 `:1096-1098` 的前提逐字写着"张量参数的类型不是 `ArrayType`"；
  触发点 `:2578-2584`、`:2830-2836`。
- 期望（注释意图）：`Tensor<bf16, [3]>` 走**张量模块**的重载。
- 实际逐字：
  `ReducedPrecisionUnsupported: The call of 'operator %' cannot take a value of the reduced-precision element type 'bf16'`
  —— 与同一算子的 `f16[]` 版本**只差 `bf16`/`f16`**，也就是说别名之下二者同型，代码已无法区分。
- 钉住它的测试：**无**（`grep` 该消息在 `test/` 下无命中；车道报告"无测试钉住，等裁定，未改"）。

## 相关裁定与设计原则（供作者定位）

- `docs/handover/reviews/questions-2026-10-23.md`：`tensor-alias-transition`（`Tensor` 是别名、
  元素类型与秩随值）、`reduced-element-epsilon-and-rounding`（约简精度的 epsilon 与舍入，见
  `docs/handover/reviews/questions-2026-10-24-evening.md` 同一节）、`f32-scalar-to-the-device`、`rank-zero-tensor`。
- 设计原则 9.x 的约简精度与 5.1 的赋值/转换规则（中文本为准）。

## 需要作者回答的两句

1. `Tensor<bf16, [3]>`（元素类型写定的张量）上的 `operator %` 应当**走张量模块的重载**，
   还是应当与同元素类型的**数组**一样被 `ReducedPrecisionUnsupported` 拒绝？
2. 若走前者：在别名之下按什么判据区分"张量参数"与"数组参数"？
   （今天唯一还在按**名字**认张量的地方是 `src/compile/gct/builder.cpp:5772` 的 `base->toString() == "Tensor"`，
   那是 P4 删除步要拿掉的；若此处也要一个判据，请一并说清它是"声明处的拼法"还是别的。）

## 落地注意

- 在裁定之前，车道**未改**任何代码与期望；本项没有测试钉住，所以现状是"静默的行为变化"，不改不会变红。
- 若答案涉及"张量模块的重载面"，请一并说明 P4 删除步（删模块 `TensorType`、删 `iter`、删裸 `Tensor` 形参臂）
  之后该重载面还在不在。