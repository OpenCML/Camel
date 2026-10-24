# `Tensor` 别名的过渡类型：删除步与依赖它的新代码

裁定原文见 `docs/handover/reviews/questions-2026-10-23.md` 第 217 行一节（"抽查：未定元素类型的数组
（`Tensor` 别名的过渡）"）。结论是：**元素类型未定的数值数组（裸 `Tensor` 的别名结果）只是迁移中的过渡，
不是目标类型**；过渡期内可以存在，但 P4 的计划里要写明删除它的那一步，并且不让新代码依赖它。

这份记录做两件事：把**计划里该补的那一段**写出来（`docs/design/**` 是本次任务不许改的路径，所以交给
计划的所有者落进去），以及把**本次改动里依赖它的位置**逐个列出，供删除步处理。所有测量取自
`handover/h366-tensor-flip`（`origin/develop` `221cfe18b` + 本分支改动）。

## 一、建议加进计划的文字（P4 一节、5.1 之后）

> **5.1a 过渡状态与删除步。** `Tensor` 作为 `ArrayType::numericUnknown()` 的导出、以及由此产生的"数值、
> 元素类型未定的数组"是**迁移中的过渡**，不是语言保留的类型（设计原则 5.5：图上只有具体类型，运行时
> 不需要类型信息；泛型要在头部用 `::<T>` 声明，裸 `Tensor` 形参等于隐式泛型，已否决）。
>
> P4 的终点：
> - 签名里的裸 `Tensor` 是**编译错误**，诊断提示写 `Tensor<T>` 并在头部声明 `T`，或写具体元素类型
>   （`f32` 等）；
> - 标准库的张量运算改为 `func op::<T>(x: Tensor<T>, …) when is_float::<T>()` 一类写法（按
>   `log_softmax` 的裁定）；
> - **值的元素类型总在类型里**；形状可以是 `?`（大小运行时才知道是值的事，不是类型的事）。
>
> 删除步（作为 P4 之后的一次独立提交，或 P4 提交内最后一步，由计划的所有者定）：
> 1. `ArrayType::numericUnknown*` 与其标记 `numericUnknown_` 删除；`Tensor` 的 `Reference` 改为
>    `Tensor<T>` 形式（元素类型参数必填，`Tensor<..>` 的 `applyArguments` 只接受类型参数 + 形状参数）；
> 2. 签名里出现裸 `Tensor` 的诊断（`InvalidTypeArgument` 或新的语法诊断）；
> 3. 本页第二节列出的每一处过渡依赖同时删除；
> 4. `test/cases/compile/array_alias/bare_tensor.cml` 与
>    `test/plans/feat/compile/array_alias.plan.toml` 的 `alias.bare_tensor` 一条改写成"裸 `Tensor` 形参
>    是编译错误"的负例；`test/cases/loops/iterate_tensor.cml` 的 `countRows(t: Tensor)` 同理改写成
>    `countRows::<T>(t: Tensor<T>)` 或删掉。

## 二、本次改动里依赖过渡类型的位置（删除步要一并处理）

