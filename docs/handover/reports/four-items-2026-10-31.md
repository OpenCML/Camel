# 04:30 催办点名的四件，今天各到哪一步

> **写这一页的原因**：04:30 催办说这四件"都没有动静"。它们不是没动，是**动在车道上、还没进 `develop`**；这一页把每一件的读数与 sha 摆出来，好让作者按读数判，而不是按 `develop` 的提交数判。

## 一、拆 `orderAfterLastWriter` 的 ctrl 替身

- **替身已删**：`531477399`，−25 行（函数体 22 + `builder.h` 2 + `builder_operators.cpp:4279` 1）。
- **读改走版本算子**：`:var/after(cell, call)` 的第二实参接受任意值（含 `void`），读写成 `:var/get(:var/after(…))`；`ab9268f24`。
- **"删完重量"的读数**：作者点的那把尺子是 `133`（"块外补丁 + 版本边同时在树上"那个中间态的整闸红数，133 行 / 129 个用例名）。**删掉替身后同一把尺子是 `4`**，且这 4 条没有一条由替身造成。**所以 `133 → 4`。**
- **块外那一半没有上**：整份补丁套上整闸是 `total 8041 / pass 7882 / fail 150`（新增 146 条：`var_tensor` 33、`var_param` 33、`optim` 24、`callback` 21、`fault` 8、`var_params` 8、`mutable_reference` 7、其余 12）。**之前报的"66 条"只是伤害的 45%。** 这一条按页等作者：`docs/handover/questions/questions-2026-10-31-what-executes-a-value-taken-by-a-position.md`。

## 二、`reinterp<jvp>` 的逐原语替换

已经通的：规则集索引按算子 URI 取到右侧图（`8ed6cec40` 给实例加了 `lhsOperatorUri`；打印 `REINTERP uri :op/div_d -> rule jvp_div -> rhs 107`）。

**替换没落地的原因是量出来的，不是没做**：

```
jvp_div  ports=1 portType=float vars=x,w count=2 fill=FunctionClosure slots=2 type=(dx: float, dw: float) => float pure
jvp_neg  ports=0 lhsPorts=0 vars=x  count=1 fill=FunctionClosure slots=1 type=(dx: float) => float pure
```

- **右侧以 `Fill` 收尾**：它产出的是**闭包**，不是数值——所以"把右侧放到 `/` 那个位置"不是一步，是**两步**：先用 `x`、`w` 的值调一次右侧图拿到闭包，再用 `dx`、`dw` 调那个闭包。
- **一元算子两种形状**：`neg` 没有端口，它的操作数不从端口进，替换前要先找操作数从哪来。
- **三条验收现在一条没到**（`f = 5.5` 是未替换时的数；`f' = 3.25` 没有可打印结果；缺 `/` 时没有替换诊断可打）。**下一步是"用 `x`、`w` 调一次右侧图并打印拿到闭包"这一笔**，不是"直接换"。

## 三、删 `ReducedPrecisionUnsupported`（03:50 返工）

- **已删干净**：`15d5601ed`，13 个文件 +40/−132：两处 raise、共用助手（~34 行）、`registry.tsv` 一行、`examples.tsv` 一行，`--write` 重生成 `semantic.h`/`semantic.cpp`/`docs/diagnostics.md`（301 codes）与 AI 参考页。
- **与 `f32` 同一句**：

```
(bf16, bf16): No matching function found for argument types: <> (bf16, bf16); operator '%' is not defined for these operand types.
(f32,  f32):  No matching function found for argument types: <> (f32, f32);  operator '%' is not defined for these operand types.
```

- **两条用例改钉普通诊断**（不删用例），A/B：把 `%` 换成 `+` → 44 → 42 pass / 2 fail，换回 44/0。
- **全仓扫描**："窄浮点没有运算"这类说法在代码/用例/计划/现行文档 **0 处**；剩 2 处在日期化的 handover 存档页，是"当时如此"的记录，按规矩不改。

## 四、窄浮点上设备的五步

- **第 1 步确实就是作者画的边界**，所以车道先写了页问（`94a2f7e74`），得到批准后按"五步在一条车道上做完、一次合入"开工。
- 已落的前置：`DType` 五元素、类型门（`graph_regions.cpp:37-44`）、`scalar_expr`、`serial_kernel`、CUDA 发射（`codegen.cpp` + `cuda.cpp:727` 那个二选一改四选一）——**侦察完成，落码进行中**。
- 验收不变：**一个 `bf16` 逐元素运算的 `#gpu` 作用域真的在设备上跑、结果与 CPU 逐位一致（按 b1：在 f32 里算、舍入一次）**；`scalar_literal.cml` 的 `unresolved_type` 读数翻成"上了设备"。

## 另外两件今天量出来、值一页的东西

- **`%` 的拒绝文案曾是两条"永不失败的守卫"**：`bf16` 含子串 `f16`，帮助段里两个词都有——钉子不变、把用例换成另一型仍 44/0 全绿。改钉整句 `element type 'bf16'` 后同一换型立刻 43/1。**（这条现在随诊断删除而作废，但手法留在 `agents/test.md`。）**
- **`:op/mem_blocks` 能译但会算错**：standalone 程序没有块空间（数组从 `new` 拿块），四个计数恒 0，`b.bytes > a.bytes` 变成 `false`。车道撤回了那条规则、选择继续拒绝，并写了页：`questions-2026-10-31-mem-blocks-has-no-block-space.md`。**"能跑但算错"不算翻译。**