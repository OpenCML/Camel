# `t[idx]` 的核心规则已统一，模块副本的删除要等 P4（实测，2026-10-25，h529 lane）

（前提：`docs/handover/reviews/questions-2026-10-24-evening.md`
§subscript-operator-choice-and-the-rank-two-refusal 的裁定——`t[idx]` 只有一条规则、由核心拥有，
模块的 `tensor:idx_take` 并入核心后删除；以及 `docs/handover/questions/
subscript-operator-choice-and-the-rank-two-refusal.md`（h446 lane）实测的裂缝。）

本页是**交接项**，不是新的待裁项：裁定已经把终态定死，本页只给"这一步在今天的树上做不了"的读数，
以及 P4（`Tensor` 作为原生数组别名）落地后要做的那一步。

## 一、本 lane 已落地的可执行子集（同一份裁定里不依赖 P4 的部分）

| 位置 | 规则 |
|---|---|
| `src/builtin/modules/globals.cpp` 的 `:op/idx_take` 解析器 | 结果形状按 `[len(idx)] + shape(t)[1:]` 在**声明层**给出：`int[3]` 下标把首维写成 `3`，`int[]` 下标写成未知；输入类型开放秩（`f32[..]`）时结果就是它自己；数值未定数组走 `numericUnknownFromForms` |
| `src/builtin/operators/struct.cpp` 的 `__idx_take_arr__` | 沿第 0 维取**行**，任意秩：秩-1 的行就是元素；秩-2 的 `f32[2, 3]` 在 `int[3]` 下标下是 `f32[3, 3]`；结果形状 `[len(idx)] + shape(t)[1:]`，是副本；越界时秩-1 的报文与从前逐字相同（`Index 7 is out of range for an array of length 5.`），更深时说 `the first dimension of the input array` |
| `src/builtin/operators/struct.cpp` 的 `__tangent_add_arr__`、`__tangent_zeros_arr__` | 按**形状**而不是 `size()`（`shapeSpan()` + `Array::create(alloc, type, extents)` + `getSlot/setSlot`），切线的秩与素值相同 |
| `src/builtin/operators/struct.cpp` 的 `__tangent_scatter_take_arr__`（新增），`src/builtin/operators/struct.h`，`src/builtin/executors/builtin.cpp` | `base` 的副本，把 `delta` 的行按 `indices` 加回对应行，**重复索引累加**；越界按 `base->dim(0)` 判 |
| `src/builtin/operators/derivative.cpp` | 新规则 `:op/idx_take`（先 `tangent/zeros_arr(t)`，再 `tangent/scatter_take_arr(zero, indices, dy)`）与 `:tangent/scatter_take_arr`（`base` 取上游、`delta` 取 `:op/idx_take(upstream, indices)`）：取行的反向是**索引 scatter-add** |

实测（主机 `88zjwei`，本 lane 树 `out/latest`，`std::nvm` 与 `std::fvm` 一致）：

| 用例 | 读数 |
|---|---|
| `linear.subscript.subscript.take_rows.{nvm,fvm}` | PASS：`f32[5]` 在 `int[3]` 下返回 `f32[3]`、`f32[2, 3]` 返回 `f32[3, 3]`、`f32[4, 2, 2]` 在 `int[2]` 下返回 `f32[2, 2, 2]` 都在**函数声明**上被检查（把 rank-2 的返回类型写成 `f32[2, 3]`，编译器报 `Return type 'f32[3, 3]' does not match the function's declared return type 'f32[2, 3]'`） |
| `modules.autodiff.array_take_grad.{runtime.nvm,macro.fvm}` | PASS：`nonzero=136.0`，`xs[[0,2,0]]` 权重 `2,3,5` 得 `[7.0, 0.0, 3.0, 0.0, 0.0]`（重复索引累加），普通两行得 `[0.0, 3.0, 0.0, 0.0, 2.0]`，取行套取行得 `[7.0, 0.0, 4.0, 0.0, 0.0]` |

裁定里"秩 ≥2 未 import tensor 就无候选"的过渡期拒绝随本子集**失去必要**（核心对所有秩都有行语义了）；
而那条拒绝本身（h446 lane 的 `33d006191`）不是 `origin/develop` 的祖先，所以本基线上没有要改写的用例。

## 二、实测：今天删掉模块副本，快层闸门从 `fail 0` 变成 `fail 9`

