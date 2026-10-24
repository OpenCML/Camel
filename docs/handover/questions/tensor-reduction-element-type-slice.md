# 张量归约取元素类型的实施切片：答案类型要从静态类型读，而且要动 170 个测试

Author: 车道 `handover/h398-tiles-f32-scalar`（`D:\Projects\Camel-wt\h398-tiles-f32-scalar`），2026-10-24。Status: open — 需要负责人定实施路线与授权。测量在 Linux 主机（`88zjwei`）上、`origin/develop`（`cf813dfd0`）与本车道改动同一构建窗口内完成，CUDA 可用，读数走计划运行器。

裁定 `9acbac6f7`（`docs/handover/reviews/questions-2026-10-23.md`）的两半里，**设备标量那一半已落地并实测绿**；**归约那一半我实现了两次、实测后撤出本车道**。这份文件交回的是实施路线与规模，不是"要不要取元素类型"（那已经裁定）。两处实测都说同一件事：改类型规则只是一行，**真正的工作量是"答案类型在运行期怎么落地"和 170 个既有测试**。

## 已落地的那一半（设备标量）

`supportedLeaves`、`buildResidentType`、区域传输/保有/下载、`tiles::compile` 的 f32 标量叶子与参考签名按 7.3 改正，11 个文件。实测：

- `test/plans/gpu/tiles.plan.toml`：`total 101, pass 101, fail 0, RUN_EXIT=0`（未改动基线同一条命令：`total 101, pass 99, fail 2`——`runtime_scalars` 的 `NoMatchingFunction` 与 `loop_boundaries_faults_permissions` 的区域传输内部错误）。
- 整层 `smoke,test/plans -Tier fast -Jobs 8`：`total 6017, pass 6002, fail 0, skip 11, known failures 4, RUN_EXIT=0`，与未改动基线逐项相同。
- `gpu/{tiles_regions 30/0, tiles_rows 2/0, profile 6/0}`；`gpu.tiles_transfer 2/1`，那 1 个红（`tree_expansion_autodiff_and_device_update`，`tree_training.cml:8` 的 `Tensor<float64, [2]>` 返回类型）在未改动的 `cf813dfd0` 上逐字相同，是既有红（与 §15 的"字面量拿不到调用点的期望类型"同族）。

## 归约那一半的两次实测

同一条命令、同一窗口：

| 实现 | 整层读数 |
| --- | --- |
| 未改动基线 | `total 6017, pass 6002, fail 0, skip 11, known failures 4, RUN_EXIT=0` |
| 第一次：kernel 按**运行期张量 dtype** 决定答案 | `total 6017, pass 5720, fail 282, skip 11, known 4, RUN_EXIT=1` |
| 第二次：kernel 按**实参的静态类型**决定答案 | `total 6017, pass 5832, fail 170, skip 11, known 4, RUN_EXIT=1` |

### 第一次为什么错（这是一个可复现的缺陷，值得记下来）

`tensor:sum` 的答案类型由 `infer` 算，值是 kernel 写的，而**标量槽没有运行期类型的载体**：一个 8 字节槽是读 4 字节还是 8 字节，只能由静态类型决定。第一次让 kernel 读运行期张量 dtype，于是"静态类型是 f64、运行期张量是 f32"的调用把 4 字节写进按 f64 读的槽：

```
[FAIL] f32_works.nvm  expect_stdout differs ... expected "10.0\n20.0\n0.0\n4.0\n..." 
                                         got      "10.0\n5.439686476e-315\n0.0\n4.0\n..."
[FAIL] var_tensor.default  unexpected output: 5.304989477e-315 | 5.398241246e-315 ...
```

`5.439686476e-315` 的位模式是 `0x41A00000`，就是 f32 的 `20.0` 被当 f64 读。触发它的形状是**元素类型没写在类型里**的那种：`func twice(x: Tensor<f32>): Tensor`（裸 `Tensor` 返回值）或 `func twice(var w: Tensor): float`（裸 `Tensor` 形参，见裁定 `6c4b371e6`），于是 `sum(twice(a))` 的静态类型是开放的 `float`，而运行期张量是 f32。

### 第二次的路线（可用）

kernel 与 `infer` 读**同一个事实**就不会分叉：`infer` 读 `ctx.facts(0).dtype`，kernel 读 `norm.type(0)` 的 `tensorFactsOf(...).dtype`。两者对同一个调用点给出同一个答案：

- 实参静态类型是 `Tensor<f32>` → 两边都是 `f32`（含 `Runtime_scalars`、`resident_scalars`、`f32_works` 的 `sum(a)`）；
- 实参静态类型是开放元素类型（裸 `Tensor`，或 `Tensor<i32>`/`Tensor<bf16>` 这类没有 arm 的）→ 两边都是 `float`，正是今天的行为，读的人也按 f64 读，不再有垃圾值。

