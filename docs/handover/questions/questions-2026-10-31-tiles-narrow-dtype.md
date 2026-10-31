# tiles 设备侧的元素类型层要不要加 `F16` / `BF16`

## 问什么

`modules/tiles` 的元素类型只有 `F32`、`F64`、`I64`，`dtype.h` 的文件头注释把这三种写成刻意的边界
（"these three are what a plan can name as a tensor's element"）。设备侧要支持窄浮点，第一步必然是往这个
枚举里加两个值。**加，还是等你把 H50（支持面）连同设备侧元素表示一起定？**

## 原文与位置（不是我转述的计划）

`docs/handover/reports/gpu-g4-g5-argmax-axis.md` 第 369 行起，§十一「窄浮点：今天的位置，与 (b) 的五步」，
第 383 行写明 **"(b) 的五步（现在立计划，不马上做）"**，第 1 步逐字是：

> 1. **`DType` 加 `F16`/`BF16`** —— 验收：`tiles/dtype.h` 两个新值，`dtypeWidth`/`isFloating` 覆盖它们，编译通过且无行为变化。

同一页第 391 行还写着：**"（a）今天的边界要写明：设备侧看不到 `bf16` 是边界，不是缺陷"**。

## 树里的现状（今天 develop 的读数）

- `modules/tiles/include/tiles/dtype.h:11` → `enum class DType { F32, F64, I64 };`
- `modules/tiles/include/tiles/internal/scalar_expr.h:19` → `enum Kind { F32, F64, I32, U32, I64 } kind;`
- `modules/tiles/src/graph_regions.cpp:41-43` → 类型门只放行 `Float32` / `Int64`，窄浮点在那里返回 `nullopt`
- `modules/tiles` 里 `bf16` 今天只作为 cuBLAS 混合精度开关出现（`schedule.cpp:433` `cublas_bf16`），不是元素类型
- 已有裁定 `docs/handover/reviews/reduced-precision.md`（2026-10-23）只说窄浮点是**语言核心**里的浮点类型
  （类型、字面量、标量算术、转换），**没有一句涉及设备侧的元素表示**
- 唯一带 `#gpu` 的窄浮点用例 `test/cases/gpu/nofallback/scalar_literal.cml` 今天的读数是那个 `bf16` 张量的
  算子被 `unresolved_type` 排除（报告 §十一 已记）

## 选项

- **① 现在做第 1 步**：`DType` 加 `F16`/`BF16`，`dtypeWidth`/`isFloating` 覆盖它们，编译通过、无行为变化。
  **要多造**：枚举两个值 + 两个函数的分支（机械，突破"三元素"的边界但不动语义）。**验收数**：照原文四条；
  另外 `git grep -c "DType::" modules/tiles` 不减少、`feat/modules/tiles` 与 `smoke` 的既有计数不变。
- **② 这一线不动**，等 H50 支持面计划获批后连同 `serial_kernel`、CUDA 代码生成、类型门一起做。
  **要多造**：无。**代价**：设备侧的窄浮点仍是"排除"，`scalar_literal.cml` 的 `unresolved_type` 读数不变。
- **③ 不复用 `DType`**，给设备侧元素另立一层。**要多造**：一层新的类型映射与两侧转换。**风险**：同一个概念两处定义，
  以后要同步两遍。

## 为什么不能自己定

`dtype.h` 的注释把三元素写成"一个 plan 能命名的元素"，这是刻意的边界，不是遗漏；改它等于替作者决定设备侧
元素类型的集合。而 (b) 那页自己写着"现在立计划，不马上做"——按页执行到第 1 步，正好撞上"不做"这句。

## 建议

**②**。今天的代价只是"设备侧看不到窄浮点"这一个已知边界，没有算错的风险（类型门在返回 `nullopt` 之前就把非
`Float32`/`Int64` 拒了，报告 §十一 已核实那条"静默当 F32"的诊断不成立）；而 ① 单独落地会让 `DType` 与
`graph_regions` 的类型门、`serial_kernel`、CUDA 代码生成之间出现一段只有枚举、没有消费方的半层。等 H50
获批，① 与 2–5 步在同一批里落地，`TILES_CENSUS_GAP` 才有数字可降。

若你要 ①，我只做第 1 步、不碰第 2–5 步，并把"无行为变化"用两条计数钉住。