实验：把 `modules/tensor/ops/layout_ops.cpp` 里 `idx_take` 的 `.exports = {"__idx__"}` 置空
（等效于"模块副本不再参与 `[]` 解析"，函数形式 `tensor::take` 不受影响；**本 lane 未提交这个改动**），
然后跑 `smoke,test/plans/feat -Tier fast -Jobs 8`：`total: 6184, pass: 6156, fail: 9, skip: 11,
known failures: 8`（同一棵树不删模块副本时是 `fail: 0`）。

| 变红的用例 | 逐字报文（截断） |
|---|---|
| `modules.tensor.subscript.take_rows`（含 `poison:modules.tensor.subscript:take_rows`） | `No matching function found for argument types: <> (Tensor<float64, [4]>, int[]); operator '[]' is not defined ...` |
| `modules.tensor.dtype.gather_grad_f64.{nvm,fvm}` | `... (Tensor<float64>, Tensor<int64>); operator '[]' is not defined ...` |
| `modules.autodiff.discrete_zero.{nvm,fvm,macro.nvm}` | 同上（`Tensor` 取值） |
| `modules.autodiff.gather_discrete_index_zero.{runtime.nvm,macro.fvm}` | 同上（`m.weights[m.labels]`，两者静态都是 `Tensor`） |

## 三、为什么现在删会新增红：两条规则在类型上互不重叠

* `modules/tensor/module.cpp` 仍然 `exportType(Reference("Tensor"), camel::tensor::TensorType::Default())`：
  在**没有 P4** 的树上，`Tensor` / `Tensor<f64>` / `reshape(...)` 的结果都是 `TensorType`（一个独立的 `OtherType`），
  不是 `ArrayType`。
* 核心的 `:op/idx_take` 要求 `norm[0]->code() == TypeCode::Array`，模块的 `idx_take` 要求
  `ParamKind::Tensor`（=`asTensorType(type) != nullptr`）。于是今天两边答的是**互不相交的类型类**：
  核心答 `f32[2, 3]` 这类写出来的数组拼写，模块答一切 `TensorType` 值。
* `test/cases/compile/array_alias/array_alias.plan.toml` 的每一条都带
  `known_fail_note = "P4 (... ) has not landed"`，而且这个计划文件位于 `test/cases/` 下、
  不被任何运行选中：P4 的验收套件本身就在等它落地。
* 反向也成立：h446 lane 实测过"让核心对所有数值数组让位"会打断 31 条**秩-1**用例
  （`stdlib.array_hof.*`、`subscript.forms.*` 等，见该页第二节），因为那些程序的 `[]` 只有核心有候选。

## 四、P4 落地后要做的那一步（交接内容）

1. 删掉 `modules/tensor/ops/layout_ops.cpp` 的 `OpDef idx_take`（或至少它的 `.exports = {"__idx__"}`）与
   `setVjp(defs, "idx_take", &takeVjp)`，把 `modules/tensor/ops/layout_ops.cpp` 里
   `subscriptTakeInfer` 的 `t[idx]: ` 前缀与 `takeKernel`/`takeGrad` 的行取语义**只**留给函数形式
   `tensor::take`（裁定保留了它）。
2. 把 `export_matrix.csv` / `export_matrix.md` 里 `tensor:idx_take` 一行去掉，并看 `ROADMAP.md`
   里引用的计数是否需要跟着动。
3. 把 `test/cases/modules/tensor/subscript/take_rows.cml`、`dtype/gather_grad_f64.cml`、
   `autodiff/discrete_zero.cml`、`autodiff/gather_discrete_index_zero.cml` 的 `t[idx]` 改为落在核心
   （P4 之后它们本来就是 `ArrayType` 值），确认 `shape(rows)` 这类读数仍与
   `[len(idx)] + shape(t)[1:]` 一致。
4. 反向规则不需要再补：本 lane 已给核心 `:op/idx_take` 与 `:tangent/scatter_take_arr` 注册了规则，
   模块的 `takeVjp` 删除后不会留下"no derivative rule"。

## 五、读数口径

读数取自本 lane 的树（`out/latest`，基线 `8f1a2f1ac`，主机 `88zjwei`），闸门命令
`smoke,test/plans/feat -Tier fast -Jobs 8`（`-Jobs 8`；该主机的 `gpu_preference.*` 在更高并发下会假红）。
本页的代码命名与"哪一边答什么类型"的判定，与 `git merge-base --is-ancestor 33d006191 origin/develop`
（退出 1）与 `modules/tensor/module.cpp` 的 `exportType(Reference("Tensor"), ...)` 一致。