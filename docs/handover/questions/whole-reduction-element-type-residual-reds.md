# 整体归约返回元素类型落地后，剩下的 16 条红与它们各自要的一句话裁定

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的行号与读数以那条线为准。


提出者：`handover/h579-reduction-element-type`（2026-10-26；同日第二轮更新：§二 的 1 条已定位并修，
残下的 8 行读数搬去 §三）。本轮把裁定（`docs/handover/reviews/questions-2026-10-23.md`，
*f32-scalar-to-the-device 与 tensor-reduction-element-type*）的**代码那一半**落了：整体形
`sum(t)`/`mean(t)` 答操作数的元素类型（`f32[..]` → `f32`、`f64[..]` → `float`、
`bf16[..]`/`f16[..]` → 自己的元素类型；`i64`/`i32` 精确、不绕 f64；`bool` → `int64`），累加仍在 f64、
只舍入一次。law 表（`modules/tensor/ops/indexing.cpp`）、声明面（`stdlib/signatures/tensor.cml` 的
`sum::<T>(t: T[..]): T`）与 tiles 侧（`compiler.cpp`、`reduction_kernel.cpp`、`schedule.cpp`、
`serial_kernel.cpp`）一起跟上。参考读数与其余细节见同目录 `tensor-reduction-element-type-code-half.md`。

落地后全快层（`smoke` + `feat/**`）新红一度只剩 17 条，全部**不在本 lane 的允许文件集内**；第二轮定位并
修了其中的 `declaration_evaluator`（§二，动的是 `modules/tensor/passes/index_eval.cpp`），**剩 16 条**。
允许文件集内的迁移（206 个 `.cml`、346 处归约交给 `float` 的位置）已按裁定的"改写显式转换"做完，
金标与计划文本按新读数重录。

## 一、规范定义库的 `mean`：16 条（`feat.modules.definitions` 15 + `feat.mm.gc_poison_sweep` 1）

根因一条，`stdlib/tensor_definitions.cml:55`：

    export func mean(x: Tensor<f32>): float { return sum(x) / (numel(x) as float) }

`sum(x)` 现在是 `f32`、`numel(x) as float` 是 `float`，而标量除法严格分宽
（`:op/div_f` 只收 `(f32, f32)`、`:op/div_d` 只收 `(float, float)`，设计原则 5.1，混合宽度一律拒绝）：

    [Error]: No matching function found for argument types: <> (f32, float); operator '/' is not
    defined for these operand types.

它一条带走 `feat.modules.definitions` 的 15 条（`kernels*`、`kernels_expanded*`、
`mean_empty_fault_expanded*`、`expanded_nodes`、`declared_and_refused`、`gradients_numel_shape`、
加上基线上本来就红的 `gradients`/`gradients_expanded`）与 `feat.mm.gc_poison_sweep` 的
`poison:modules.definitions:declared_and_refused`。

**要的裁定**：本目录 `tensor-reduction-element-type-code-half.md` §四 问题 2，候选：
(a) 定义也改写元素类型 `sum(x) / (numel(x) as f32)`（返回类型跟着改成元素类型），还是
(b) 保持 `as float`、在定义里写显式转换 `float(sum(x)) / (numel(x) as float)`。
两条都只改这一行，都让"内核 == 展开后的定义"（`kernels_expanded`）继续成立。
定义库是规范文本（设计原则 7.1），未经裁定本 lane 不动；按 (b) 的最小形式，逐字补丁是

    55: return float(sum(x)) / (numel(x) as float)

**第二轮在主机上量出的差**（同一棵树，`f32` 张量 `[2^25, 7, 7]`，与
`test/cases/modules/tensor/whole_reduction_rounding.cml` 的 C 同族；逐字读数，元素类型都是 f32）：

    今天的整体 mean（元素类型、只舍入一次）           11184815.0
    逐轴 mean(x, 0)（同一条规则）                     11184815.0
    规则本身：f64 累加、f64 相除、最后舍入一次        11184815.0      ← f32(11184815.333333334)
    候选 (b)：float(sum(x)) / (numel(x) as float)     11184816.0      ← 与规则差一个 f32 步长
    同族的 f32 相除形 float(sum(x) / f32(numel(x)))   11184816.0      ← 同上
    精确 f64 均值（不落到元素类型）                   11184815.333333334
    按步 f32 折叠的和（作为对照）                     33554450.0      ← 精确和 33554446、一次舍入 33554448

逐字回答"哪一种才符合'在 f64 里累加、只舍入一次'"：**只有先把操作数搬进 f64、在 f64 里累加并相除、
最后一步舍入进元素类型**的那一种——`f32(sum(cast(x, 'float')) / float(numel(x)))`，值 11184815.0，
与今天的 `mean(x)` 和逐轴形逐位相同。候选 (b) 把 `sum(x)` 先舍入进 f32 再相除，值 11184816.0，
**是两个舍入**，不符合规则；今天那一行本身不编译。等价且更短的第三条：定义直接转呼逐轴形
（`mean(x, 0)`，值同为 11184815.0——逐轴形本来就按规则算）。

## 二、`test_index_eval` 的声明求值与内核不一致：1 条 —— **已定位并修**

