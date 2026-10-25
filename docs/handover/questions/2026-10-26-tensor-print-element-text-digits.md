# 张量打印的元素文本：位数与"最短且能往返"冲突（但那一种形式已被裁定废止）

## 是什么

设计原则 7.7（`docs/design-principles.cn.md:529`）只有一条浮点文本规则："最短、且读回来得到同一个数。`string`、`println`、`format` 一致。"

develop 上 `src/core/rtdata/array.cpp` 的 `formatTensorElement`（`:1336`）**不走这条规则**：它把元素交回 C 库，
f64 用 `"% 11.7g"`（7 位有效数字）、f32 用 `"% 7.4g"`（4 位），整数值用 `"% 7.0f."`。`src/**` 里只有这一处
用 `snprintf` 写数字文本（`%g`/`%f` 全仓只有这一处），所以张量打印与标量/数组打印出入不同。

## 逐字证据（develop `5c890aabf`，主机跑，`std::nvm`）

同一个程序里同一个值（`1.0/3.0`）：

```
B1 f64-array            [0.3333333333333333]          ← 值自己类型的最短文本（数组走 rtdata::printSlot）
B2 f64-scalar           0.3333333333333333
B3 f64-string           0.3333333333333333            ← string() 同一条规则
E1 tensor-f64-third     tensor([  0.3333333], dtype=float64)   ← 7 位
E2 tensor-f32-third     tensor([ 0.3333], dtype=float32)       ← 4 位
B4 f32-array            [0.33333334]
```

往返读数（把打印出来的文本读回去，用语言自己比）：

```
C1 float(string(1.0/3.0)) == 1.0/3.0                true
C2 f32(float(string(f32(1.0/3.0)))) == f32(1.0/3.0) true
C4 float("0.3333333") == 1.0/3.0                    false   ← 张量文本读不回原值
C5 f32(float("0.3333")) == f32(1.0/3.0)             false
```

翻转线 `handover/h465-flip-integration`（`c9be5d619`）上同一份源码：`[0.3333333333333333]` / `[0.33333334]`
——那条线的 `Array::print` 已改走 `printArray` → `rtdata::printSlot`（`floatText`），`formatTensorElement`
在这条线上**已被删除**。

## 已有裁定

- `docs/design-principles.cn.md:529`：最短且能往返，一条规则，`string`/`println`/`format` 一致。
- `docs/handover/reviews/questions-2026-10-24-evening.md:109`：**"打印只有一种形式：数组的形式"**，
  `tensor(…, dtype=…)` 的文本随 `Tensor` 一起消失，**"不要 `%8.1f` 对齐的那一种"**；元素按"值自己类型的最短
  文本"（已定）。约 90 条 golden 按此更新。

所以规则没有争议；有争议的是**在哪条线上修**：这条对齐形式本身已被判为要消失的形式，而它的替代（数组形式）
属于翻转线 / h588 那次合并。

## 选项与代价

1. **现在在 develop 重写 `formatTensorElement`**（`floatText` + 宽度对齐）：develop 上出现 `tensor(` 的
   文件有 27 个（`test/plans`、`test/cases`、`exemplars` 里，2026-10-26 量的），按 529 逐条重录；收益在
   翻转合并落地后归零，且这批 golden 正是那次合并要改成数组形式的那一批 → 正面冲突。
2. **不动，等数组形式落地**：develop 上保留 `tensor([  0.3333333], dtype=float64)` 到合并为止；
   合并的那一刻缺陷消失（翻转线上已经是 `[0.3333333333333333]`）。
3. **只换精度、留 `tensor(` 外壳**：与 1 同代价（`0.` → `0.0`、`1e+30` 的写法、换行宽度都要一起动，否则
   仍是"一种规则"的反例）。

建议 2；若不允许跨分支等待，则 1 应当明确由 h588 那次合并接管，而不是两条线各改一遍。

## 卡住什么

不卡本次 `-nan` 的修复（已修，见 `test/plans/feat/modules/tensor.plan.toml` 的 `nan_text_in_a_tensor`）。
本页只记录一个**未修**的、已被裁定覆盖的缺陷，请裁定在哪条线修。