| 位置 | 依赖什么 | 删除步怎么做 |
|---|---|---|
| `modules/tensor/type.cpp#tensorFactsOf` 的 `isNumericUnknown()` 分支 | 读过渡类型的事实（无元素类型） | 该分支随标记一起删；事实只剩"数值数组"一种 |
| `modules/tensor/ops/support.cpp#tensorOf(facts)` 的 `!facts.dtype` 两支 | 造过渡类型（`numericUnknown` / `numericUnknownFromForms`） | 元素类型必有时删掉，改成断言 |
| `modules/tensor/ops/vjp.cpp#vjpTensorType()` | VJP 结果类型是过渡类型（`ArrayType::numericUnknown()`） | 改为由被微分值的切线类型给出具体元素类型 |
| `src/core/derivative.h/.cpp#setTangentType`/`statedTangentType` | 过渡类型的元素是 `Void`，结构上取不到切线，所以新加一个注册钩子 | 随过渡类型删除；`computeTangentType` 的 `Array` 分支恢复只走元素类型 |
| `modules/autodiff/pullback.cpp#carriesGradient` 的 `Array` 分支 | 同上（`tangentTypeOf` 判据） | 可恢复为 `carriesGradient(elemType)` |
| `modules/tensor/module.cpp` 的 `.npy` 加载器/保存器 | 检查值对着 `ArrayType::numericUnknown()` 做形状/元素检查 | 改为对着声明的具体数组类型检查 |
| `modules/tensor/ops/gradient_ops.cpp#tensorTangentType` | 判断过渡类型有没有切线 | 随钩子删除 |
| `modules/onnx/emitter.cpp` 的 `Value::constant(..., numericUnknown())` | 数组常量当张量时的声明类型 | 改为值自己的数组类型 |
| `src/compile/gir/builder_operators.cpp` 的 `var` 实参接管（普通实参与 `with` 实参各一处） | 形参的裸 `Tensor` 与实参值的数组拼法不是同一个类型，cell 的"内容类型必须相等"（`src/core/type/composite/tuple.cpp`）会拒 | 随标记删除：两侧是同一个类型，接管分支删掉（详见下一节） |

**没有让新的消费者依赖它**：所有从 `TensorType` 迁过来的构造点走 `tensor::ops::tensorOf(...)`，元素类型
来自事实（`facts.dtype`）；过渡类型只在"事实里没有元素类型"时出现，即 `Tensor` 的两种拼写本身。

## 三、一处过渡期的已知不精确（记录，不改）

`iterKernel` 在运行期看到秩 1 时给出**元素槽**（裁定：秩 1 迭代给 `T`），`iterInfer` 在静态形状已知时也
给 `T`；但静态类型是**裸 `Tensor`（秩未知）**时 `iterInfer` 给 `Tensor`，而运行期该值若恰好是秩 1，
元素槽与静态的 `Tensor` 不符。今天的用例只在这种形参上传秩 ≥ 2 的值（`iterate_tensor.cml` 的
`countRows(m)`），所以没有可观测的错；秩未知的形参正是删除步要删掉的写法，故不改 `iterKernel`。
若删除步之前要写这种程序（秩未知形参 + 秩 1 实参 + 把轮次当张量用），它会读到错的值。

## `var` 实参的 cell 按声明形参类型放行：h419 有这一段，`develop`/h438 没有

（测量取自 `handover/h455-flip-var-params` = `handover/h443-flip-survey` + `origin/develop` `3d46c4406`，
即翻转线；对照 `origin/develop` 与 `handover/h438-tensor-alias`。）

一个 `var` 绑定是调用方的一个**槽**（cell）：`:var/new` 造槽，`:var/get`/`:var/peek` 读，`:op/assn_owned`、
`:var/take` 在原地改写。槽的**内容类型必须相等**（`TupleType::assignableFrom`，
`src/core/type/composite/tuple.cpp`）——这是决定，不放宽。`var` 形参写 `Tensor` 时，槽的内容是**值的数组
拼法**（`f32[?]`、`f32[2, 3]` …），与形参声明的裸 `Tensor`（`ArrayType::numericUnknown()`）不是同一个类型，
于是调用点被拒。补救在**调用点接管**，不在相等规则里：

- 普通实参：`src/compile/gir/builder_operators.cpp` 的 `enter(...)` 一段，`fdc0813d0`（"give a `var`
  argument's cell the declared parameter type"）落了这条，负例 `e57d5febe`（非数值槽仍拒）。
- `with` 实参：同一规则的另一半，`fdc0813d0` **没有**覆盖（它只扫 `normInputs`）。本 lane 在 `withInputs`
  上加同一段（`59c4dd098`）。两侧逐字：

  ```
  // 红前（with <var acc: Tensor>，最终产物、源码回退）
  [FAIL] si_var_argument_with_parameter  197.63 ms
      [INFO] expected exit 0, got 1
      [stdout] camel: internal error: Runtime graph 'main' breaks its edge invariants (1 violation(s)):
      [stdout]   graph 'main' node 7 (10): passes a var float[] as 'with' argument 0 of 'step', which takes a var Tensor
  // 绿后
  [PASS] si_var_argument_with_parameter  131.24 ms
  ```

  GIR 里两侧对齐（`std::gir`，调用方的槽与 callee 的 `with` 端口）：
  `N18 [label=":var/new", type="OPER", value_type="var Tensor"]` ↔
  `N00 [label="acc", type="PORT", param_kind="with", param_index="0", value_type="var Tensor"]`；
  `std::jgir` 同一节点 `{"label": ":var/new", "outputType": "var Tensor"}`。