修前逐字读数（同一二进制、同一 plan，一次调用里跑三遍；三次的判定与总计**一字不差**，变的只是地址）：

    EVAL_MISMATCH op=tensor:add (f32[], f32[]): element 0: kernel -5.7901223e-24f(0x98dffe98) vs declaration -3.3937044f
    EVAL_MISMATCH op=tensor:subtract (i64[], i64[]): element 0: kernel 129110477206984 vs declaration -1282579
    （三遍的 kernel 侧：0x98dffe98 / 0x6b5ffe98 / 0x14dffe98——低 4 字节稳定、高位随 ASLR 变；
      kernel 的 i64 侧 129110477206984 = 0x7569… 是 x64 堆地址）
    EVAL total operators=33 cases=2780 agree=2310 faults=211 points=53904 unsound=0 mismatches=470 undeclared=0

**根因（逐字代码）**：pass 用 `ArgList norm(c)`（`modules/tensor/passes/index_eval.cpp:1071`）调内核
`def->kernel(with, norm, ctx)`（同文件 `:1096`），而 `ArgsView::resultType_` 默认 `nullptr`
（`include/camel/core/operator.h:53`），只有真跑的调度器才会 `setResultType`
（`src/passes/sched/nodevm/nodevm.cpp:1003`、`taskflow.cpp:1934`、`fastvm/native_call.cpp:17`、
`modules/graph/txn.cpp:310`、`src/runtime/rules.cpp:169`）。内核据此判"结果是元素还是箱子"：
`ops::wrap`（`modules/tensor/ops/support.cpp:96`）读 `declared != nullptr && declared->code() != Array`，
`nullptr` 一律走箱子分支；而 pass 自己按类型规则把秩 0 结果读成元素
（`index_eval.cpp:1103-1122` 的 `numberResult`），于是**把箱子的指针当 f32/f64 元素读**。**不是随机**，
是"地址当元素读"。

**修法**（一行 + 注释，`index_eval.cpp:1093` 之前，来源与调度器同一处）：

    norm.setResultType(resultType);   // resultType = def->infer(...)，即这次调用的静态结果类型

**修后逐字读数**（同一二进制、同一 plan，一次调用里跑三遍，三次**完全相同**，含 kernel 侧的值）：

    EVAL total operators=33 cases=2780 agree=2767 faults=211 points=53904 unsound=0 mismatches=13 undeclared=0
    EVAL_MISMATCH 行：97 → 8（`mismatches` 470 → 13，`agree` 2310 → 2767，`unsound` 与 `faults` 不动）

`feat.modules.index_semantics declaration_evaluator` 因此仍红，但只剩 §三 那一族。

## 三、`declaration_evaluator` 修后剩下的 8 行：混合元素类型的搬运（`i64[]`/`bool[]` 对 `f32[]`）

    EVAL_MISMATCH op=tensor:broadcast_like (i64[], f32[]): element 0: kernel 6.09212e-40f(0x0006a23c) vs declaration 434748f(0x48d44780)
    按算子与操作数形状：broadcast_like (i64[], f32[]) ×4、sum_to (i64[], f32[]) ×3、sum_to (bool[], f32[]) ×1

读数含义：`0x0006a23c` = 434748 = i64 元素那一格的**低 4 字节原样**，声明侧给的是 `434748f`（i64 → f32
的转换）。即**声明说"搬到结果元素类型"（`index_eval.cpp:477` 的 move 说明："copies the one value it
reads (converted to the result's dtype: broadcast_like and expand_axis produce f32)"），内核的搬运按位复制**；
同元素类型的搬运两侧一致（`agree=2767`）。

**要的裁定**（二选一，都是一句话）：
(a) 混合元素类型的 `broadcast_like(g, like)` / `sum_to(g, like)` **合法** → 内核侧搬运必须按结果元素类型
    转换（`modules/tensor/ops/indexing.cpp` 的 move 路径）；或
(b) **不合法** → 按翻转线自己的做法"按名字拒绝"（同 `9146994b4` 对"标量与数组"的做法）。
本轮未动这两处：它是 `handover/h523-decl-eval`（`8146a407e`）那一族"混合宽度"的残渣，属核心/内核车道；
本 lane 只把 harness 一侧的读数补齐（§二）。

## 四、不属于本轮的既有红（列出只为划界，不要记到这条裁定头上）

- `registry.docs_complete`：`stdlib/signatures/nn.cml:142` 等 `@example` 里 `Tensor` 不带元素类型
  （P4 删别名 `handover/h438-tensor-alias` 的后果），与本轮无关；本树基线上就是红的。
- `stdlib/optim.cml:311:42` 的 `Tensor` 同族（带塌 `feat.modules.optim` 一片），同上。
- `feat.compile.diag_catalog catalog.doc_is_generated`、`feat.modules.stdlib_registry
  registry.reference_is_generated`：本轮已按仓库自己的生成器（`node scripts/diagnostics-catalog.mjs
  --write`、`node scripts/stdlib-reference.mjs --write`）重生成，绿。
- `docs/reference/camel-for-ai.md` 的超预算（`the document is 112030 characters; its budget is
  112000`）属翻转线自己的 25 个落地（其 tip 112040 已超 40），修它要删该文件的手写散文，超出本 lane
  的允许文件集，只报不动。