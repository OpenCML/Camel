# 张量上的复合赋值在翻之后成了编译错误

## 现象

`test/plans/feat/modules/tensor/compound_assign.plan.toml`
（`feat.modules.tensor.compound_assign: t += u on a tensor computes a new tensor`）的 6 个用例
（`.nvm` `.fvm` `.jit` `.prl` `.opt` `.inline`，外加 `poison:modules.tensor.compound_assign` 变体）
在本分支上全部变成编译错误：

```
(compound_assign)/test/cases/modules/tensor/compound_assign.cml:33:5: [Error]: No matching function
found for argument types: <...>   (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

候选表里只有内建的标量候选（`<:op/assn_add_f>: (var f32, f32) => f32`、`assn_add_f6`、`assn_mul_c` …），
没有任何张量候选。这几例在翻之前（`origin/develop`）是绿的。

## 原因：不是漏改一处门，是裁决的直接后果

`src/compile/gir/builder_operators.cpp` 的 `compoundAssignmentByRebinding` 只对「模块定义的类型」
推导复合赋值，注释把这条规则和它点名的例外写得很清楚：

> Only for a type a module defines. The built-in types have their own operators, and
> where one is missing (`%=` on a float, `+=` on an array) that stays an error.

翻之前 `Tensor` 是张量模块**定义**的类型，`t += u` 走「按重绑定推导」这条路；翻之后 `Tensor`
就是内建的数组类型，于是这条推导按设计拒绝它——`+=` on an array 正是注释点名的那个「仍然是错误」
的例子。所以这 6 例的红不是漏改了一处判断，而是 `Tensor` 变成内建类型之后必然掉出来的一格。

## 需要作者裁决的两条路

用例自己的文档说这是有意的行为（design principles 4.2）：

> `t += u` on a tensor computes the new tensor and binds `t` to it, as for a number …

1. **张量模块自己声明** `assn_add` / `assn_sub` / `assn_mul` / `assn_div`（参数为数值数组），
   行为与推导出来的重绑定一致；用例与文档不动。这条路要新声明四个算子，或者让
   `compoundAssignmentByRebinding` 认「模块导出的类型」而不只是「模块定义的类型」。
2. **裁决说终态里张量上的复合赋值就是错误**：6 个用例与 `compound_assign.cml` 改成「拒绝」形态，
   文档同步。注意最后一条的代价：`ArrayType::numericUnknown()` 同时是每个 `float[]` 的类型，
   所以「让推导也接受它」会顺带让普通数组的 `+=` 不再报错——那是另一条语义改动，不在本分支手里。

本 lane 不自行选择：这既不是漏改的门，也不是期望文本，落了任何一条都是替作者改语义。
现状：分支上这 7 例保持红，等这一条裁决。
## 路线 1 会动到哪里（给作者的回答材料，2026-10-24 追加）

路线 1 = 让 `compoundAssignmentByRebinding` 认「模块**声明**了该普通算子」的情形，而不是只认
「模块**定义**的类型」。会动的代码只有 `src/compile/gir/builder_operators.cpp` 里那道判断
（`hasProgramOperatorOverloads` 与「target 是否为模块定义的类型」这两处），加上它上面那段注释。

代价与它为什么大概率是对的：P4 终态里 `Tensor<T, [d0, d1]>` **就是**数值数组类型，而张量模块今天
已经为数值数组声明了 `+`、`*` 等（这也是 `u + u` 能编过的原因）。复合赋值跟随那个 `+`，等于说
普通数值数组经由张量模块的 `+` 也获得 `+=`——这与 `+` 已有的放宽是同一件事，不是新的表面。
若作者要保留「普通数组的 `+=` 仍是错误」这条，路线 3（拒绝 + 改 6 个用例）是唯一自洽的选择；
路线 2（模块新声明四个 `assn_*`）代价最大，且会把「复合赋值是重绑定」这条语义从推导搬进模块。