**实测：这一族在翻转线上已经全绿，在 `develop`/h438 上会重现。**
`test/plans/feat/linear/var_reference.plan.toml` 的 76 条（`var_tensor.*` 33、`var_grad.*` 33、
`var_tensor_read*` 10）在翻转线上全部 `[PASS]`（`-Plan test/plans/feat/linear,test/plans/feat/opt
-Tier fast -Jobs 8`：`[SUMMARY] total: 1548, pass: 1502, fail: 42`，42 条里没有一条属于这一族）；
而 `origin/develop` 与 `handover/h438-tensor-alias` 上
`grep isNumericUnknown src/compile/gir/builder_operators.cpp` = **0 命中**（`isNumericUnknown` 本身两侧
都有，缺的就是这段接管）。两边的用例名集合逐字节相同（各 180 条 `var_*`）。
⇒ **别名落地那一支必须带上 `fdc0813d0` 这段**，否则 76 条在合并后的树上重现。

同一族在翻转线上剩下的红只有一条，且不在接管这一段：`si_var_argument`（`var s = ones([3])` 传
`var s: Tensor<f32, [3]>`）要的 `ArgumentNeedsConversion`（"No conversion applies to argument 1: a var
parameter takes the variable itself"，`src/compile/gir/builder_diagnostics.cpp` 的 `isVar` 分支）现在到不了，
因为 `collectConversionGaps`（`src/compile/gir/builder_helpers.h`）只把 `want->isOtherType()` 当"一次 `as`
可达"，数组拼法之间没有这一支；全层 6 条 `expect_diagnostic_name = "SemanticDiag::ArgumentNeedsConversion"`
的用例（`si_computed_extent_stays_open`、`si_plain_result`、`si_loaded_struct`、`si_computed_slice`、
`unshaped_argument`、`si_var_argument`）同为这一根，属形状/诊断 lane，不在本 lane 改。

## 实测：整翻之后还红在哪（fast 层 5899 例，500 红）

翻的动作本身（`Tensor` = 数值数组、`isTensorType`、`tensorFactsOf`、模块导出与各消费方）已经
全部走通：`stdlib/optim.cml`、`nn.*`、`onnx.*`、`tiles`、`to_bytes`、`show`、入口参数、golden 之外的
单元都在编译期正确解析到数组类型上。剩下的红有三类，**都指向同一件事：秩 0 的表示**。

- **246 例 `Expected Tensor argument`**（`tensorArg` 对非张量实参的那句话）：全部发生在
  `grad(...)` 里或标量损失上。现在秩 0 的**类型**是元素类型（`f32`，裁定：秩 0 就是元素类型本身），
  于是 `sum(loss)` 之类的结果值是**标量槽**，而 `tensor:sum_to`、广播、`divide` 这些算子的
  kernel 仍按「张量 = `Array*`」读参数，`tensorArg` 就拒绝了。翻之前这些值顶着
  `Tensor<f32, []>`（`TensorType`），槽里是秩 0 的 `Array`，所以从不报这句话。
  两个方向都缺一块：入参要能把标量**装箱**成秩 0 数组（`tensorArg`，本 lane 可改），
  出参要能把秩 0 结果**拆箱**成标量槽（kernel 返回处，`modules/tensor/kernels/**` 属别的 lane）。
  只做前者会把响亮的报错变成悄悄读错值（类型说 `f32`，槽里是 `Array*`），所以不能只做一半。
