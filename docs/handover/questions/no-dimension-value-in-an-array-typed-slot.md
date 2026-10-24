# 数组类型的槽里装着「没有维度的值」：核心报错，但逐值分辨还没有（实测，2026-10-25，h453 lane）

前提裁定：`docs/handover/reviews/questions-2026-10-23.md` 的 `rank-zero-tensor`（秩 0 就是元素类型
本身，不是秩 0 的数组）；`...-2026-10-24-evening.md` 的 `loop-domain-through-the-tensor-iterator`
（`for` 对数组沿第一维取元素，由核心负责）。本 lane 按裁定把「取第一维元素」的规则放进核心
（`ArrayType::firstDimensionElement`、`Array::firstDimensionElement`），循环的每个驱动器共用它。

## 一、实测：秩 0 值进数组类型的槽，核心报错（逐字）

`test/cases/loops/fault_tensor_rank0.cml`（`func toShape(t: Tensor, s: int[]): Tensor`，把
`reshape([1.0], [])` 的秩 0 值经 `Tensor` 形参与返回位置送到 `for`）：

```
nvm: test/cases/loops/fault_tensor_rank0.cml:13:1: [Error]: An error has occurred: a value with no dimension has no first dimension to iterate over. Check the runtime environment a…
fvm: test/cases/loops/fault_tensor_rank0.cml:16:14: [Error]: An error has occurred: a value with no dimension has no first dimension to iterate over. Check the runtime environment …
```

即 `exit 1`、`RuntimeDiag::RuntimeError`（计划今天期望的行号与消息名不同：期望
`iter: a rank-0 tensor has no first dimension`、行 16；nvm 的 origin 落在 13:1）。守卫在
`include/camel/execute/loop_frame.h` 的 `loopTest`：`Array *array = frame.get<Array *>(...)`，
`array == nullptr` 时报这条错误。秩 0 的值是**标量槽**（不是 `Array`），零值在这里正好是空指针。

## 二、洞：非零标量没有逐值分辨

框架里没有「逐值的运行时类型表」：声明为数组的槽若装着一个**非零**标量（同一个 `Tensor` 形参在别的
程序里可能是 `reshape([2.0], [])`），今天的空指针守卫认不出来，会把它当指针用——与基线里
`tensor:iter` 那条路径同一类危险。`Tensor`（任意秩、未述元素类型）的值按裁定**可以**是元素本身，
所以这不是本 lane 能关的事：任何读数组的操作数都要在运行时分辨「这个槽里是数组，还是元素本身」。

## 三、要裁定

1. 逐值分辨由谁落：给 frame 的槽加逐值类型（或让秩 0 值有自己的标记），还是让 P4 之后的 `Tensor`
   不再覆盖标量（`Tensor` 只表示秩 ≥1？）——`tensor-alias-transition` 的 P4 清单里有没有这一条？
2. `test/cases/loops/fault_tensor_rank0.cml` 与 `err_tensor_rank0.cml` 的期望要不要按 2026-10-23 的
   裁定改写成终态（前者只留 `reshape` 元素个数错误的用例、后者期望行 10 → 13）；本 lane 只给出读数，
   没有改任何 `test/plans/**`。