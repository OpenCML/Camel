# h450（拼写与打印形式）没有落地完的部分：实测与判断依据（2026-10-25）

本文件只记 **h450 lane 做不到终态、或做到了但需要别的车道/作者注意** 的事。全部结论都来自本树实测
（`handover/h450-flip-spelling`，host `88zjwei`，计划 `smoke,test/plans/feat -Tier fast -Jobs 8`）。

## 一、数组打印机丢掉了内层开括号（**已在本 lane 修好**，别的车道需要知道）

裁定要求「多维用嵌套方括号：`[[0.0, 1.0], [2.0, 3.0]]`」。实测终态打印机给的是
**`[0.0, 1.0], [2.0, 3.0]]`** —— 秩 ≥ 2 的数值数组**少了最内一层的 `[`**：

```
println(reshape([1.0, 2.0, 3.0, 4.0], [2, 2]))   ->   [1.0, 2.0], 3.0, 4.0]]      <- 缺 "["
println([[1, 2], [3, 4]])                          ->   [[1, 2], [3, 4]]           <- 这条对，但走的是嵌套类型
```

根因在 `src/core/rtdata/print.cpp:71-94` 的 `pushArray`：它每一层压 `"]"`，却只由**调用方**
（`:256-257` 的 `os << "["`）写一次开括号，所以深层没有对应的 `[`。修法是递归返回后补压 `"["`
（一行），本 lane 已改并随本批提交；改前秩 ≥ 2 的数值数组打印即上面那行，改后为 `[[1.0, 2.0], [3.0, 4.0]]`。
**这是一处代码缺陷，不是用例措辞**：不修就没法把 golden 写成裁定的形式（写成 `[a, b], c, d]]` 等于钉住缺陷）。

## 二、未述元素类型的叶子：22 处里 8 处已按终态改写，14 处需要再一轮

裁定：「不为过渡类型发明语义，这 22 处用例按终态写法改写（显式泛型或写明元素类型）」。实测 22 处
（= 本 lane 基线的 22 条红）及处置：

| 处 | 实测错误 | 处置 |
|---|---|---|
| `numeric_contract.{nvm,fvm,jit,prl,generic_unit}` 5 + `declared_numeric_semantics` 1 | `sum(bool)` `NoMatchingFunction (bool)` | **已改**：`cast(s != s,'int64')` 也被拒（`cast` 只收 `Tensor | number[]`，`bool[]` 不是 `number[]`），改为打印整条轴 `softmax axis=[nan, nan, nan]`，与用例本意（整轴 NaN）一致 |
| `native.tensor_wrong_arity`、`native.tensor_show_wrong_arity` 2 | 实参文本是 `(f32[?, ?], int)`，用例写 `(Tensor<float32, [?, ?]>, int)` | **已改**（纯拼写） |
| `opt.generic/arms_laid_out_whole_tensor.*` 5 + `opt.screens/generic.arms_laid_out_whole_tensor` 1 | `x[0] + 2.6` 报 `NoMatchingFunction (void, float)`：`load_npy` 返回裸 `Tensor`，`x[0]` 的静态类型是 `void` | **未改**：要把 `load_npy(...)` 的结果写成有元素类型的形态（`as f32[…]` 还是显式泛型参数）需要一轮实测 |
| `trans.annotated_dump/boundaries.{agir,argir,tooltip_slots}` 3 + `mm.gc_poison_sweep/poison_trans.annotated_dump_boundaries.agir` 1 | `t[0] > 0.5` 报 `NoMatchingFunction (void, float)`：`gate(t: Tensor)` 的形参元素类型未述 | **未改**：同上，形参要写成 `f32[..]`/`Tensor<f32, [..]>`，也要实测 |
| `modules.onnx/{export_branch,ort_branch,ort_dynamic_batch}` 3 + `opt.screens/onnx.export_branch` 1 | 同上（`tensor:gt` 收到 `void` 叶） | **未改**：`modules/onnx/**` 是 h449 的地盘，按纪律不碰 |

`numeric_contract.cml:57` 那条同时钉住了「bool 不是 numeric」这条规则（`is_numeric` 拒绝是对的），
改写保住了这一点：注释里写明了为什么不能计数。

## 三、拼写族里两处**不是措辞**的变化，需要作者/集成确认

1. `modules.tensor.shape_types/unshaped_argument`：用例期望 `SemanticDiag::ArgumentNeedsConversion`
   加一句「less exact」帮助；终态给的是 `SemanticDiag::NoMatchingFunction` 加一串重载表（`(Tensor)`）。
   `ArgumentNeedsConversion` 这个码还在（`diagnostics/registry.tsv:75`，`builder_diagnostics.cpp:1292`
   仍在产生它，`shape_inference` 里还有绿用例在测它），所以这是**同一现象换了诊断**，不是措辞。本 lane 未动。
2. `modules.tensor.bounds/{index_const,index_declared_shape}`：用例期望**编译期** `SemanticDiag::ArgumentsRejected`
   「`index N is outside axis 0 of extent 3`」；终态**编译通过、跑起来、打印 `ran`**，之后才是运行期
   `RuntimeDiag::IndexOutOfRange`。即数组下标丢了静态越界检查（`StaticAccessIndexOutOfRange` 只服务元组，
   见 `builder_static.cpp:1152,1196`）。这是**回归**，本 lane 不动代码。
3. `trans.cpp/refuse_tensor`、`refuse_tensor_json`：终态的拒绝理由是
   `function 'main' uses the operator 'tensor:ones' on a value of type 'int[]', which has no translation in std::cpp yet.`（第 14/15 行），
   用例期望的是 `function 'scale' returns a value of type 'Tensor'`（第 10 行）：**主体、行号、verify 脚本都不同**，
   不是措辞。