- **11 例 `matmul inner dimensions differ: 2 vs 3` + 6 例 `cannot broadcast [3, 4] with [3]`
  + 6 例 `conversion to 'f32[2, 3]' failed: the array has shape [2]`：多维埃的值只剩第一维。
  **已定位到行**：`src/runtime/draft_session.cpp:248`（`cloneStaticSlot` 的 `Array` 分支）

  ```cpp
  auto *clonedArray = ::Array::create(mm::autoSpace(), arrayType, array->size());
  ...
  for (size_t i = 0; i < array->size(); ++i) { ... }
  ```

  `include/camel/core/rtdata/array.h:272` 把这个重载写成「The rank-1 convenience」：它按 `size`
  造一个**秩 1** 数组，`numel`/`size` 的差别由此而来。多维埃的值经静态槽克隆后就成了秩 1、
  只有 `extents[0]` 个元素。证据（临时探针，`--arg` 传 `x.npy` 与 `w.npy`）：

  ```
  x value: [-0.5, -0.25]          w value: [-0.625, -0.5, -0.375]
  x shape/numel: [2] 2            (同一个进程里) load_npy('x.npy') → [2, 3]
  new([[1,2,3],[4,5,6]]) → [2, 3] literal [2, 3] → 值正确；ones([2, 3]) → [2, 3] 只说明它的
                                  shape 是折叠出来的，值同样被截断
  ```

  `.npy` 文件与 `loadNpy` 正确（`load_npy` 直接调用打 `[2, 3]`），声明的形参类型也不影响
  （写成裸 `Tensor` 一样只剩 `[2]`）。这是 **`src/runtime/**`（别的 lane，本 lane 只读）**里
  翻之前没被走到的一行：以前张量值的类型是 `TensorType`，静态值重编码不走这条；现在它是数组，
  于是所有经过静态槽克隆的多维埃常量/入口参数都被截断。修法在那一行：按 `shapeSpan()` 造，
  并拷 `numel()` 个元素（不是 `size()`）。本 lane 不改，只报。

  **补丁（一行函数，已实测）**：

  ```cpp
  case TypeCode::Array: {
      auto *array     = fromSlot<::Array *>(slot);
      auto *arrayType = static_cast<ArrayType *>(type);
      auto *clonedArray = ::Array::create(mm::autoSpace(), arrayType, array->shapeSpan());
      objectCache.emplace(object, clonedArray);
      for (uint64_t i = 0; i < array->numel(); ++i) { /* 原样保留元素递归 */ }
  ```

  **实测**：在翻的分支上打这个补丁跑 fast 层，5899 例里 500 红 → 483 红（20 例转绿，3 例
  是并行跑的 `poison:` 抖动），主导错误类没变（253 例 `Expected Tensor argument`，即下面的秩 0）。
  也就是说它值 4%，不是「一半的红」。

  **这个缺陷在 `origin/develop` 上不可复现**（三条实测，都在 h374 的基线上）：
  `--arg` 传 `.npy` 给 `Tensor<f32, [2, 3]>` 形参，nvm 与 jit 下都打 `[2, 3] 6`（值正确）；
  `let floats: f32[2, 3] = ones([2, 3])` 在基线上是编译错误
  （`This value has type 'Tensor<float32, [2, 3]>', which is not the declared type 'f32[2, 3]'`）；
  多维埃的普通数组字面量也是编译错误。原因：基线里多维埃的值顶着 `TensorType`，而
  `shouldTraverseStaticSlotType` 对不被 GC 追踪的类型返回 false，静态槽根本不会被克隆；
  `ArrayType` 会被追踪，这条克隆路径是翻之后才第一次走到的。所以「红—绿」这一对只在翻的分支上
  成立（上面的 `[2] numel 2` 对 `[2, 3] 6`），在 h374 的基线上不能作为独立缺陷的守卫。
