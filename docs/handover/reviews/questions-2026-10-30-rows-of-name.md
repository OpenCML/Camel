> 已由作者答复，裁定原文见 `docs/handover/reviews/questions-2026-10-29.md` 末节与 `questions-2026-10-30-gate-control-edge.md`；本页留档，不再更新。

# `RowsOf<T>` 这个名字沿不沿用

裁定：待裁（作者 2026-10-30 尚未定；当前按"默认建议"做，即已在 `lane-jax2` 上按 `RowsOf<T>` 落地）

## 现象

返工 `vmap` 时新加了一个**公开的语言原语**：编译器提供的类型函数 `RowsOf<T>`（声明 `stdlib/types.cml:55`，实现 `src/core/type/typecall.cpp:34,110`）。它是"第一维元素为 `T` 的数组类型"：`RowsOf<f32[3]>` 是 `f32[?, 3]`，`RowsOf<f32>` 是 `f32[?]`。

加它的原因是量出来的、不是猜的：`vmap(f)` 要保持"一个参数、轴 0、同一个名字"的柯里形态，而这条签名在 Camel 里写不出来——

- `vmap::<T, U>(f: (row: T[d]) => U): (xs: T[B, d]) => U[]` 被 `SemanticDiag::DimensionNameNotBound`（0x03000141）拒，原文是"名字只出现在返回类型里是错误：调用方没有实参能说出它是多少"；
- `xs: T[]` 不匹配 `f32[4, 3]`（现有 `vmap` 的拼法，返工前它就只接受 `T[]`）；
- `xs: R[..]` 拼出 `f32[3][..]`，也不匹配 `f32[4, 3]`；
- `xs: T[?]` 在函数类型参数里被归一成 `T[]`，于是实参写 `(row: f32[3])` 反而不匹配。

能通的两条路都要付代价：每种行宽各写一条声明（无有限条数），或者留开口维度、让调用方写 `f32[]` 而不是 `f32[3]`（作者那条用例就写不成原样）。所以按设计原则 10.4／21 的那句"声明写不出的签名靠类型函数算出、真需要编译器的写成有名字、有文档、能复合的原语"，加了一个类型函数，`vmap` 因此仍是**一条**声明（`stdlib/array.cml:392`）。两条声明走不通还有个具体原因：`when is_array::<T>()` 不参与重载择一，同一模块里两条 `vmap` 会报 `AmbiguousCall`（0x030000F4）。

`RowsOf<T>` 当前被三处引用：`vmap` 的签名、语言页 `docs/language/06_expressions.md:426`（中英各一页，写的是"六个 `match type` 取不出或造不出的部分"里的最后一个）、以及 `TypeFunctionNotApplicable` 的诊断说明（`diagnostics/registry.tsv:617` 与 `src/core/error/diagnostics/messages/semantic.cpp:1764`）。

## 为什么不能自己定

它是**语言表面**：类型函数的名字进了语言页、进了一类诊断的说明文字、也会进用户写的类型里（`func f::<T>(xs: RowsOf<T>)`）。名字取错，改一次要动 `stdlib/types.cml`、`src/core/type/typecall.cpp`（名字表与分支两处）、中英语言页、诊断条文与三份生成文档，已经写下的用户代码也要跟着改。这一类命名的裁定先例是作者做的（标签名 `#jvp` 不改、线性转置仍叫 `transpose`、重算换内存叫 `remat`），所以名字归作者。

## 两条路

1. **沿用 `RowsOf<T>`**（我方默认建议，已按它落地）。语义直读："第一维元素为 `T` 的数组"，与核心词汇 `ArrayType::firstDimensionElement()`（`src/core/type/composite/array.cpp:359`，也就是 `x[i]` 与 `for x in xs` 取的那一维）同向，与既有 `ElementOf<T>` 成反方向——`ElementOf<RowsOf<T>>` 就是 `T`，两条 doc 例子就是这么写的（`is_same::<RowsOf<f32[3]>, f32[?, 3]>()`、`is_same::<RowsOf<f32>, f32[]>()`）。按它做留下的限制：名字押在"行"这个词上，若将来批处理不再限于前导维（`in_axes=1` 那一类），这个名字要重新解释或者再加一个更一般的拼法。
2. **改名**（例如 `BatchOf<T>`，或从"加一维"的角度叫 `Prepend<A>` 一类）。收益是名字更贴近用途（批处理）或更贴近机制（前插一维）；代价是上面列的五处一起改，且 `BatchOf` 会把"批"这个词也占掉——将来 `#shard`／分片的批与 `vmap` 的批不是一回事时容易混。

## 我方默认建议

**沿用 `RowsOf<T>`**，理由是与 `ElementOf` 成反方向、与 `firstDimensionElement` 同向、语义直读；按它做会留下的限制见上一条。若作者改判，改动量是 `types.cml` 一行、`typecall.cpp` 两处、中英语言页各一句、诊断条文一句、三份生成文档重跑，外加 `vmap` 的签名与两个用例——不需要动语义。

**要作者答的一句：`RowsOf<T>` 这个名字沿用，还是换一个？**