# 窄浮点算不算"浮点"：`Tangent<f16>` 的射程

裁定说"浮点数与浮点张量的切空间是它自身"。对 `float`（f64）和 `f32` 成立，对 `bf16`、`f16` **今天不成立**：`Tangent<f16>` 求值成 `()`。不是登记漏了一行，是差一个算子。

## 现象到符号

- `Tangent<T>` 问的是派生机制自己的答案（`src/core/type/typecall.cpp#tangentSpaceOf` → `machineryTangentTypeOf`），而登记的切空间在 `src/builtin/operators/derivative.cpp#registerBuiltinDerivatives`：`setTangentSpace(TypeCode::Float64, …)`、`setTangentSpace(TypeCode::Float32, …)`，没有 `BFloat16`、`Float16`。
- 一个切空间**需要**什么，写在 `src/builtin/operators/derivative.cpp#floatTangentSpace`：两样东西，`F::add`（一个算子 URI）与 `F::value(0.0)`。`F64` 给的是 `:op/add_d`、`F32` 给的是 `:op/add_f`；`bf16`/`f16` **没有任何算术算子**（`:op/add_h`/`:op/mul_h`/`:op/neg_h` 在 `src` 与 `modules` 里一处都没有），所以那一行登记写不出来——没有名字可指。
- 于是在 `stdlib/rulesets/jvp.cml#zero_tangent` 里，`f16` 走到 `case _ => ()`，报的是"返回 `f16` 与声明的 `()` 不符"。

**这不是一整套窄浮点算术**：把 `Tangent<bf16>`/`Tangent<f16>` 变成"自身"，只缺**一个加法算子**（以及一个零值），够 `floatTangentSpace` 用即可；减、乘、除今天一个都不需要。

## 为什么不能自己定

它决定的不是实现，而是**"切空间是它自身"这句话的射程**：窄浮点算不算"浮点"。补算子（①）与承认没有切空间（②）在语言上给出的是两个不同的答案，第二个还要在设计原则里留一句，否则下一个人会再问一次。参赛的两条都在作者手上，不在施工方。

## 选项

- **① 给窄浮点补最小的加法算子**（`bf16`、`f16` 各一个 `:op/add_h` 的等价物），刚好够 `floatTangentSpace` 用。**要多造**：两个登记 + 一个零值；`setTangentSpace(BFloat16/Float16, …)` 两行。**验收数**：`Tangent<bf16>`/`Tangent<f16>` 等于自身，规则集的 `f16` 用例（`let z: f16 = lift(f16(0.5)).1`）变绿，一处新断言。
- **② 承认窄浮点没有切空间**（`Tangent<f16> = ()`）。**要多造**：设计原则里一句"窄浮点无切空间"，否则同一个问题会被再问一次；规则集的注释与文档同步。**验收数**：`f16` 用例断言的是空元组而不是 `f16`，断言仍是一处，但口径变了。
- **③ 本轮先不覆盖窄浮点**，规则集只留 `float`/`f32` 与任意秩张量。**要多造**：无。**验收数**：少一条用例（`-1`），并且"浮点数"这个说法暂时只对两种宽浮点成立——页里要写明是**暂缓**，不是不支持。

**默认建议：①**。它与"浮点切空间是它自身"一致，也是 JAX 对 `float16`/`bfloat16` 的做法（切向量同 dtype）；代价只是两个算术算子，不是一套窄浮点运算；②改的是口径，③把裁定留一个缺口而缺口会被下一个人当成结论。

在这一页裁定之前，`stdlib/rulesets/jvp.cml` 只覆盖 `float`/`f32`，窄浮点在类型上被拒绝，不会拿到一个错的零。