第二次的 170 个红按首条诊断分类：`expected exit 0, got 1`（其他编译/运行错误）42、`NoMatchingFunction` 37、stdout 文本差异 28、其他 23、`verification exited 1`（数值/性质断言）22、`ReturnTypeMismatch` 14、值不一致 4。典型：

- 可直接逐处核对改写的：`ReturnTypeMismatch`/`NoMatchingFunction`/`expected exit 0, got 1` 这一批（把结果交给 `float` 的地方写显式转换，裁定里说"不计兼容"）；范围覆盖 `test/cases/**`（`autodiff`、`nn`、`definitions`、`perf`、`linear`、`modules/**`）与 `examples/**`、`docs` 片段。
- **要授权才能动的**：`tensor_grad_check.*`（`gradient check failed: arith ok analytic=... numeric=...`）、`residual_mlp.*`、`nn_grad_check.*`、`numeric_contract.nvm`（`matmul fma_step=0.0004883408546447754` → `0.00048834085`，打印精度随元素类型变）、`discrete_zero.*`、`rules.fusion_bit_identical_fused`、`library_gemm.tolerance`、`fuse.result`、`examples.ml.train_linear.*`。这些是**位一致 / 容差 / 梯度一致性 / 打印精度**这类性质，改它们不是重录数字，是改判据。

## 与 `6c4b371e6`（裸 `Tensor` 形参按调用点实例化）的关系

第二次的 170 是**下限**，不是终值。这一半依赖"元素类型没写在类型里时保持 `float`"；一旦裸 `Tensor` 形参按调用点实例化落地（裁定：必须），`func f(x: Tensor) { return sum(x) }` 这类调用点的元素类型就是 f32，`sum` 随之答 f32，于是更多程序需要显式转换。两条裁定的落地顺序会影响规模：**先落实例化、再落归约**，归约那一半的改写面会更窄也更稳定（实例化之后图上只有具体类型）。

## 选项与代价

1. **按第二次的路线（静态类型决定答案）落归约，并把 170 个测试的改写与重录一起做**。代价：类型规则本身已经写好并实测（`reduction_ops.cpp` 的 `scalarElementInfer`/`scalarAnswer`、`sum`/`mean` 的 VJP 按元素类型选 `:op/div_f`、`indexing.cpp` 的 f32 law、`tiles` 的 `sum_all`/`mean_all` 取元素类型、`reduction_kernel` 的 law 校验），真正的工作量是 170 个测试：其中约 116 个是编译/类型/文本改写，约 54 个是性质判据，需要负责人先给判据（见下）。
2. **先做 `6c4b371e6`（裸 `Tensor` 实例化），再做这一条**。代价：更晚；收益：改写面更窄、更稳定，且不必为"开放元素类型"保留一条临时语义（`sum` 对开放类型答 `float`）。
3. **保持 `float`，改 §7.3 的括号**（`tensor-reduction-element-type.md` 选项 2）：代价是 `crosslane` 这类形状上不了设备，GPU 计划 M0 的目标在这个形状上落空。

## 建议

**选 2 再选 1**：先落 `6c4b371e6` 的实例化（它已经裁定为"必须"，且 38 个 `var Tensor` 用例与 ONNX 导出两族都在等它），归约那一半作为**独立车道**跟在后面做，把 170 个测试的改写与重录一并纳入该车道。若要在本车道内直接把这一半做到底，请先定两件事：

1. 约 116 个程序的逐处改写（含 `examples/**` 与 `docs` 片段）是否全部由该车道承担；
2. 约 54 个**性质判据**（位一致、licence/tolerance 界、梯度一致性检查、打印精度）在 f32 下按什么标准重录——这是放宽或改判据，需要明确授权，否则该车道无法收绿。

## 出处

裁定 `9acbac6f7`、`6c4b371e6` → `docs/handover/reviews/questions-2026-10-23.md`；`docs/design-principles.cn.md:463`（§7.3）、§5.1、§5.5；`stdlib/array.cml:182`（`f177b10db`，"one reduction declaration per element type … no path asks for a type answer at run time"）；`stdlib/signatures/tensor.cml:229`；`modules/tensor/ops/reduction_ops.cpp`（`sumAllKernel`/`meanAllKernel`、`scalarElementInfer`/`scalarAnswer`、`meanVjp`）；`modules/tiles/src/{compiler.cpp,reduction_kernel.cpp,schedule.cpp}`；测量日志在本车道笔记 `D:\Projects\Camel-wt\notes\h398-fast-layer-full.log`（282 红）、`h398-fast-layer-static.log`（170 红）、`h398-gpu-tiles-task1.log`（101/101）。