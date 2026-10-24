# 数值数组的伴随：拒绝已消失，剩下的是「谁改这几条用例」与数组打印的小数位（实测，2026-10-24，h446 lane）

本文件只报两件**实测**到、且**本 lane 不动**的事。前提裁定都在
`docs/handover/reviews/questions-2026-10-23.md`：`rank-zero-tensor`（秩 0 就是元素类型本身）、
`unstated-element-type-scalar-and-print`（未述元素类型的数组）、`tensor-alias-transition`（别名过渡）、
以及 `bare-tensor-parameter-instantiation-measured` 的**更正**（裸 `Tensor` 形参实例化撤销，改由 P4
把形参改写成显式泛型，**相关用例按终态写法改写，不是改成拒绝**）。本 lane 没有自己发明语义。

## 一、`new`/`reshape`/`concat`/`cast` 对数值数组的拒绝已经无法触发，改这四条用例归谁

这四处规则（`modules/tensor/ops/{creation,layout,elementwise}_ops.cpp`）在 develop 上拒绝「依赖可微值的数值
数组」——当时的判据是 `isTensorNode`（那时是 `TensorType`），`float[]` 不是张量，于是「本模块没有把张量的
梯度带回数组的算子，会被静默丢成 0」。翻转后 `isTensorNode` 就是 `isNumericArrayType`
（`modules/tensor/type.cpp`），数组**就是**张量，判据恒真、拒绝恒不触发，而梯度真的被静默丢了：实测四条
用例各差一次 `exit 0`，且**值是对的**：

```
slice_grad_refuse_reshape_array   实际 "10.0 { w: 5.0 }"   （期望：exit 1 + 拒绝诊断）
slice_grad_refuse_concat_array    实际 "7.0 { w: 2.0 }"
refuse_new_array                  实际 "[1.0, 1.0]"
refuse_cast_array                 实际 "[1.0, 1.0, 1.0]"
concat_array_part.skipped_and_refused
                                  实际 "concat_array_part sum=5.0 sq=13.0" + "[1.0, 1.0]"
```

本 lane 按 9.9 把伴随写出来（`reshape` 走 `tensor:shape`+`tensor:reshape`，`concat` 两边各取 dy 的切片，
`new` 直通，`cast` 用 `tensor:cast_like` 回到值自己的宽度，新助手 `tensorCastTo`），四条拒绝代码删除。
**这五条用例现在断言的是「必须拒绝」，而按裁定梯度可以算、且算对了**：它们的期望要改写成断言梯度值
（`grad` 与手写伴随逐位比较），这属于「按终态改写用例」的一类，与本 lane 的改动分开。

**要裁定的问题**：这五条用例的改写在 `tensor-alias-transition` 的 P4 清单里，还是单独一片？
（本 lane 的读数：改写后应转绿五条；不改写则它们红在「期望拒绝、实际给出正确答案」。）

## 二、`f64_broadcast_grad` 与另外 15 条：数值全对，只差数组打印的小数位

`f64_broadcast_grad` 的四个判据里，距离 `a_x=0.0 a_p0=0.0 wa=0.0` 与 `wp [16777217.0, 2.0, 16777217.0]`
**都成立了**（即 f64 累加器跑了、宽窄两对的梯度都对）；唯一不成立的是
`gx tensor([[        0.5,        0.25,       2.],` —— 今天数组打印把嵌套数组按行折行，读到的行首是
`[0.5, 0.25, 2.0], 0.5, 0.25, 2.0]]`，而计划里的断言写的是 tensor 打印形式。

同一形态在 `second_order`（`[6.0, -12.0, 3.0]` vs `tensor([      6.,     -12.,       3.], dtype=float32)`）、
`grad_closure`/`grad_function_values*`/`grad_effects_kept`/`grad_function_values_arms`（`[0.7692267, …]` vs
`tensor([ 0.7692, …], dtype=float32)`）上重复：共 15 条用例、27 条红，**值全部对得上计划里的期望**。
这就是 `unstated-element-type-scalar-and-print.md` 第二节那条「打印形式由作者裁定」的范围，本 lane 只测量。
（唯一新读数：`transform<...>` 的 `{:.4f}` 期望 4 位小数，今天数组打印给全精度 `0.7692267`，即
`unstated-element-type-scalar-and-print.md` 里那个「数组打印机的对齐/位数规则」也覆盖 `transform` 的参数化格式。）