4. `modules.graph/facts_of_graphs_and_nodes`：`shapeKernel`（`modules/graph/facts.cpp:69-79`）现在对未述
   元素类型的值给 `f32 [?]`、`int [?]`，以前给 `-`。这是**信息更全**（`-` → 具体元素类型和秩），
   本 lane 已按终态更新用例，但它是 facts 表面的行为变化，请集成确认。

## 四、本 lane 明确不碰的（同族但在别的车道）

- `test/plans/feat/compile/entry_args.plan.toml`（h451）2 处旧打印形式；
- `test/plans/feat/modules/tiles.plan.toml` 6 处 + `test/plans/gpu/tiles_regions.plan.toml` 1 处（h448）；
- `modules/onnx/**`（h449）4 处（见上表）；
- `opt.generic/fold_cast.rewrites`、`modules.tensor.dtype/gather_grad_f64.*`、`modules.tensor.compound_assign/*`、
  `modules.definitions/kernels*`、`modules.naive_in_expert_out/*`、`linear.loops/*`、`docs.*`、`examples/*`
  （golden 快照，走 `run-tests.mjs --update-golden`）等仍未处理，原因见报告。

## 五、静态越界检查整族丢失（**根因已定位；修在 core，不属本 lane**）

规范依据是明确的：`docs/design-principles.cn.md:211`「**越界**：编译期能知道长度时（尤其是固定长度的
类型），`slice` 与下标的越界应当是编译期错误，而不是悄悄截断或留到运行期。」

实测（本树，日志戳 `20261024_122250_696948`）——不是措辞，是**行为变了**：

| 用例 | 用例期望 | 终态实测 |
|---|---|---|
| `modules.tensor.bounds/index_const` | 退出 1 + `SemanticDiag::ArgumentsRejected`「index 8 is outside axis 0 of extent 3」 | 先打印 `ran`，再 `RuntimeDiag::IndexOutOfRange`「Index 8 is out of range for an array of length 3.」（`index_const.cml:6:14`） |
| `modules.tensor.bounds/index_declared_shape` | 退出 1 + 「index 5 is outside axis 0 of extent 3」 | 先打印 `ran`，再运行期 `IndexOutOfRange`（`index_declared_shape.cml:4:13`） |
| `modules.tensor.subscript/slice_const_bounds` | 编译期「slice bounds 0..9 are outside the first dimension …」 | 运行期「The range 0 to 9 is out of range for an array of length 4」 |
| `modules.tensor.subscript/slice_rank2` | 编译期「rank 2 or more」拒绝 | 无该诊断 |
| `modules.tensor.subscript/slice_step`、`slice_step_negative` | `expect_exit = 1` | **`expected exit 1, got 0`**：编译通过、跑完 |

根因：这些检查原先在 **tensor 模块的 infer** 里——`modules/tensor/ops/utility_ops.cpp:64-86`
（`indexInfer`：`ctx.constInt(k+1)` 取常量下标、`in.shape[k]` 取静态 extent，越界就
`throw std::invalid_argument("index {} is outside axis {} of extent {}")`，由调用点报成
`SemanticDiag::ArgumentsRejected`）。翻转后 `b` 是 core 数组 `f32[3]`，`b[8]` 改派发到 **core** 的
`:op/idx_arr`（`src/builtin/modules/globals.cpp:1697-1707`）：它的 resolver 只拿到类型、拿不到常量，
越界留给运行期内核 `src/builtin/operators/ops.cpp:793`
（`throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "an array", arr->size())`）。
tensor 模块的 infer 不再被选中 ⇒ **静态检查整体消失**，只剩运行期检查。

归属与修法：这是 **core 数组下标/切片语义**（与已裁定的「`for` over an array 是 core 的」同族），而
`h453-core-loop-domain` 正在改 `src/compile/gir/builder_control.cpp`、`src/core/rtdata/array.cpp`、
`src/core/type/composite/array.cpp`、`include/camel/core/**`。修法要在 core 的 `[]`/`slice` 路径上按
规范补回同样的静态检查（常量下标 vs 静态 extent、rank、切片界），会连带改动
`test/plans/feat/linear/subscript.plan.toml` 与 `test/plans/feat/consistency/faults.plan.toml` 的期望，
所以按纪律报回，不在 h450 分支落。

## 六、`ArgumentNeedsConversion` 为什么不再报（`shape_types/unshaped_argument`）

终态：`SemanticDiag::NoMatchingFunction`，实参类型 `(Tensor)`，只列 `f: (b: Tensor<[3]>) => float`；
用例期望 `SemanticDiag::ArgumentNeedsConversion` +「argument 1 ('t', for parameter 'b') has type
'Tensor', which is less exact than 'Tensor<?, [3]>'」+ 写法提示。

根因：`ArgumentNeedsConversion` 在 `src/compile/gir/builder_diagnostics.cpp:1289-1298` 报出，条件是
**某个候选被判为「fits」**（类型相容、只差一处可写出来的转换）。现在 `Tensor` 传到
`Tensor<[3]>`（=`Tensor<?, [3]>`）不再被判为 fits，于是提前落到 `NoMatchingFunction`。

归属：这是「未述/部分述类型的相容关系」，与作者在 `docs/handover/reviews/questions-2026-10-23.md:620-627`
的两条裁定（`unstated-element-type-scalar-and-print`、`tensor-alias-transition`）同族。需要作者定
「`Tensor` 到 `Tensor<?, [3]>` 还算不算 less exact、还算不算可以写的转换」，本 lane 不能替它定。