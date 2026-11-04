# `tensor:shape` 的声明返回 `int[]`（无长度）——要不要让它能说「每轴一条」？

**状态**：待裁。这不是 `sum_to` 的事，是语言层的事：一个列表的**长度**今天在类型里写不出来。`sum_to`
只是第一个撞上它的算子。本页给读数、给默认（第 3 节）、给一句可裁的话（第 4 节），不改任何东西。

## 1. 让它是问句的那一组读数（照抄）

`modules.onnx` 从 36 红降到 8 红之后，那 8 条**全部停在同一句**：

```
tensor:matmul_grad_lhs: the rank of an operand is not known statically
```

那 8 条是：`export_train_step_dynamic`、`export_train_step_dynamic_mean`、
`ort_train_step_dynamic`、`ort_dynamic_params`、`ort_grad_loop_values`、
`ort_loop_values_dynamic`、`ort_loop_loaded_weights`、`pass_export_equals_export_model`。

链条是：

`tensor:shape` 的声明返回 `int[]`（`stdlib/tensor.cml#shape`：`func shape::<T>(t: T[...]): int[]`，
`int[]` 是"一条未知轴"，不是"n 条"）→ 由它构成的 `sum_to` 的 target **长度不是事实** →
`sum_to` 的结果 rank 说不出来 → 动态 batch 的反向图里 rank 一路缺失 →
`tensor:matmul_grad_lhs` / `lowerMatmulGrad` 的
`staticShape(ctx.facts(Lhs ? 1 : 2), ctx.uri())` 正当拒绝。

**基线为什么能过**：旧 `sum_to` 的声明是 `(t: T[...], like: T): T`，结果类型就是 `like` 的类型，
**rank 从 `like` 的类型白拿**——正是甲判掉的那份复印件。甲把它拿掉之后，rank 只能从
`shape(x)` 的长度来，而"一个列表有多长"今天在声明里写不出来。

## 2. 已经试过、不成的那两条（免得下一个人再走一遍）

1. **在 C++ 的 `infer` 里说出长度。** `shapeInfer`（`modules/tensor/ops/layout_ops.cpp#shapeInfer`）
   改成 `ArrayType::create(Int64, {rank})` 之后，图里仍然是 `int[]`：`shape` 是**声明算子**
   （`exports = {"shape"}`），签名由 `stdlib/tensor.cml` 的声明钉住，infer 不覆盖它。
2. **在发节点的地方给类型。** `shapeListType`
   （`modules/tensor/ops/vjp.cpp#shapeListType`）在 `addOper` 时给 `int64[rank]`，图里仍然
   是 `int[]`——发射那一刻的操作数类型还没被推断，读不到 rank。

两条都说明同一件事：**这个事实只能由声明来说**。

## 3. 我方默认（按此继续，不等裁定）

- **把那 8 条按 `known_fail` 记在 `test/plans/feat/modules/onnx.plan.toml` 上**，
  `known_fail_reason` 指向本页，并**逐条写「rank 来自 `like` 的类型，已被甲判掉」**。
- **不改 `shape` 的声明**——那要先有裁定。
- **不改那 8 条的 pin。**

理由：设计原则说文档与测试冲突时文档优先，冲突记成缺陷或待办而不是改测试；甲判掉的那份
复印件不能靠改测试拿回来。把 8 条记成 `known_fail` 是**记账**，不是变绿：`known failures` 会
从 0 涨到 8，涨的这一笔就是本页的问题。

## 4. 需要裁的那一句

**`tensor:shape` 的声明今天返回 `int[]`（无长度）。要不要让它能说「每轴一条」（`int[N]` 式
依赖 extent 的返回类型）？**

若裁"要"，形状是：`shape::<T>(t: T[N]): int[N]` 之类，且 `T[..]`（开放秩）时退回 `int[]`；
8 条随之可解，`sum_to` 的 rank 从目标长度来、extents 仍留在运行期的值里（与甲一致）。
若裁"不要"，那么动态 batch 的反向图导出需要另一条路——今天没有，这 8 条就停在 `known_fail`。

## 5. 这一笔里另一个更值钱的发现（不属于本页的问题，但同源）

`let _ = export_model<...>(...)` 把 `void!` 的值丢掉，`ExportUnsupported` 无处落地：拒绝发生的
程序照样打印 `exported`、退出码 0、磁盘上没有文件。同一个程序走 `onnx::export` 一句话就拿到
拒绝理由。这一条有独立用例（`test/cases/modules/onnx/export_refusal_is_visible.cml`），判据是
**这个程序不再打印 `exported`**：要么印出 `ExportUnsupported`，要么退出码非零。