- **2 处文档**：`docs.links.citations` 说 `docs/design/arrays-memory/plan.cn.md:357` 引用的
  `modules/tensor/type.cpp#TensorType::get` 已经不存在（该文件本 lane 只读，属计划作者）；
  `docs.snippets_compile` 说 `docs/tutorial/tutorial.{cn,en}.md:1033/1061` 还写着旧的
  `tensor([[ 3.], …], dtype=float32)` 打印（打印统一是本次任务的一部分，属于本 lane，但
  正确文本要从实测取，取到时一并改）。

## 实测：本 lane 里已经改掉的坑

- `__iter__`：`for x in xs`（`xs: int[]`）在 `Tensor` 别名之前走的是「数组按元素迭代」的内建路径；
  `Tensor` 变成数值数组之后，数值数组会**先问 `__iter__`**，而 `stdlib/array.cml`、`optim.cml`
  这些不 import tensor 的库里没有 `__iter__`，于是全库报
  `LoopSequenceNotIterable … 'xs' has type 'int[]'`（684 红里的大头）。`builder_control.cpp`
  现在在「数值数组问 `__iter__` 但没有候选可解析」时回落到按元素迭代，非数组类型仍然报错。
- 微分结果的类型：VJP 规则过去直接声明 `Tensor`（现在等于元素类型未定的数组），
  于是 `tangentElementType` 从「元素是 `Void` 的数组」取元素类型，报
  `GradientElementTypeMismatch`（200 例）。现在规则用**原值的类型**声明梯度（`addTensorOperFor`），
  `tangentElementType` 对 `Void` 元素返回 `nullptr`，这一类已经清零。
### 秩 0 的收口（2026-10-24 实测）

`tensorArg` 把标量装箱、`wrap` 把秩 0 的结果取出元素：都在本 lane 的
`modules/tensor/ops/support.cpp` 两个函数里，**内核一行都不用改**——每个内核都经 `wrap`
返回（57 处 `runKernel(`，没有任何一处自己 `toSlot`），所以「结果没有维度就交元素」一处即可。

实测（同一目标、同一机器，先前已带上面的克隆修法）：`Expected Tensor argument` 的出现次数
253 → **0**，500 红 → 489 红（总数 5899 → 5900 是因为多了 `entry_args.tensor_npy_values`）。
**0 个转绿**，6 个原本绿的转红：`library_gemm.tolerance{,.safe,.eight_threads,.safe.eight_threads}`
（脚本断言「程序打印各自的 sum、difference 和 bound」）、`binary_roundtrip` 与
`poison:modules.tensor:binary_roundtrip`（脚本断言「float bits、inference 或 typed function
value 变了」）。六个全是**期望文本**层：秩 0 的值现在按元素打印、秩 0 结果的类型化值现在是
float——正是裁决要的，所以要等期望那一刀跟着改才可能转绿；没有一个崩溃，也没有静默误读。

顺带回答「并行抖动」那一问：`poison:modules.optim:optim.trajectory_matches_native.nvm`、
`poison:modules.tensor.compound_assign:tensor.compound_assign.nvm`、
`poison:modules.tiles:compiled_region_invalidation_and_ownership` 在本分支的**每一次**运行里
都红（带修法、不带修法、带秩 0 收口都一样），不是这次改动抖出来的；这次改动唯一动到的
poison 例是 `poison:modules.tensor:binary_roundtrip`（原因同上，期望文本）。

## 三个 `poison:` 红的判决（2026-10-24，逐条查过）

先说结论：它们**不是抖动**，develop 上都是绿的，所以是翻自己的红；三条各有各的因，只有一条是期望。

1. `poison:modules.tiles:compiled_region_invalidation_and_ownership`（连带它的非 poison 同体）
   ——**期望文本**：`expect_stdout_exact` 里还是旧的张量打印形式
   （`expected "TILES_OUTLINED 2\ntensor([      4.,      16.,      36.], ..."`，实测已是
   `TILES_OUTLINED 2\n[4.0, 16.0, 36.0]`）。属于打印/期望那一刀。

