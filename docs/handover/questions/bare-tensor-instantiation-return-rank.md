# 裸 `Tensor`：形参实例化落了，返回值端实测**不能**实例化（h470 lane，2026-10-25）

> 本文只报选题与读数，不定语义。作者页面（`docs/handover/README.cn.md`、`reviews/**`、
> `docs/design-principles*`、`docs/design/**`、`docs/language/**`）一个字未动。

## 一、落下的部分

按 2026-10-23 裁定与 lane 任务书，**调用点按元素类型实例化裸 `Tensor` 形参**：声明的形参类型里每一处
裸 `Tensor`（`ArrayType::numericUnknown()`，开放秩 + `Void` 元素）在调用点换成 `T[..]`，`T` = 实参的
元素类型；**extent 与秩都不代入**（`src/compile/gir/builder_operators.cpp`
`Builder::instantiatedTensorSignature`，调用点 `:3266-3281`）。声明本身不改写（一个图服务所有元素类型，
到 P4 才删这条拼写）。实参在裸位置是裸 `Tensor`（元素未知）、不是数组、或两个实参元素类型不同时，
**不作实例化**，声明按原样使用。

实测：`<> (f32[?, ?], f32[..])`——`func grow(t: Tensor): Tensor` 的调用点在实例化后读到 `f32[..]`
（`notes/h470-probe3.log` 的 probe_c），实例化路径确实被走到。

## 二、返回值端：三条已绿用例证明**不能**按实参实例化

把返回值也按实参的元素类型（`f32[..]`）或按实参的具体类型（`f32[?, ?]`）实例化，都被已绿用例挡住。
逐字读数（`notes/h470-fix2.log`、`notes/h470-fix3.log`、`notes/h470-after-full.log`）：

1. **元素类型不跟随实参**：`test/cases/modules/onnx/export_capability.cml:14-15`

   ```
   func counts(t: Tensor): Tensor {
       return relu(cast(t, 'int64') - 2)
   }
   ```

   调用点 `:20` 的实参是 `f32`（`reshape([1.0, 2.0, 3.0, 4.0] as f32[], [2, 2])`），返回的是 **int64** 张量。
   按实参元素实例化后运行期受检转换拒绝：

   ```
   export_capability.cml:20:19: [Error]: conversion to 'f32[..]' failed: the array has element type int
   ```

   同一形状的第二例：`export_float_array.cml:17:17`：
   `conversion to 'f32[..]' failed: the array has element type float`（实参 `f32`，返回 `f64`）。
   这两例连带把 `capability_vs_torch`、`03_export_capability` 也弄红（导出侧不再报"int64 的 `Relu`
   在 opset 17 没有 kernel"）。**这四条的读数见 `notes/h470-after-full.log`**；本 lane 已回退。
2. **秩不跟随实参**：`test/cases/modules/tensor/print_value.cml:14`
   `func forward(x: Tensor): Tensor { return zeros([shape(x)[0]]) + 1.0 }` 从秩 2 实参返回秩 1 值。
   按实参秩（`f32[?, ?]`）实例化时 `print_value.{nvm,fvm,jit,prl}` 四条在运行期被拒：
   `conversion to 'f32[?, ?]' failed: the array has shape [2]`（`notes/h470-fix3.log`）。
3. **extent 不跟随实参**：`test/cases/modules/nn/mnist.cml:77`
   `func build_one_hot(i: int, acc: Tensor): Tensor` 是形状递增的递归累加器；按首个实参
   `zeros([1, num_classes])` 的具体类型实例化会把形参钉成 `f32[1, ?]`，第二步就不成立：
   `mnist.cml:87:33: conversion to 'f32[1, ?]' failed: the array has shape [100, 10]`（`notes/h470-fix2.log`）。

结论（可证）：**不做函数体实例化的前提下，`Tensor` 返回值的元素类型、秩、extent 三者都推不出来**；
把任一项按实参写下来，都要么在运行期被受检转换拒（上面三条），要么成为没有依据的静态声明。
`docs/handover/questions/bare-tensor-parameter-instantiation-measured.md:65` 也已说过"只实例化形参不足以
去掉出口图里的 `f32[..]`"。

## 三、`lrit` / `mnist` / `static_types` 仍然红，且三条路互斥

逐字读数（`notes/h470-fix5.log`）：

- `lrit.cml:78:5`（`w = train<X, y, lr, its>(w)`）：`<> (f32[?, ?], Tensor); operator '=' is not defined`
- `mnist.cml:92:5`（`W = train<X_bias, y_onehot, lr, its>(W)`）：同上
- `static_types.cml:22:5`（`w = ones([5])`）：`<> (f32[?, ?], f32[5]); operator '=' is not defined`

裁定 `reviews/questions-2026-10-23.md:378` 说"实例化后形参类型就是实参的 `f32[?, ?]`，cell 的内容类型相等
成立，cell 不变式不放宽"。要让 `w = train<...>(w)` 合法，只有三条路，三条都被已绿用例挡住：

1. 返回值按实参类型（含秩）实例化 → 第二节 2、3 挡住；
2. 返回值按元素类型实例化（`f32[..]`，本 lane 落下的）→ `f32[?, ?] ← f32[..]` 被
   `ArrayType::assignableFrom`（`src/core/type/composite/array.cpp:625-628`，5.4"开放秩不承诺秩"）拒；
3. `var` cell 加宽成开放秩（develop 的 `Tensor<f32>` 语义）→ 被 `alias.var_widening`、`alias.err_var_rank`
   （已绿；plan `feat/compile/array_alias.plan.toml:108`/`:120`）挡住，且
   `docs/design/arrays-memory/plan.cn.md:784` 的 **Q5 已裁定同秩加宽**。

`static_types.cml:22` 与第 3 条是同一件事：`docs/design/arrays-memory/plan.cn.md:323`、`:447` **已经**把这条
用例列进"按 Q5 结论改写（两次 `var` 或显式 `as`）"的名单。

## 四、请裁定

1. `questions-2026-10-23.md:378` 那句要不要按实测改写为"**只有形参实例化，且只代入元素类型**"？
   若是，`lrit`/`mnist` 要不要按 Q5 的结论改写用例（`var w: f32[..]` 或对返回值写 `as`）？
2. `var` 形参（`var w: Tensor`）要不要一并实例化元素类型？本 lane 落的是"一并"（callee 体内 cell 的
   类型也参与比较）；裁定"只按值形参与返回"时改一行即可。
3. `print_value`/`forward`、`export_capability`/`counts`、`mnist`/`build_one_hot` 这类"返回值与形参无关"
   的写法，在 P4 的 `T[..]` 终态下按什么算：`T[..]` 只承诺元素类型，所以它们合法；那么 `lrit` 这族的
   `w = train(...)`（`var` cell 同秩）在终态下是不是本来就该报错？