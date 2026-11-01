# `T[?]` 匹配什么？——作者裁定，以及一条还没兑现的读数欠账

**裁定（作者 2026-11-02 11:10）**：`T[..]` 匹配任意秩；**`T[?]` 只匹配秩 1**。

## 落地

`src/compile/gir/builder_static.cpp` 的 `matchTypeCase` 末尾，`typeEquals` 之前加了一支：`pattern` 是数组、`!anyRank() && rank() == 1 && extents()[0].isUnknown()`，且 `subject` 是数组、`!anyRank() && rank() == 1`、元素 `typeEquals` ⇒ 命中。

- **`T[..]` 那一支一行未动**（它已经匹配任意秩，含秩 0）；
- **`T[3]` 仍精确**：常量 extent 是 `Kind::Constant`，走下面那句 `typeEquals`；
- **`T[n]` 仍精确**：维度名是 `Kind::Linear`（`0 + 1*n` 这样的项），**不是通配**。`include/camel/core/type/extent.h` 里 `enum class Kind : uint8_t { Unknown, Constant, Linear, Computed }`，`isUnknown()` 就是 `kind_ == Kind::Unknown`——"未知"与"名字"在类型上是两件事，这是这支改动能写的前提。

## 欠账：六格读数没测

**六格读数（`T[..]`/`T[?]` × 矩阵/向量/标量，改前改后）未测。** 原因是它需要探针用例 + 两次构建 + 临时 plan 通道；第一次探针在 Camel 的按需求值下只跑出一行（`println` 的调用没有依赖者时不会执行，编译器直接警告 "has side effects but is never executed"），要拿到六格得先给探针加 `sync` 块。

**本页下面的结论来自读 `stdlib/rulesets/jvp.cml` 的 case 顺序，不是读数**：`zero_tangent::<T>` 的 case 依次是 `float[..]`、`f32[..]`、`f16[..]`、`bf16[..]`，然后才是 `f32[?]`、`f64[?]`。`T[..]` 已匹配任意秩（含秩 1），所以秩 1 的数组在前四行就被接走；`f64[?]` 只有在"元素是 f64 而 `float[..]` 不匹配"时可达，而那一行自己写作 `case f64[?] => float(zeros_like(x))`，把 `f64` 与 `float` 当成同一个东西。**⇒ 这个修复今天很可能没有可观测的后果，是防未来的规则，不是修今天的错。** 这句话是"看的"，不是"量的"；下一个人若要当结论用，先把六格补上。

## 一条与我有关的红，但不是我的账

全量里唯一抓到名字的红是 `docs.doc_readings.current`，它读另一页写的六个数 `[202,268,116,98,1531,1396]`。**这条红在我合并之前就存在**：`origin/develop`@`427eb900d` 上 `modules/transforms/split_operator.cpp` 已经是 **203** 行，页写 **202**——差一，在 develop 侧。页里记的是代码的行数，代码没同笔改读数。**我没有动它**，由合并车道改 `202 → 203`，并同笔加一句"读数与代码同一次提交改"。

## 一个记号

写完这一页时留下的教训：**按需求值让"打印"不是免费的观测手段**。探针写成 `println(f())`，编译器会因为它没有依赖者而不执行——探针必须让结果被消费（`sync` 块或作为返回值参与计算），否则读到的是空。这条已单列进 `docs/handover/reports/the-instrument-discipline.md`。