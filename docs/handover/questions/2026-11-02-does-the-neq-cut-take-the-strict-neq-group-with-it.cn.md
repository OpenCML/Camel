# `ne_` 那 22 行里有没有 `__strict_neq__` 的 11 行？有的话 `!==` 跑在什么上？

一句话：**前端已把 `!=` 建成 `!(==)` 且全绿；把这 22 行删掉会让 `!==`（标量和数组都算）没有一条规则可跑，而 `inspect --ops` 恰好只在这 22 行一起消失时才少 22 行。** 这两件事同时成立的唯一出路是给 tensor 模块补一个逐元素 `!`，那是新能力，不是"删注册行"。

## 读数（都是这台机器上量的，不是推的）

- `camel inspect --ops` 对一个最小程序今天列 **352** 行。
- `__neq__` 组今天 13 行：9 条显式 `:op/ne_{i,l,f,d,b,s,bytes,c,z}` + `:op/ne` 复合 + `CAMEL_HALF_COMPARE("ne")` 的 2 条（`ne_bf`、`ne_f6`），外加 collection 模块导出的 `:coll/neq`。
- `__strict_neq__` 组今天 11 行，**一行都不是写出来的**：`src/builtin/modules/globals.cpp:2335-2345` 把 `__neq__` 的 11 条定签名 resolver 抄了一份。
- 于是：删掉 `__neq__` 的 11 条 `ne_*`（9 条显式 + `CAMEL_HALF_COMPARE("ne")` 一行），`__strict_neq__` 的 11 条自动变空 —— **352 → 330，正好 22 行，且 `:op/ne_*` 剩 0 条**。删 9 条留 2 条 bf16/f16 则只少 18 行（`__neq__` 9 + `__strict_neq__` 9），对不上 22。
- `!==` 不是可重载形式：`isOverloadableOperatorGroup("__strict_neq__")` 为假（`src/core/operator_names.cpp:167-174`，`__strict_eq__`/`__strict_neq__` 只在 `operatorSymbolOf` 的附加表里，不在 `kForms` 里）。所以 `visitExpr` 不会把它送进 `operatorDrefOf`，而是按名字解析 `__strict_neq__` 这个导出组 —— 组一空，报的就是"`operator '!==' is not defined`，可用重载：空"。
- 想把 `!==` 建成 `!(===)`（和 `!=` 之于 `==` 对称，也正是设计原则 4.2 的说法）也不行，因为**数组**那半要 `!` 作用在 `bool[]` 上：`__not__` 今天只有 `<:op/not>: (bool) => bool`；tensor 内核的 `UnaryOp` 里没有 `Not`（`modules/tensor/kernels/elementwise.h:70-82`）；`!==` 在数组上今天走的是 tensor 模块自己导进 `__strict_neq__` 的 `CompareOp::NotEqual`（`modules/tensor/ops/elementwise_ops.cpp:506`）。
- 实测（构建树里 `!==` 建成 `!(===)` 之后）：`1 !== 2` → `true`、`1 === 1` → `true`、`1 !== 1` → `false`、`"a" !== "b"` → `true`，标量全对；但 `test/cases/modules/tensor/comparison.cml:28` 的 `a !== b` 报 `operator '!' is not defined for these operand types`，可用重载只有 `(bool) => bool`。
- 所以：**"少 22 行"和"`!==` 仍然能跑"在没有逐元素 `!` 的前提下互斥**，`!=` 那半不影响这一点。

## 三条路，带代价

1. **给 tensor 补逐元素 `!`**（`UnaryOp::Not` + 把它导进 `__not__`），`!==` 统一建成 `!(===)`，22 行照删。
   代价：内核要过一遍 int/浮点/半精度那几个 switch 和 dtype 表（`elementwise.cpp` 里 `UnaryOp::Neg` 有 3 处 switch）；语言面变大 —— `!xs` 从"不许"变成"逐元素求反"；tensor 那半从 `NotEqual` 走成 `Not(Equal)`，读数要重记。
2. **`!==` 保留自己的标量规则**，也就是 22 行一条都不删（只删 `!=` 那一半根本不存在：`__neq__` 的行同时是 `__strict_neq__` 的来源）。
   代价：`inspect --ops` 仍是 352，22 行死注册留在表里，"少 22 行"这条判据拿不到。
3. **按操作数形状分叉**：`__strict_neq__` 自己的条目命中就用它（数组走 tensor 的 `NotEqual`），没命中就退回 `!(===)`（标量）。实现上是 `visitLinkNode` 里解析失败后重试一次。
   代价：前端按形状选语义，正是这套代码避免的写法；两条语义路径以后会各自长。

## 本笔的默认（已按它做了）

**走第 2 条的本笔版本：这一笔只交前端那半**（`!=` 建成 `!(==)`，判据 1/2/4 全绿），**22 行不动**，因为本笔没有权限决定 `!==` 换成什么。若无人回复，下一步按**第 1 条**做：补 tensor 的逐元素 `!`，`!==` 建成 `!(===)`，然后一次性删掉那 22 行 —— 那样 `inspect --ops` 少 22、`1 != 2` 仍是 `eq_l`+`not`、`!==` 标量与数组都还在，三条同时成立。