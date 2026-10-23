# 写入的低精度转换不再需要许可之后，`PrecisionChangeNotPermitted` 该留还是该删

出处：`docs/handover/reviews/reduced-precision.md` 第 (c)/(f) 条；实现分支 `handover/h319-bf16-cast`（已合入 `origin/develop`，`275cb98ba` 之前的 `c8eeb00f9` 系列）。

## 现状（实测）

- 裁定 (c) 要求删掉"程序写下的 `bf16`/`f16` 转换需要许可"的检查。该检查是 `SemanticDiag::PrecisionChangeNotPermitted`（`0x0001D5`）**唯一的生产者**：`include/camel/core/operator_table.h` 的 `ReducedPrecisionNarrowing`/`PrecisionGate`/`declarePrecisionGate`/`precisionGate()`、`src/core/operator_table.cpp`、`src/compile/gir/builder_operators.cpp` 的调用点检查、`modules/tensor/module.cpp` 的 `narrowingIntoReducedType`、`modules/tensor/ops/elementwise_ops.cpp` 的 `castKernel` 运行期目标检查，全部随它一起删除。
- 实现者因此**整条删掉了这个诊断码**（`diagnostics/registry.tsv` 从 257 码变 256 码，`node scripts/diagnostics-catalog.mjs --write` 重新生成）。理由是可核查的：`scripts/diagnostics-catalog.mjs --coverage-check` 禁止存在"没有任何测试能产生"的注册码。
- 裁定 (f) 的原话是"改写……诊断的 help"，读起来像是**保留这个码、只改文案**。
- 编译器引入的低精度**仍然**被管住，只是换了个地方：`modules/tiles/src/schedule.cpp` 里把选择 `cublas_bf16` 作为**计划选择条件**（`:405-409`），并且 `:516` 的 `require` 会以 `tiles: invalid tiles plan: mixed GEMM permissions` 拒绝；`bf16.compiler_introduced.is_gated` 这个守卫证明只有带 `reassociate`+`change_precision`+`finite_math` 的函数才会走到那一步。

## 问题

`0x0001D5` 这个语义诊断码是**保留（并给它一个新的、今天仍存在的生产者）**，还是**接受删除**？

## 选项与代价

**(a) 接受删除（实现者已做的）。**
代价：写入转换"需要许可"这条文案随码一起消失（这本来就是裁定 (c) 要删的语义）；将来若"编译器引入的低精度"需要一条具名诊断，要么新分配一个码，要么重新启用 `0x0001D5`。
收益：目录门禁（每个注册码都要有生产者）保持满足；树里不留一条谁都产生不了的错误码；`0x0001D5` 可被别的语义复用。

**(b) 保留这个码，把它交给仍然存在的那条路。**
即让 `tiles` 的计划选择拒绝（今天的 `mixed GEMM permissions`）使用 `SemanticDiag::PrecisionChangeNotPermitted`，让"编译器引入低精度"这一步有一条**具名**诊断，而不是一句计划文本。
代价：要动调度器的诊断与它的金标/期望；而且这条拒绝是"计划不合法"，算不算**语义**诊断需要作者判断——如果算，它同时要满足目录门禁（有测试产生它）。

**(c) 保留码但暂时没有生产者。**
与仓库自己的门禁冲突（`--coverage-check`），除非同时改门禁规则；不建议，除非作者认为"预留码"是合法状态。

## 倾向

**(a)**：裁定 (c) 删的就是这条语义，而门禁是仓库自己的规矩；把一条产生不了的错误码挂在树上，比"将来需要时再分配"更贵。(b) 的价值独立于本问题——**不论选哪个**，`tiles` 那条拒绝的文本值得点出它缺的是哪个许可（今天的 `mixed GEMM permissions` 没说出 `change_precision`），那是一件小工程，不需要裁定。

## 判断在谁

语言作者（决策者）。判断依赖"编译器引入的低精度要不要一条具名语义诊断"这个口径，属语义/诊断命名，不由实现者决定。

## 来源

- `docs/handover/reviews/reduced-precision.md`（裁定 (c)、(d)、(f)）
- `diagnostics/registry.tsv`（256 码）、`docs/diagnostics.md`（脚本生成）
- `modules/tiles/src/schedule.cpp:405-409,516`
- `test/cases/compile/numeric/bf16_permission_gate.cml`、`test/plans/feat/compile/numeric_permissions.plan.toml`
- `test/plans/feat/compile/diag_catalog.plan.toml`（`catalog.coverage_zero` 257 → 256）