2. `poison:modules.tensor.compound_assign:tensor.compound_assign.nvm`（连同
   `tensor.compound_assign.{nvm,fvm,jit,prl,opt,inline}` 六例）——**真缺陷，但根因是裁决的后果**，
   已单独写成 `docs/handover/questions/compound-assignment-on-a-tensored-array.md`：
   `compoundAssignmentByRebinding` 只对「模块定义的类型」推导复合赋值（注释点名 `+=` on an array
   仍是错误），`Tensor` 变成内建数组类型后 `t += u` 必然掉出这一格，需要作者在「模块声明四个
   assn_* 算子」与「终态就是错误」之间选一条。

3. `poison:modules.optim:optim.trajectory_matches_native.nvm`（连同非 poison 的
   `.nvm/.fvm/.jit/.prl`）——**真缺陷，落点在别的 lane**：运行期报
   `(grad(loss)):?:?: 'the gradient kernels' does not support a value of type 'Tensor<f64>';
   it takes f32 tensors.`，拒绝的代码在 `modules/tensor/kernels/gradient.cpp:34`
   （`refuseTensorElementType("the gradient kernels", t, {TypeCode::Float32})`，内核 lane，本 lane 不动）。
   而用例 `test/cases/modules/optim/agreement.cml` 第 6、38 行是**故意**留一个「形状与元素类型都不写」
   的 f64 叶子（引 `f64-tensor-leaf` 那条裁决），它在 develop 上不走张量的 f32 梯度内核。
   所以这一格要判的是：f64 叶子在翻之后为什么落到了 `grad` 的 f32 内核上——
   是 `stdlib/optim.cml`（另一 lane，只读）的分派变了，还是 autodiff 这一侧的候选变了。
   本 lane 只能报到这一步，改法取决于这一判。

（另：`poison:linear.var_reference:var_tensor.default`、`poison:modules.ddpm_primitives:math.sqrt.runtime.nvm`、
`poison:modules.nn:with_context_step_demo`、`poison:trans.annotated_dump:boundaries.agir` 也是翻自己的红，
尚未逐条分类；它们在我量过的每一次运行里都红。）

## 489 个红的分类（flip7 实测，按每个用例的首个诊断归并）

- **B 内核元素类型拒绝：162** ← 最大的一类，且全是真缺陷（`var_grad.*` 30 例、
  `tensor_grad_check.*`/`nn_grad_check.*`、`grad_closure.*`、`grad_function_values*`、
  `slice_grad.*`、`gelu_grad.bits`、`fusion_group.*`、`autoencoder/tiny_rnn/siamese_*` …），
  文本是 `'the gradient kernels' does not support a value of type '…'`。
- Z 未细分：166（还要再归并一轮）
- A No matching function：49（其中张量复合赋值 7 例；其余是真洞，名单见下）
- E internal error：44；F 旧打印形式期望：**36**；D 返回/声明类型不匹配：15；
  C to_bytes dtype：14；H 梯度元素类型：2；G 不可迭代：1

⇒ **期望那一类只有 36 例，不是主体**；主体是 162 例内核元素类型拒绝，落点在 autodiff/VJP 这条线。
⇒ 翻离绿还很远，期望那一刀不是最后一步。

A 的真洞名单（去掉 7 个复合赋值）：01_scalar_boundary、08_structured_control_flow、
alias.err_var_rank.nvm、alias.type_text.nvm、arms_laid_out_whole_tensor.{default,simplify_nvm,
simplify_fvm,simplify_jit,simplify_tf}、boundaries.{agir,argir,tooltip_slots}、call_mismatch、
declared_numeric_semantics、export_branch、generic.arms_laid_out_whole_tensor、lrit、mnist、
module_import_builtins、native.tensor_{show_wrong_arity,wrong_arity}、nn.{lrit,mnist}、
numeric_contract.{nvm,fvm,jit,prl,generic_unit}、onnx.export_branch、ort_branch、ort_dynamic_batch、
poison:trans.annotated_dump:boundaries.agir、propagated_mismatch、rank_mismatch、
si_computed_extent_stays_open、si_computed_slice、si_loaded_struct、si_plain_result、
si_recursion_leaf、si_var_argument、static_types、unshaped_argument

## 复合赋值：翻之前模块**没有**声明过这四个算子（证据）

