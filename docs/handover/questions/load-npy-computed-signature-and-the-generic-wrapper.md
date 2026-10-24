# `load_npy` 按裁定做成计算签名后，有一处过渡损失要你确认

按 `docs/handover/reviews/questions-2026-10-24-evening.md` §h450-spelling-and-print-form-leftovers 的要求（`load_npy::<T>(path: string): T[..]`，运行时核对文件 dtype、不符报带位置的错误，**不保留元素类型未定的版本**），我们已落地。落地时发现**那行声明今天不能按字面写**，两处实测阻塞：

- **F1：数组类型进不了 `Tensor` 位置，也 `as` 不过去。** `Tensor = numericUnknown`、`Tensor<f32>` 是 `TensorType` 而非 `ArrayType`；`TensorType::assignableFrom(ArrayType) = false`、`castSafetyFrom = Forbidden`（`modules/tensor/type.cpp:244-247,297-301`）。实测 7 处：`take(t: Tensor)`、`take(t: Tensor<f32>)`、`sum(x)`、`x + x` 全报 `NoMatchingFunction`，`x as Tensor<f32>` 报 `DynamicCastForbidden`。所以 `T[..]` 作返回类型今天是死路。
- **F2：`Tensor<T>` 带类型变量非法。** `stdlib/signatures/tensor.cml:113:42`：`The arguments of the type 'Tensor<...>' are not accepted: a type variable cannot be an argument of a module type`。

**落地的做法（路线 B：计算签名，一个元素类型一条规则）**，与既有的 `from_bytes` 完全同构（`FromBytesResolver`，`modules/tensor/ops/binary_ops.cpp:60-103`）：

- `modules/tensor/ops/utility_ops.cpp` 新增 `LoadNpyResolver` + `loadNpyOperatorGroup()`，5 条 `OpDef`（f32/f64/i32/i64/bool，与 `npy.cpp:160-168` 支持的 descr 一一对应）；`signature()` 渲染候选行如 `::<float32>(path: string) => Tensor<float32>`。
- **裁定那一行逐字落在 `stdlib/signatures/tensor.cml`**：`export func load_npy::<T>(path: string): T[..]`（用 `#op<'tensor:load_npy_*'>` 记录计算签名）；`stdlib/tensor.cml` 的旧声明删除；`npy.cpp` 的 `loadNpy` 增加 `declaredElement`，dtype 不符即报带位置的错。
- 无期望类型处 `load_npy('x.npy')` **不解析**（`NoMatchingFunction`，候选 5 行）——没有留"元素类型未定数组"的版本。
- 读数：整层红集与改前**逐条相同**（都只有 `docs.links.citations`、`docs.snippets_compile`），新增 3 条用例全绿；132 处调用点（43 个 `.cml`）全部给出元素类型，其中 129 处 f32、3 处 i64（`showcase/c2-mlp-mnist/train.cml:95,97,98`，生成脚本写 int64）。

## 要你确认的一件事

**过渡损失**：泛型包装

```camel
func f::<T>(p: string): T[..] { return load_npy::<T>(p) }
```

**不再编译**（实测 `Return type 'Tensor<float32>' does not match the function's declared return type 'f32[..]' [in the instance 'load_as<f32>']`），根因就是 F1。这不是拒绝逻辑变了，而是"返回值是 `Tensor<f32>`、形参声明是 `f32[..]`"两种拼写今天不等价。

我们的判断是：**P4 的别名（`Tensor<f32> ≡ f32[..]`）落地后，计算签名可以原样改回声明式的 `load_npy::<T>(path: string): T[..]`，语义不变**，这处过渡损失随之消失；在那之前不保留未定类型的版本（按裁定）。请确认这个判断，或者给出过渡期该怎么做（例如是否需要一个临时包装，或是否接受"泛型包装暂时不编译"）。