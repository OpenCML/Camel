# `ne_` 那 22 行里有没有 `__strict_neq__` 的 11 行？有。而删除的前提是同元素类型的 `eq_` 已经存在

**答案：22 = `__neq__` 的 11 条 + `__strict_neq__` 抄它们的 11 条；已删、已测、全绿。** 这一页记的是删除前必须先立的一条判据，和 22 行按这条判据的分类。

## 一条判据

**凡是 `ne_*` 存在的地方，同一元素类型的 `eq_*` 必须已经存在。** 因为 `!=` 走的是 `!(==)`：中间的 `==` 交出 `bool`，`!` 只需作用在 `bool` 上，而 `:op/not: (bool) => bool` 一直都在。判据落在 `eq_*` 上，不落在 `not` 上 —— 一个元素类型若有 `ne_*` 却没有 `eq_*`，那 `ne_*` 就是它唯一能被比较的方式，删了就少一种能力；两者都在，`ne_*` 只是多余的那一个。

## 22 行按这条判据分两类

1. **11 条 `__neq__` 的**（9 条显式 `:op/ne_{i,l,f,d,b,s,bytes,c,z}` + `CAMEL_HALF_COMPARE("ne")` 的 `ne_bf`、`ne_f6`）：同元素类型的 `eq_{i,l,f,d,b,s,bytes,c,z}`、`eq_bf`、`eq_f6` **全都在**，多个取反就够。**这一类删了不欠账**：`1 != 2` 得 `eq_l` + `not`，`bf16`/`f16` 的 `!=` 走 `!(bf16 == bf16)` 即 `eq_bf`/`eq_f6` + `not`（实测图里就是 `op/eq_bf` + `op/not`）——**不需要"补一个 bf16/f16 的 `not`"，那会是"数值真假"的新语义，不是这一笔的事**。
2. **11 条 `__strict_neq__` 抄来的**：`!==` 在数组上要的取反是 **`!` 作用在 `bool[]`**，而这一条当时**不存在**（`__not__` 只有 `(bool) => bool`，tensor 内核的 `UnaryOp` 里没有 `Not`）。**这一类除了 `eq_*`（即 `__strict_eq__` 的 11 条，都在）还要先补 `!bool[]`**，否则 `!==` 的标量与数组两半同时没有规则 —— 实测过：删了之后 `1 !== 2` 报"可用重载：空"，`modules.tensor.comparison:28` 报 `operator '!' is not defined`。

## 删除半的阻塞是 `__not__` 的内核，不是注册表

**不是"`__neq__` 组删不掉"，也不是"注册表里有别的东西引用它们"**：注册表这一侧是一条一条删干净的（`globals.cpp` 的 11 条、`builtin.cpp` 的 21 行内核与免故障清单、`onnx/lowering.cpp` 的 `:op/ne_*` 一行、`struct.cpp` 的 `__ne_bytes__`）。**唯一的阻塞是"取反可表达"：`__not__` 的 `bool[]` 内核落地与否，决定那 11 条能不能删。** 后来的人若看到这里还红，先查 `__not__` 有没有 `bool[]` 条目，不要回头翻注册表。

## 一段留下但不生效的代码，以及它的边界

**`globals.cpp` 里那个把 `__eq__`/`__neq__` 的定签名 resolver 抄进 `__strict_eq__`/`__strict_neq__` 的循环还留着，今天两侧都无行可抄 ⇒ 它是一段不生效的代码。** 边界写在这里：**哪天有人恢复 `__neq__` 的派生（例如把某个 `ne_*` 加回去），这个循环会立刻复活，`__strict_neq__` 会重新拿到一份 `ne_*` 拷贝，而 `!==` 已不再走它 —— 那会是一份没人读的注册。** 要彻底删掉它，得连 `__strict_eq__` 的抄写一起改成显式写法（`__strict_eq__` 今天仍在用这份抄写），那是另一笔。

**`bf16`/`f16` 的 `!=` 走 `op/eq_bf` + `op/not`，树里 `op/not_bf`/`op/not_f6` 0 处。** 这两个名字属于"数值真假"（`!x` 即 `x == 0`）的新语义，不是这次删除的产物：被取反的始终是比较结果的那个 `bool`，所以两个窄类型不需要自己的 `not`，`CAMEL_HALF_PREDICATE` 也随之删掉。

## 本笔的读数（`develop` 18ddc4eb3 的合并树上）

- `camel inspect --ops` 最小程序：**352 → 330，正好 −22**；`:op/ne_*` **剩 0 行**。
- `1 === 1, 1 !== 2, 1 !== 1, "a" !== "b"` → `true true false true`；图中 `1 !== 2` 是 **`eq_l` + `not`**。
- `f16` 的 `a != b`/`a !== b`/`a == b` → `true true false`；`bf16` 的 `a != b` → `false`，图里是 **`op/eq_bf` + `op/not`**。
- `!([true, false])` → `[false, true]`，`![true, true]` → `[false, false]`，`![false]` → `[true]`；`!` 作用在 `float[]` 上仍然被拒（`notInfer` 只接受 `bool` 元素类型）。
- `test/cases/modules/tensor/comparison.cml` 整份跑通：`a !== b` → `[false, false, false]`（逐元素，`a` 与 `b` 相等处取反为假）。
- 整树 **fast** 层：**8447 总数 / 8435 过 / 0 失败 / 10 skip / 2 known failures**；`stdlib-reference --verify-ops` **exit 0**。

## 给 `lane-m5` 的一句话

`__not__` 的 `bool[]` 条目本笔也已经补上（tensor 内核 `UnaryOp::Not`，`notInfer` 只接受 `bool` 元素类型，`!float[]` 仍被拒），所以那 11 条不必再等；`lane-m5` 的 `86dbd3672` 做的是同一条，两个车道会撞在 `elementwise.h`、`elementwise.cpp`、`elementwise_ops.cpp` 三个文件上，合并时取一份即可。**bif16/f16 的 `Not` 两车道都不需要**（见上面第 1 类，`:op/not_bf`/`:op/not_f6` 是"数值真假"的新语义）。