`git grep -n "assn" 221cfe18b -- modules/tensor` → **0 命中**；`stdlib` 同样 0；全仓库也没有任何
模块声明 `assn_*`（只有 `src/builtin/executors/builtin.cpp` 的内建标量表）。所以「把模块的四个
声明放回去」没有可放回的东西：开发分支上的 `t += u` 走的是 `compoundAssignmentByRebinding`
对「模块定义的类型」的推导，而 `TensorType` 正是模块定义的类型。

三条路连同代价：
1. （推荐，改动最小）让推导认「模块**声明**了该普通算子」的情形——复合形式跟随模块的 `+`。
   代码里本就有「复合赋值跟随声明该算子的模块」（design principles 5.7）的说法，那条注释说的
   是「模块定义的类型」。代价与风险：普通数值数组会经由张量模块的 `+` 一起获得 `+=`；
   在 P4 终态下这大概正是想要的，但它仍是语义放宽，**需要作者确认**。
2. 让张量模块新声明 `assn_add/sub/mul/div`：新算子表面 + 重绑定语义的实现，代价大得多。
3. 拒绝：6 个用例改成「拒绝」形态，文档同步。

推荐 1；本 lane 没有自行落，因为这是语义裁决，不是漏改的门。

## f64 叶子（optim）为什么落到 f32 梯度内核：bisect 在这条历史上走不通

- 基线 `221cfe18b`：`test/plans/feat/modules/optim.plan.toml` **22/22 全绿**（已实测），
  所以这一格的红是翻带来的。
- 但**中间提交编不过**：在 `a5a32a077`（第一个提交）上构建直接失败（85 s 后 exit 1）。这次是按
  主题拆的提交，类型、消费者与 API 分散在不同提交里，单个提交不是可构建状态。所以「逐个提交
  bisect」不可行；要 bisect 需要一份可构建的 squash 副本，或按文件回退到 tip 之前。
- 因此这一格还差一步才能说出「哪个提交改的」：拿一份可构建的对照（tip 与 tip 去掉 VJP/秩 0 两刀），
  并在 `agreement.cml` 里打出那个叶子的解析类型。没有猜，也没有改内核去迁就 f64——内核只收 f32
  是决定，不该 f64 的叶子要停止是 f64。

## 主体那一类的根因（2026-10-24 一次实测定位）

`'the gradient kernels' does not support a value of type 'Tensor<f64>'` 在 flip7 的日志里出现
**248 次**，`'Tensor<f32>'` **0 次**。所以 162 例内核元素类型拒绝（连同 optim 的 4 例 trajectory、
2 例 GradientElementTypeMismatch，约 168 例）是**同一个根因**：`Tensor` 变成数值数组类型之后，
元素类型没有写出来的张量按**数组/字面量的规则**取到元素类型 `float`（= f64），而张量类型自己的
约定是 **f32**，于是经过它的每一次梯度都被只收 f32 的内核拒绝。落点在本 lane（refiner 给未写元素
的张量取什么元素类型 / VJP 的算子类型），不在内核 lane：内核只收 f32 是决定，不改。

## 文件级回退 bisect（f64 叶子为什么走到 f32 内核）

基线 `221cfe18b`：optim 计划 **22/22 全绿**（实测）⇒ 红是翻带来的。逐个提交不可行（中间提交
编不过，见上）。按文件回退实测两组：

- 秩 0 那一对（`modules/tensor/ops/support.cpp` 回到 bf8d9a40a）：optim **6/22**，trajectory 仍红
  ⇒ **不是它**；而且它承重——去掉它坏得更多。
- `modules/autodiff/pullback.cpp`（回到 221cfe18b）：optim **6/22** ⇒ **不是它**，同样承重
  （它正是让 200 例 GradientElementTypeMismatch 消失的那一刀）。

⇒ 剩下的候选是入口类型（`src/execute/entry_args.cpp`）与 VJP 的算子类型（`vjp.*` 与各 op）。
但 f64 叶子与上面 162 例同源这件事已经可以断定：都是「未写元素的张量现在按 f64 解析」。
