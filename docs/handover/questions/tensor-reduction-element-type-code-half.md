# 归约返回元素类型：裁定的**代码**那一半落在哪，以及规范定义库怎么跟

提出者：`handover/h451-flip-entry-args`（2026-10-24）。裁定在
`docs/handover/reviews/questions-2026-10-23.md` 的 *f32-scalar-to-the-device 与 tensor-reduction-element-type*：
`sum`/`mean` 对 `Tensor<f32>` 是 `f32`、对 f64 是 `float`，累加在 f64 中、舍入一次，
"现有程序里把结果交给 `float` 的地方改写显式转换（不计兼容）……**现在先按原则改正，不等 P4**"。

本 lane 的任务书把 `feat.modules.definitions` 的 10 条 `ReturnTypeMismatch` 报成"归约返回元素类型"这条。
**实测下来那 10 条不是归约**（见 §二），所以这条裁定的代码那一半到本轮为止仍然没有人做。
把它记在这里，免得它随"10 条已转绿"一起消失。

## 一、今天的状态（本树实测）

`tensor:sum` / `tensor:mean` 仍然只返回 `float`，而按轴的 `sum_axis`/`mean_axis` 已经是元素类型——
这正是裁定说的那个"不一致"：

- `modules/tensor/ops/reduction_ops.cpp:122` `scalarFloatInfer` 就是 `return Type::Float64();`，
  被 `:349`（sum）与 `:358`（mean）用作 `.infer`；
- `modules/tensor/ops/indexing.cpp:881-894` 的 law 把结果写死 f64：
  `sumLaw("sum_f32", f32, f64, f64)`、`meanLaw("mean_f32", f32, f64, f64, LawEmpty::Fault)`；
- 同一段的 `sum_axis`/`mean_axis`（`:896-910`）已经是 `sum_axis_f32 -> f32`、`mean_axis_f32 -> f32`。

判据：`func f(): float { return sum(<f32 张量>) }`（`mean` 同）在本树编译得过、退出 0，
所以两个名字今天对 f32 张量给出 f64。裁定要的是 f32。

## 二、为什么本 lane 没有顺手落它

那 10 条红的唯一根因是**秩 1 元素读取**，不是归约。逐字（`camel test/cases/definitions/kernels.cml`）：

    error[SemanticDiag::ReturnTypeMismatch]: Return type 'f32' does not match the function's declared return type 'float'.
       --> test/cases/definitions/kernels.cml:10:29
     10 |     return 0..numel(t)->map<(i: int): float => v[i]>
        |                             ^^^^^^^^^^^^^^^^^^^^^^^

`v = reshape(t, [numel(t)])` 是秩 1，`v[i]` 取到的就是元素，类型是元素类型 `f32`
（rank-zero-tensor 裁定：秩 1 处取到的是元素 `T`）。改法是裁定说的显式转换 `float(v[i])`，
金标一字不动——本 lane 已经这么做，`feat.modules.definitions` 23/23 绿。
那条诊断与 `sum`/`mean` 的返回类型无关，所以"10 条绿了"不等于这条裁定落了。

## 三、落这条裁定要动的地方（按代码读出来的，本 lane 未实测）

1. `modules/tensor/ops/reduction_ops.cpp` 的 `scalarFloatInfer`（f32 张量给 f32，f64 给 f64），
   以及 `modules/tensor/kernels/reduce.cpp` 里整个张量求和/求均值的**一次舍入**（今天按 f64 出结果）。
2. `modules/tensor/ops/indexing.cpp:881-894` 的六条 law 的结果类型。
3. `test/cases/definitions/grad.cml` 的 12 个 `: float` 函数（`return sum(...)` / `mean(...)`），
   按裁定的"改写显式转换"加 `float(...)`。
4. `test/plans/feat/modules/golden/definitions/kernels.txt` 末尾 5 行 `println(mean(...))`：
   今天钉的是 f64 累加文本，改成 f32 之后要重录；`gradients_numel_shape` 的
   `expect_stdout_exact = "2.5\n1.0\n"` 也可能随之动。

## 四、要裁定的两句

1. **代码那一半归哪条车道**：是 P4（`handover/h438-tensor-alias`，裁定说"不等 P4"）顺手做，
   还是单开一条？它对 `modules/tensor/ops/**` 与核心打印/求值都要动，与本 lane 的 14 条无关。
2. **规范定义怎么写**：`stdlib/tensor_definitions.cml` 的 `mean` 是
   `sum(x) / (numel(x) as float)`，而 `feat.modules.definitions` 的 `kernels_expanded` 要求
   内核与**展开后的定义**逐位相同。`sum(x)` 变成 f32、`numel(x) as float` 仍是 f64 之后，
   定义给出的 `mean` 按不按元素类型算？
   - (a) 定义也改写元素类型：`sum(x) / (numel(x) as f32)`（定义与内核同一个类型故事）；
   - (b) 保持 `as float`，"除以 float 再舍回元素类型"，由求值/打印一侧补那一次舍入。
   两条都能让"内核 == 定义"继续成立，但定义库是规范文本（设计原则 7.1），所以由作者选。

## 五、读数口径（补注）

本页的读数取自本 lane 的树：本页最后改动 `ece64b777`，与翻转线 tip 上的 blob 相同。develop 侧
第一节那条事实今天仍成立——`modules/tensor/ops/reduction_ops.cpp` 的 `scalarFloatInfer` 仍是
`return Type::Float64();`，`sum` 与 `mean` 两条算子登记都用它，所以这条裁定的代码那一半在
develop 上也还没有落。