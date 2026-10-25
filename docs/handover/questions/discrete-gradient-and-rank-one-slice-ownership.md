# 两处待裁：全离散 struct 的梯度，与 `t[a:b]` 的收窄线（实测，2026-10-25，h573 lane）

（前提：`docs/handover/reviews/questions-2026-10-24-evening.md` §tensor-gradient-structure-and-slice-adjoint
的两条裁定：①离散字段在梯度里照样有，值是同形状同元素类型的零；②`t[a:b]` 与 `slice(t, 0, a, b)`
是同一个切片节点，只留 `sliceVjp`。本 lane 按这两条实现；下面两处是裁定没覆盖、而今天的用例与实测会给
出不同答案的地方：只报读数与做法，不替作者定。）

## 一、所有字段都离散的 struct：还有梯度吗？

裁定①说"梯度结构体和模型同一个类型，`labels: i64[..]` 的梯度就是同形状同元素类型的零"。照字面推，
`type Plain = { s: string, n: int }` 也该有梯度（`{ n: int }`，值是零）；但绿用例
`test/cases/modules/autodiff/grad_refuse_struct.cml` 要求 `grad(plain)<{s: 'a', n: 2}>(3.0)` 被拒
（`SemanticDiag::NotDifferentiable`，报文 "has type Plain, which has no gradient"，期望 exit 1）。

本 lane 采保守做法（`src/core/derivative.cpp`，`keepsInTangent` / `computeTangentType` 的 Struct 分支）：

| 类型 | 切空间 | 读数 |
|---|---|---|
| `{ weights: f32[..], labels: i64[..] }` | `{ weights: f32[..], labels: i64[..] }` | `discrete_zero ... gather=(1.0,6.0,0.0) zero=1.0`；`gather_discrete_index_zero value=1.0 weights_grad=6.0 index_grad=0.0` |
| `{ s: string, n: int }` | 无 | `grad_refuse_struct` 保持绿（仍拒） |

即：**struct 只要有至少一个"真"切空间字段才给出切空间**；离散字段（标量 `int`/`bool`、整数张量）
在该 struct 的切空间里照样保留、值为零。请作者裁：**全离散的模型是否也该拿到一份"全零梯度"**
（那 `grad_refuse_struct` 的期望要改），还是"一个可训练字段都没有"就继续按无梯度拒。

## 二、`t[a:b]` 的收窄线：`T[]` 归核心，其余数值数组归模块

裁定②要求下标与函数式是同一个切片节点。本 lane 已把参数表对齐：

- 核心的下标降低改成切片节点的参数表 `(值, 轴, start, end, step, 写没写掩码)`，轴对下标恒为常量 0
  （`src/compile/gct/builder.cpp`）；核心的 `:op/slice_sub` 与张量模块的 `tensor:slice` 因此**同一个
  参数表**，下标只多一个掩码字。
- 张量模块只留一个 `slice` 算子（`exports = {"slice", "__slice__"}`，`modules/tensor/ops/layout_ops.cpp`），
  一个 `sliceInfer`/`sliceKernel`/`sliceVjp`；`sliceSubVjp` 删除。
- 实测（`std::gir`）：`reshape(...[6])` 上的 `x[1:4]` 与 `slice(x, 0, 1, 4)` 都打 `tensor:slice`；
  `sum(x[1:4])` 的伴随报 `sum=3.0`（改动前同一条报 `[Error]: ... sum: Unsupported Tensor dtype`）。

谁答一条下标，取决于值是不是"程序自己的数组"（`src/builtin/modules/globals.cpp`，`isTensorSubscript`）：

| 值的类型 | 谁答 | 读数 |
|---|---|---|
| `f32[..]`（开放秩，`Tensor` 的过渡拼写） | 模块 | `slice_grad ... value=33.0 nonzero=70.0`；`slice_open_end dot=8.0 sq=13.0 sum=5.0` |
| `f32[6]`（秩 1、**extent 已述**） | 模块 | 探针 `sum(a[1:4])` 的伴随 `sum=3.0`（收窄前这条 exit 1） |
| `float[]` / `int[]`（秩 1、**extent 未述**，`T[]`） | 核心 | `array_slice_grad ... nonzero=115.0`（只 import autodiff）；`stdlib.array.*` 等一并保持 |

这里与 `t[idx]` 的现行分工**只差一档**：h446 的收窄是"秩 ≥2 或开放秩归模块，秩-1（含已述 extent）
归核心"（`docs/handover/questions/subscript-operator-choice-and-the-rank-two-refusal.md` §二，31 条
不 import tensor 的秩-1 程序是那次的读数）。本 lane 把切片这条线再往里收一格，因为**秩-1 且 extent
已述**的值就是张量算子返回的东西：不收这一格，`reshape(...[6])` 上的 `sum(a[1:4])` 的伴随仍然是槽数组、
`sum(g)` 仍然报 `Unsupported Tensor dtype`（本 lane 探针实测）。

请作者裁两点：

1. 切片的收窄线是不是"`T[]` 归核心、其余数值数组归模块"？（h446 那 31 条都是 `T[]`，本 lane 的收窄
   没有碰到它们——全 fast 层 6234 条前后对比只少了本 lane 的 9 条红，无新红。）
2. `t[idx]` 是否也该跟着收这一格（同上，秩-1 且 extent 已述的数值数组交给模块），还是两条 `[]`
   各按各的线？P4 别名落地后两种拼写同型，这条线大概就该消失。