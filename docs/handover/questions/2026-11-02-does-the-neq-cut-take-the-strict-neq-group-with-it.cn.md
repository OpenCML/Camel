# `ne_` 那 22 行里有没有 `__strict_neq__` 的 11 行？有。而删除的前提是 `not` 已经存在

**答案：22 = `__neq__` 的 11 条 + `__strict_neq__` 抄它们的 11 条；已删、已测、全绿。** 这一页记的是删除前必须先立的一条判据，和 22 行按这条判据的分类。

## 一条判据

**凡是 `ne_*` 存在的地方，`not` 必须已经存在。** 因为 `ne_*` 是"取反"在算子表里的唯一写法：删掉它，`!=`（和 `!==`）就必须用 `!(==)`（`!(===)`）表达，而那需要一次取反 —— 取反不可表达的组，`ne_*` 是它唯一的表达方式。先有 `not`，再删 `ne_*`，顺序不能反。

## 22 行按这条判据分两类

1. **11 条 `__neq__` 的**（9 条显式 `:op/ne_{i,l,f,d,b,s,bytes,c,z}` + `CAMEL_HALF_COMPARE("ne")` 的 `ne_bf`、`ne_f6`）：`not` **早就存在** —— `:op/not: (bool) => bool`，而 `==`/`===` 的结果都是 `bool`，标量的取反一步就够。**这一类删了不欠账**：`1 != 2` 得 `eq_l` + `not`，`bf16`/`f16` 的 `!=` 走 `!(eq_bf)`/`!(eq_f6)` 照旧可用（**`ne_bf`/`ne_f6` 不需要"补一个 bf16/f16 的 `not`"：被取反的是比较结果的 `bool`，不是 bf16/f16 本身**）。
2. **11 条 `__strict_neq__` 抄来的**：`!==` 在数组上要的取反是 **`!` 作用在 `bool[]`**，而这一条当时**不存在**（`__not__` 只有 `(bool) => bool`，tensor 内核的 `UnaryOp` 里没有 `Not`）。**这一类必须先把 `!bool[]` 补上再删**，否则 `!==` 的标量与数组两半同时没有规则 —— 实测过：删了之后 `1 !== 2` 报"可用重载：空"，`modules.tensor.comparison:28` 报 `operator '!' is not defined`。

## 删除半的阻塞是 `__not__` 的内核，不是注册表

**不是"`__neq__` 组删不掉"，也不是"注册表里有别的东西引用它们"**：注册表这一侧是一条一条删干净的（`globals.cpp` 的 11 条、`builtin.cpp` 的 21 行内核与免故障清单、`onnx/lowering.cpp` 的 `:op/ne_*` 一行、`struct.cpp` 的 `__ne_bytes__`）。**唯一的阻塞是"取反可表达"：`__not__` 的 `bool[]` 内核落地与否，决定那 11 条能不能删。** 后来的人若看到这里还红，先查 `__not__` 有没有 `bool[]` 条目，不要回头翻注册表。

## 本笔的读数（`develop` 2a573d12f 的合并树上）

- `camel inspect --ops` 最小程序：**352 → 330，正好 −22**；`:op/ne_*` **剩 0 行**。
- `1 === 1, 1 !== 2, 1 !== 1, "a" !== "b"` → `true true false true`；图中 `1 !== 2` 是 **`eq_l` + `not`**。
- `f16` 的 `a != b`/`a !== b`/`a == b` → `true true false`；`bf16` 的 `a != b` → `false`。
- `test/cases/modules/tensor/comparison.cml` 整份跑通（数组 `!==` 逐元素正确）。
- 整树 **fast** 层：**8447 总数 / 8435 过 / 0 失败 / 10 skip / 2 known failures**；`stdlib-reference --verify-ops` **exit 0**。

## 给 `lane-m5` 的一句话

`__not__` 的 `bool[]` 条目本笔已经补上（tensor 内核 `UnaryOp::Not`，`notInfer` 只接受 `bool` 元素类型），所以那 11 条不需要等它落地；**bf16/f16 的 `Not` 也不需要** —— 见上面第 1 类。若 m5 也在做同一条，两边会撞在 `elementwise.h`、`elementwise.cpp`、`elementwise_ops.cpp` 三个文件上。