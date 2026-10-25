# 整体归约返回元素类型落地后，剩下的 17 条红与它们各自要的一句话裁定

提出者：`handover/h579-reduction-element-type`（2026-10-26）。本轮把裁定
（`docs/handover/reviews/questions-2026-10-23.md`，*f32-scalar-to-the-device 与
tensor-reduction-element-type*）的**代码那一半**落了：整体形 `sum(t)`/`mean(t)` 答操作数的元素类型
（`f32[..]` → `f32`、`f64[..]` → `float`、`bf16[..]`/`f16[..]` → 自己的元素类型；`i64`/`i32` 精确、不绕 f64；
`bool` → `int64`），累加仍在 f64、只舍入一次。law 表（`modules/tensor/ops/indexing.cpp`）、
声明面（`stdlib/signatures/tensor.cml` 的 `sum::<T>(t: T[..]): T`）与 tiles 侧
（`compiler.cpp`、`reduction_kernel.cpp`、`schedule.cpp`、`serial_kernel.cpp`）一起跟上。
参考读数与其余细节见同目录 `tensor-reduction-element-type-code-half.md`。

落地后全快层（`smoke` + `feat/**`，6235 条）新红只剩 **17 条**，全部**不在本 lane 的允许文件集内**，
逐条如下。允许文件集内的迁移（206 个 `.cml`、346 处归约交给 `float` 的位置）已按裁定的
"改写显式转换"做完，金标与计划文本按新读数重录。

## 一、规范定义库的 `mean`：16 条（`feat.modules.definitions` 15 + `feat.mm.gc_poison_sweep` 1）

根因一条，`stdlib/tensor_definitions.cml:55`：

    export func mean(x: Tensor<f32>): float { return sum(x) / (numel(x) as float) }

`sum(x)` 现在是 `f32`、`numel(x) as float` 是 `float`，而标量除法严格分宽
（`:op/div_f` 只收 `(f32, f32)`、`:op/div_d` 只收 `(float, float)`，设计原则 5.1，混合宽度一律拒绝）：

    [Error]: No matching function found for argument types: <> (f32, float); operator '/' is not
    defined for these operand types.

它一条带走 `feat.modules.definitions` 的 15 条（`kernels*`、`kernels_expanded*`、
`mean_empty_fault_expanded*`、`expanded_nodes`、`declared_and_refused`、`gradients_numel_shape`）
与 `feat.mm.gc_poison_sweep` 的 `poison:modules.definitions:declared_and_refused`。

**要的就是本目录 `tensor-reduction-element-type-code-half.md` §四 问题 2 的裁定**：
(a) 定义也改写元素类型 `sum(x) / (numel(x) as f32)`，还是
(b) 保持 `as float`、在定义里写显式转换 `float(sum(x)) / (numel(x) as float)`。
两条都只改这一行，都让"内核 == 展开后的定义"（`kernels_expanded`）继续成立。
定义库是规范文本（设计原则 7.1），未经裁定本 lane 不动；按 (b) 的最小形式，逐字补丁是

    55: return float(sum(x)) / (numel(x) as float)

## 二、`test_index_eval` 的声明求值与内核不一致：1 条（`feat.modules.index_semantics declaration_evaluator`）

`test/cases/modules/tensor/index_semantics.cml` 上的测试用 pass `tensor::test_index_eval`
在随机算例上比对"声明体求值"与 CPU 内核，本轮起在 `tensor:add`/`subtract`/`multiply`/`divide` 上
逐元素不一致，逐字（一侧是垃圾读数）：

    EVAL_MISMATCH op=tensor:add (f32[], f32[]): element 0: kernel -4.6115277e+19f(0xe01ffe98)
    vs declaration -3.3937044f(0xc0593274)
    EVAL_MISMATCH op=tensor:subtract (i64[], i64[]): element 0: kernel 129110477206984
    vs declaration -1282579

（两条读数逐次不同——同一 `[Error]` 里 kernel 那一侧每次是另一串垃圾，declaration 那一侧稳定。
形状每次都一样：算例逐元素不一致，最后一行总是 `unsound=0`。）

同一族在 `docs/handover/reports/two-layer-reds-of-the-flip-line.md:28` 被拆过一次（"15 处混合宽度、
4 处未知extent 的 reshape、**8 处空 extent 的归约**"），`handover/h523-decl-eval`（`8146a407e`）当时
答了这三族；`docs/handover/reports/tensor-flip-integration.md:800` 把同类残留判为**核心侧诊断**
（harness 把秩 0 操作数读成元素、内核把它装箱，实例搜索因此找不到实例）。本轮的差别是：
整体归约的结果类型从 f64 变成元素类型之后，算例生成器会造出新的（f32 空 extent 归约、混合宽度）组合。

**要的裁定**：这条红算不算本轮语义改动的**接受后果**（"harness 要能按内核的读法命名操作数"是核心车道的活），
还是本轮必须先补齐 `modules/tensor/passes/index_eval.cpp` 侧的元素类型读数再交。
（本 lane 未动这个 pass；动它需要先确认上面两处引用的结论是否仍成立。）

## 三、不属于本轮的既有红（列出只为划界，不要记到这条裁定头上）

- `registry.docs_complete`：`stdlib/signatures/nn.cml:142` 等 `@example` 里 `Tensor` 不带元素类型
  （P4 删别名 `handover/h438-tensor-alias` 的后果），与本轮无关；本树基线上就是红的。
- `stdlib/optim.cml:311:42` 的 `Tensor` 同族（带塌 `feat.modules.optim` 一片），同上。
- `feat.compile.diag_catalog catalog.doc_is_generated`、`feat.modules.stdlib_registry
  registry.reference_is_generated`：本轮已按仓库自己的生成器（`node scripts/diagnostics-catalog.mjs
  --write`、`node scripts/stdlib-reference.mjs --write`）重生成，绿。