# `extent_of::<A>` 当一个值用：答，还是拒绝这个值？

**一句话**：`extent_of` 是 prelude 里 `export` 的普通内建函数，所以 `extent_of::<float[3]>` 可以不当调用而**当一个值**拿走（`let f: () => int[] = …`、`take(extent_of::<float[3]>)`）。这些写法在本 lane 之前**都落到 `__op_extent_of__` 的核**上，答 `RuntimeDiag::PassInternalError`（"这是编译器缺陷"）——即用户写得出的程序看到一句指错方向的话。本 lane 把它们改成**答**：这样的值是一张**没有参数、体就是这个常量的图**（`Builder::constantValueGraphOf`）。问题只有一个：**一个值该答，还是该在取值的那个位置拒绝？**

## 让它是问句的读数（`lane-extentfold`，`d7b0bf134` 上，主机 load 1m 35.91）

1. 三种取值写法都会运行：`let f: () => int[] = extent_of::<float[3]>` → `[3]`；`let g = extent_of::<float[2, 3]>` 连调两次 → `[2, 3]` / `[2, 3]`；`counts(extent_of::<float[4]>)` → `1`。`nvm` 与 `fvm` 逐字相同，exit 0。
2. 同一件事在别的取值位置上**已经被语言拒绝**，而且是正当拒绝、不是内部错误：`[extent_of::<float[3]>, …]`（数组字面量）与 `return extent_of::<float[4]>`（返回值位置，无期望类型）都报 `SemanticDiag::FunctionNameAsValue`，措辞是「它命名一个签名取决于实参的算子，不是一个函数」并给出改法（包一层 lambda）。`() => extent_of::<float[3]>()` 这条 lambda 写法本来就答。
3. 所以**同一个表达式在四种位置上有三种结局**（答、正当拒绝、答）；决定结局的不是 `extent_of` 的语义，而是那个位置有没有「期望类型」让 `Builder::overloadValueFor` 把类型实参用上。这正是「两条路都自洽」的读数：把「值」这条路也算成拒绝，四种位置就只剩**两种**结局（答 / 正当拒绝），代价是 `let f: () => int[] = …` 与 `take(…)` 从「答」变成「拒绝」。
4. 现有实现并非无代价：一个算子值通常是**一条 OPER 调用的图**，而这个值是**一张无参数、体是常量的图**——GIR 的读者会看到一种新的函数体形状；且每个取值位置各建一张（没有像 `operatorValueGraphOf` 那样按类型缓存）。

## 三条路

- **甲：答（本 lane 已实现并通过判据）。** 值是「无参数、体为常量」的函数；类型实参在取值处还在手里，因此答得出。**代价**：GIR 里多一种函数体形状；取值处多建图（未缓存）。
- **乙：拒绝这个值。** 在 `Builder::overloadValueFor` 之前拦下 `:op/extent_of` 的取值，报一条「它的答案是一个类型实参的事实，一个函数值不带类型实参」的语义诊断。**代价**：`let f: () => int[] = extent_of::<float[3]>` 与 `take(extent_of::<float[3]>)` 从答变成拒绝；且与作者 2026-11-03 17:55「它是一个普通内建函数」的框法相反（普通函数有值）。
- **丙：让值也走 `statedExtentsOf` 之外的通道 —— 给算子值带上类型实参。** **代价**：动图数据模型（作者同一条裁定：不动图数据模型），本 lane 不做。

## 默认建议（施工方默认，已按此开工）

**甲：答。** 理由三条：

1. 一个用户写得出、语义上说得通的表达式，**答 > 正当拒绝 > 错数 > 崩**；甲是这里唯一的「答」，而它用的读数是同一个 `statedExtentsOf`（`include/camel/core/type/composite/array.h#statedExtentsOf`），不会与拒绝侧、也不会与转置走线给出两个答案。
2. 拒绝一条本来能答的写法要新造一条语言层面的限制，而作者对 `extent_of` 的框法是「普通内建函数」——普通的函数有值。
3. 若作者认为「值」这条形状（无参数常量体的图）不该出现在 GIR 里，改乙只动 `overloadValueFor` 一处、加一条诊断；甲的三处判据（`nvm`/`fvm` 上的三种取值写法）届时随之翻面，代价是本 lane 的两个用例要改写。

## 判据 / 哨兵（本次读数，主机 load 1m 15–36）

- `feat/compile/transforms` **111 = 108/2/1**（基线 `107 = 104/2/1`；只多了本 lane 的 4 条 `extent_of` 答案条目，两条长红仍是 `transpose_loop_escape.*`）。
- `feat/compile` **2244 = 2239/4/1**，与基线 `2240 = 2235/4/1` 同四条红（`catalog.coverage_zero`、`catalog.pinned_coverage`、`transpose_loop_escape.*` 两条）。
- `feat/transforms/split` **19/19**。
- 若改乙：`extent_of_answers_through_a_function_value` 要改成拒绝用例，且 `let f = extent_of::<float[3]>; f()` 这条（DREF 直接当 callee，不经 `overloadValueFor`）仍会答——两条路的分界会落在「有没有期望类型」上，这正是第 3 条读数指出的事。