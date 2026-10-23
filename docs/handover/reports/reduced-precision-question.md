# bf16 / f16 的标量地位：四个待裁定的问题

这是一份问题清单，不是裁定，也不是计划。`bf16`/`f16` 已经能作为张量元素类型存储和计算，但语言里没有它们的字面量、标量运算、比较和转换，于是"返回元素类型的归约"在写下运算的地方被按名拒绝。四种缺口可以分别裁定，本文只把每种回答在现有代码里的代价摊开，供语言作者一次性读完。

除本文档外没有改动任何文件；没有新增字面量、算子或"假装问题已定"的用例。文中的 `文件:行号` 对应分支起点 `b43dd5c68`（`origin/develop`）；行号会漂移，紧挨着给出的符号名才是稳定的引用。**运行时证据的限制**：本工作树没有 `out/` 也没有 `node_modules/`，没有可运行的 `camel`，所以第 1.3 节的拒绝文本取自诊断表与测试计划里钉死的期望输出，不是在这里跑出来的。

---

## 1. 事实

### 1.1 已经存在的

**存储格式与两个 C++ 值类型。** `bf16` 是 f32 的高 16 位，`f16` 是 IEEE 754 binary16；两者都是两字节，只有"拓宽精确、收窄正确舍入一次"这一条转换规则（`include/camel/core/rtdata/half.h:4-29`）。`half.h:138-225` 的 `BFloat16`、`Float16` 已经有**完整的 C++ 算术与比较**（`operator+ - * /`、一元 `-`、`== != < <= > >=`），全部"先转 float 再算、构造时舍入一次"（`half.h:164-182`）。也就是说，运行时侧的标量语义已经写好，只是没有接到语言上。`half.h:12-13` 自己写着：

> Both are stored in two bytes and are element types of a tensor only; the language has no bf16 or f16 literal or scalar operation.

**类型系统与数据布局。** `TypeCode::BFloat16` / `Float16` 在 `include/camel/core/type/base.h:90-97`，注释是这段状态的规范表述：

> They are element types only: no value of either fits in a slot, no literal has one, and a conversion from another element type needs `change_precision`.

`src/core/type/pure.cpp:24-27` 把两者登记为纯数据：`{size 2, alignment 2, slotResident = true, slotShaped = false}`；注释说"它不是槽的字节（elementSize 是 2），所以元素访问走慢路径"，`slotShaped=false` 又让"bf16 字段的结构体"不进入批量拷贝（`pure.h:41-62`）。物理上 16 位当然装得进 8 字节的槽：数组元素的慢路径把两字节放进槽的低位（`src/core/rtdata/array.cpp:561-565`），`toSlot`/`fromSlot` 也按"小于槽宽的类型保留低位"工作（`include/camel/core/rtdata/base.h:135-175`）。所以"没有值装得进槽"是**类型系统的一句规定**（`base.h:90-97`），不是表示上的不可能，也不是数据布局在阻止它——这正是问题 (a)(c) 的分界线。

**张量计算。** `modules/tensor/dtype.h:14-19` 声明了唯一的算术路径：

> Every kernel computes a bf16 or f16 element in f32 and rounds it once when it stores it.

具体地：逐元素二元运算走 `half.h` 的 C++ 运算符（`modules/tensor/kernels/elementwise.cpp:281-305`），一元运算显式拓宽到 f32、跑 f32 循环、再舍入一次（同文件 `552-579`），reduced 之间的缓冲转换经 f32（同文件 `245-263`）；矩阵乘在 f32 里累加、结果舍入一次并保持元素类型（`modules/tensor/kernels/gemm.cpp:324-339`）；轴归约用 **double** 累加、存回元素类型时舍入一次（`modules/tensor/kernels/reduce.cpp:150-189`），最小值/最大值拓宽后比较、结果就是两个操作数之一，不发生舍入（`:197-202`，`modules/tensor/kernels/minmax.h:40-71`），全张量 `sum` 用 double 累加并返回 `float`（`reduce.cpp:229-248`、`modules/tensor/ops/reduction_ops.cpp:87-107`）。注意累加精度**不是统一的**：归约是 double（轴归约与全归约都是），矩阵乘是 f32。

**转换。** 唯一的入口是张量算子 `cast`（`modules/tensor/ops/elementwise_ops.cpp:228-252`）；写入 `bf16`/`f16` 需要 `change_precision`，编译器在写下的调用处检查：张量模块声明识别函数（`modules/tensor/module.cpp:75-112`），编译器在权限写入节点后询问它（`src/compile/gir/builder_operators.cpp:2512-2532`），契约写在 `include/camel/core/operator_table.h:406-435`（"Only the owning module knows what a `Tensor<dtype>` is"）。拒绝文本是 `SemanticDiag::PrecisionChangeNotPermitted`（`src/core/error/diagnostics/messages/semantic.cpp:1415-1422`）。读出到 f32/f64 是精确的、不需要许可（同文件 `1420-1421`、`1428`）。`cast` 的目标类型必须在调用处写死，否则拒绝（`elementwise_ops.cpp:239-248`）。

**谓词。** `stdlib/types.cml:42-46` 的 `is_float` 列出 `bf16`/`f16`，`is_numeric`（`:67-69`）与 `is_ordered`（`:74-76`）从它继承；`test/cases/modules/stdlib/reduced_predicates.cml` 钉住这一点。结果是：`sum` 的 `when is_numeric::<T>()` 与 `min`/`max` 的 `when is_ordered::<T>()` 现在**通过**，拒绝发生在函数体里写下运算的地方，而不是被 `WhenConditionUnmet` 悄悄排除。

### 1.2 不存在的

- **字面量。** 数字记号没有类型后缀（`docs/language/02_lexical.md:57-106`；唯一的后缀 `i` 是虚数）。字面量按上下文取类型，而对 `bf16`/`f16` 的物化路径根本不存在：`src/compile/gct/builder.cpp:4103-4117` 的 `declaredValue` 只给 `Int32`、`Int64`、`Float32`、`Float64` 和复数做"按声明类型精确转换"，`src/compile/literal.cpp:929-989` 的 `Value::toData` 同样只认这些；`include/camel/core/data/primary.h:19-39` 的 `PrimaryData<T>` 对其它 `T` 直接编译期断言。于是 `let x: bf16 = 0.1` 走到 `convertTo` 返回空、由 `reportLiteralCastFailure` 报 `LiteralNotRepresentable`（`builder.cpp:4173-4177`、`:365-394`）；而 `x as bf16` 这类真正的 CAST 走 `Type::castSafetyFrom`，对 reduced 来源/目标是 `Forbidden`（`src/core/type/base.cpp:285-299`），用户看到 `DynamicCastForbidden`（`src/compile/gir/builder_expr.cpp:1031-1053`）。字面量的范围/下溢文本也没有 reduced 分支（`literal.cpp:1257-1278`、`:1007-1018`）。
- **标量运算与比较。** 全局算子组的每个类型重载在 `src/builtin/modules/globals.cpp:1169-1222`（`__add__` 等）逐条列出 `i32`、`int`、`f32`、`float`、`string`、`c64`、`c128`；没有 `bf16`/`f16`。`math::min` / `math::max` 的固定签名也只有 `i32`、`int`、`f32`、`float`、`string`、`bool`（`stdlib/math.cml:283-313`）。字节码的内联算子表同样只列这些类型（`src/passes/sched/fastvm/compile.cpp:77-118`）。
- **比较的运行时退路也被关掉了**：`sort` / `argsort` / `merge_sorted` 在运行时按元素类型拒绝，支持集写死为四种（`src/builtin/operators/array.cpp:96-101`、`:318`、`:393`；用例 `test/cases/modules/stdlib/reduced_sort_refused.cml`）。
- **转换。** `stdlib/signatures/globals.cml:32-120` 的 `int`/`i32`/`float`/`f32` 等转换函数没有 `bf16`/`f16` 重载；标量转换被 `base.cpp:294-298` 明确禁止，注释说得很直白："a bf16 or f16 is stored in a compact tensor buffer, never in a slot, and a conversion between them and a scalar is not a scalar cast: it is a tensor operator that needs `change_precision`."
- **打印。** 一个 bf16 标量若真的存在，`printScalar` 的 `switch` 没有它的分支，会落到 `default: os << std::format("<{}>", type->toString())`（`src/core/rtdata/print.cpp:82-117`，默认分支在 `114-116`）；`println` 对未知类型走这条路（`src/builtin/operators/io.cpp:103-107`）。即打印出的是 `<bf16>`，不是数字。

一个推论，不是直接引用：**按现有语言，非空的 `bf16[]` / `f16[]` 没有构造路径**。字面量、转换、追加都要求先有一个元素值，而元素值只能来自字面量或转换；现有用例里这两个元素类型的数组都只写作空数组（`reduced_predicates.cml:25-26`、`reduced_sum_refused.cml:17`）。因此今天那个"归约"拒绝拦住的其实是**空数组上的泛型实例化**，不是数据。

### 1.3 用户今天遇到的拒绝

最小用例（仓库里的真实用例，`test/cases/modules/stdlib/reduced_sum_refused.cml:12-20`）：

```camel
module stdlib_reduced_sum_refused

import { sum } from array

func main(): int sync {
    let xs: bf16[] = []
    println(sum(xs))
    return 0
}
```

测试计划把它的退出码钉为 1（`expect_exit = 1`），诊断是 `SemanticDiag::ReducedPrecisionUnsupported`（`0x030001D7`）。消息模板（`src/core/error/diagnostics/messages/semantic.cpp:1424-1429`，示例页 `docs/diagnostics.md:6237-6265`）：

> The call of '{0}' cannot take a value of the reduced-precision element type '{1}'.

代入后用户看到的正文：

```text
The call of 'operator +' cannot take a value of the reduced-precision element type 'bf16'.
```

同一处还会带一段定位说明，测试计划要求输出包含这两段（`test/plans/feat/modules/stdlib_reduced.plan.toml:30-32`）：

```text
in the instance 'sum<bf16>'
```

同一诊断的帮助文本是：

> bf16 and f16 are stored element types: the language has no bf16 or f16 literal, scalar operator or conversion, so a function that would add, compare or otherwise use one cannot compute with it. A bf16 or f16 tensor is computed by the tensor module, which widens every element to f32 and rounds once when it stores the result (design principles 7.3, H238); cast reads it back into f32 or f64 exactly. Write the reduction, comparison or arithmetic on the tensor, or on a value of a type that has the operation.

`sum` 的实例被拒在 `+` 上（`builder_operators.cpp` 的算子路径，`src/compile/gir/builder_operators.cpp:2248-2259`）；`min`/`max` 走的是函数调用路径（`stdlib/array.cml:170-179` 的 `reduce<math::min, xs[0]>`），同一个诊断在调用点 `{0}` 处报的是被调者的图名（`builder_operators.cpp:2108-2117` 用 `displayNameOf(graphs->front()->name())`，即 `math::min`/`math::max` 那个图的名字；具体拼写没有用例钉住）。两条路径共用 `reducedPrecisionArgumentOf`（`builder_operators.cpp:1000-1031`），它的注释把理由写在代码里：

> A bf16 or f16 scalar or array has no operation in the language: there is no literal, no operator and no conversion for one, so a call that needs to add, compare or otherwise use it is reported as such instead of as "no matching function".

**怎么复现**（在有构建的树上）：`camel test/cases/modules/stdlib/reduced_sum_refused.cml`，或 `node scripts/test.js test/plans/feat/modules/stdlib_reduced.plan.toml`。

---

## 2. 四个问题

它们**可以分别裁定**：每个问题的"是/否"不蕴含另一个的答案。实现上不独立，本文在每处写明：例如 (d) 若想用定义图实现，就绕不开 (a) 或 (b)/(c)——这是代价，不是裁定。

### 2.1 (a) 可以有字面量吗；它表示"按 f32 舍入的值"还是"精确的半精度值"

**问题。** `let x: bf16 = 0.1` 合法吗？如果合法，`0.1` 是"离写下的十进制数最近的 bf16"（与 `f32`/`float` 字面量的现行规则一致：`docs/language/02_lexical.md:80`、设计原则 5.1），还是"只接受能精确表示的值"（因此 `0.1` 被拒），还是"先取 f32 最近的数、再取 bf16"？

**一个先要说明的事实。** 就 `+ - * /` 与转换而言，"十进制→f32→bf16"与"十进制→bf16"**逐位相同**：训练设计已经写下这个定理及其条件（`docs/design/training/README.cn.md:278`，24 ≥ 2×8+2，f16 是 24 ≥ 2×11+2）。所以"f32 舍入"与"直接舍入"的选择在正常值上不是数值问题，而是**叙事一致性**问题：字面量是否与张量的计算精度用同一条规则。真正的数值分歧只在"允不允许舍入"这一层：允许，`0.1` 有值；只接受精确值，`0.1` 被拒，而全语言只有这一个浮点类型这么做。次正规与溢出的边界是否也逐位相同没有验证（见 §5）。

**选项 a1：字面量存在，从写下的十进制数正确舍入一次。** 代价：

- `literal::Value` 管线：`toData` 新分支（`src/compile/literal.cpp:929-989`）、`underflows`/`rangeText` 新分支（`:1007-1018`、`:1257-1278`）；f16 的范围文本要新写（最大 65504、最小次正规 2^-24），bf16 借用 f32 的范围但要写自己的下溢边界（2^-133）。
- 一个新的数据值类型：`PrimaryData<T>` 对 `BFloat16` 的 `type_`/`isZero`/`toString`/`equals` 分支（`include/camel/core/data/primary.h:19-93`）与 `convertTo`（同文件 `243-278`）。
- 字面量物化入口：`declaredValue` 的允许类型表（`src/compile/gct/builder.cpp:4108-4117`）加两项。
- 槽规定：字面量的物化要么让 `Value::toData`/`convertTo` 放行，要么给字面量一条专用路径；`castSafetyFrom` 对显式 `as` 要么放行，要么另开一条（`src/core/type/base.cpp:285-305`）；`base.h:90-97` 那句"no value of either fits in a slot"必须改写（物理上已经装得下，`array.cpp:561-565`；`pure.cpp:24-27` 也已经把它当纯数据，只是 `slotShaped=false`）。数据布局本身不必改。
- 打印/格式化：`printScalar` 的 `bf16`/`f16` 分支与浮点文本规则（`src/core/rtdata/print.cpp:82-117`、`docs/language/02_lexical.md:81-93`）；否则 `println` 打印 `<bf16>`。
- 文档一致性：`types.cml:17-22`、`half.h:12-13`、诊断帮助（`semantic.cpp:1428`）都写着"没有字面量"。
- 会坏掉的东西：没有直接坏掉的语义，因为今天没有代码能构造非空 reduced 数组；但 `docs/diagnostics.md:6237-6265` 与 `reduced_sum_refused` 的说明文字（"no literal"）会变成假话，必须同批改；`castSafetyFrom` 一旦放行，还要检查 `builder.cpp:2984`、`builder_expr.cpp:1034` 这些依赖 `Safe` 的地方的行为。

**选项 a2：只接受能精确表示的值（"exact half"）。** 代价比 a1 **更高**：a1 的全部表示与打印工作照做，另加一条"是否为二进制有理数且落在格式内"的判定与新的拒绝文本，并且与 5.1/02_lexical.md 对 `f32`/`float` 的规则冲突（它们接受 `0.1`）。结果是一个几乎无用的字面量：只有 `0.5`、`0.25`、`1.0` 这类值可写，其它十进制数全被拒。若作者选它，需要同时说明为什么 bf16 与其它浮点类型不同。

**选项 a3：不引入字面量（今天）。** 零代码。后果：reduced 标量始终不可被程序写出，数组始终只能为空，(d) 的"返回元素类型"也拿不到有内容的输入。

**未知（a）**：bf16 的 DATA 节点能否走通帧/静态区/JIT。要试的是：临时允许 `declaredValue` 与数据值类型，写 `let x: bf16 = 1.0` 与 `let y: bf16 = 0.1`，在 `std::nvm`/`std::fvm`/`std::jit`/`std::prl` 上跑 `println(x)` 并存进数组再读回，看四个后端是否一致；用 `std::gir` 看 DATA 节点的类型与值。

### 2.2 (b) 可以有标量 `+ - * /` 与比较吗；如果有，规则是"f32 里算、舍入一次"吗——7.3 说的是"存储时舍入一次"，一次标量运算算不算一次存储

**问题。** 两类要分开：算术（`+ - * /`、一元 `-`）会产生新的舍入；比较（`< <= > >= == !=`）只判断、不产生新值，不涉及舍入。可以只答其中一类。若算术为"是"，"f32 里算、舍入一次"是否是规范？7.3 的"一次存储"针对的是写进元素类型，标量运算写回标量算不算同一个"存储"？

**已有先例把它答成"不需要许可"**：张量模块的精度门只认"改变元素类型的转换"，明确写着"An operator that keeps an element type it already has (`reshape`, `sum`, `add` of two bf16 operands) converts nothing"（`modules/tensor/module.cpp:75-83`）。`dtype.h:14-19` 的"一个算术路径"就是按这个读法工作的。**但 7.2 的原文把"提高累加精度"也算进 `change_precision`**（`docs/design-principles.cn.md:436`），而 bf16 在 f32 里算正是"提高精度"。于是二选一：要么承认"元素类型自身的规范算术路径"不算许可（张量模块现在的读法），要么承认张量模块现在一直在无许可地做需要许可的事。这个选择不做，标量运算的答案就悬空；它也是本文认为**最承重的一条先例**（见 §3.1）。

**选项 b1：算术与比较都有，按"f32 里算、舍入一次"，不要许可。** 代价：

- 注册：逐类型重载要进 `__add__` 等组（`src/builtin/modules/globals.cpp:1169-1222`），或由一个模块以同名组补充（张量模块就是这么加自己的二元算子的：`modules/tensor/ops/elementwise_ops.cpp:485-500`、`modules/tensor/ops/registry.cpp:245-268`；跨模块算子重载是否无歧义合并，见 §5）。
- 实现：`half.h` 的 C++ 运算符已经存在，包一层 `slot_t` 的 `get`/`set` 即可（参考 `src/builtin/operators/ops.cpp:552-556` 的 `f32` 版）。
- 各后端：`std::fvm` 对任何已注册的算子有通用 `OPER` 退路（`src/passes/sched/fastvm/compile.cpp:545-567`），所以**不必**加字节码；但所有按 URI 逐类型处理的翻译/优化都要认识新后缀：C++ 翻译 `src/passes/trans/cpp/operator_rules.cpp:259-284`、ONNX `modules/onnx/lowering.cpp:1181`、tiles `modules/tiles/src/compiler.cpp:750`、rewrite `modules/rewrite/analysis.cpp:214` 与 `module.cpp:1258`。这些地方多半会"不认识就拒绝/不匹配"，是**功能缺口**而不是错误值，但要让 `std::cpp` 与 `onnx::export` 支持 bf16 就得逐个加。
- 自动微分：`bf16`/`f16` 没有切空间（`src/builtin/operators/derivative.cpp:415-416` 只登记了 `Float32`/`Float64`），也没有导数规则（同文件 `32-43` 的 `add_f` 等）。9.9 要求"没有梯度通道的连续值不得悄悄丢梯度，一律带位置拒绝"，所以要么给 reduced 类型登记切空间与规则，要么在 `grad` 处按名拒绝；两者都是新增工作。
- 归约仍然不工作：`sum` 的初始值 `0` 要先成为 bf16（`stdlib/array.cml:164`），这需要 (a)，或者把 `sum` 换成原生实现。`min`/`max` 需要 `math::min`/`max` 的 bf16 重载（`stdlib/math.cml:283-313`），光有 `<` 不够。
- 结果仍然不可用：除了再比较一次，结果没有别的用途——打印、转成文本、转换都还要 (a)/(c) 或打印分支（§1.2）。

**选项 b2：只加比较。** 代价比 b1 小：不产生舍入，没有 `change_precision` 的争议；语义可以直接照 `half.h:177-182`（拓宽后比较，NaN 与 ±0 的行为继承 f32 的 IEEE 规则）。仍需注册、仍需各翻译/优化认识它。自动微分处要分开看：比较的结果是 `bool`，按 9.9 属于离散值、导数为零，本身不是问题；但一个 `bf16` 值若出现在被求导函数里，它没有切空间，9.9 要求按名拒绝（见 §3.2）。它让 `is_ordered::<bf16>()` 至少在类型上有了对应的运算（尽管还没有值可比较），但**不**解锁 `min`/`max`（它们是函数重载，不是 `<`），也不解锁 `sort`（`array.cpp:96-101` 是按元素类型分派的，要另加分支）。

**选项 b3：算术存在，但要求 `change_precision`。** 代价比 b1 更大：每个 bf16 算式都要带标签，`#strict` 会撤销它（设计原则 7.4），于是严格模式下 bf16 完全不能计算；精度门目前只认 `tensor:cast`（`module.cpp:86-90`），要教它认标量 URI；张量算术要么同样加许可（改变现有行为），要么被声明为例外——那等于承认 7.2 的"提高精度"不适用于元素类型自身，回到 b1 的读法。

**未知（b）**：JIT 是否与 fvm 一样有通用算子退路（要试一个模块注册的 bf16 算子在 `std::jit` 上）；`std::cpp` 与 `onnx::export` 会拒绝还是能退回调用；一个模块级的 `__add__` 重载与核心 `__add__` 在核心调用点上是否歧义（两个模块的同名算子组怎样合并，见 §5 第 2 条）。这些都要先在临时分支上注册一个 bf16 算子再跑四个后端与两个翻译遍。

### 2.3 (c) 可以有转换吗；它是 `change_precision`（一个许可）还是精确的

**问题。** `bf16(x)`、`f16(x)`、`x as bf16` 可以存在吗？如果存在，收窄是否复用张量 `cast` 的许可规则，还是只允许精确（不改变值）的转换？

**事实。** 今天唯一的转换是张量算子 `cast`，写入 reduced 需要许可并逐调用点检查（`module.cpp:75-112`、`builder_operators.cpp:2512-2532`）；读出到 f32/f64 精确、不需许可（`semantic.cpp:1420-1421`）；标量 cast 被明确排除在转换矩阵之外（`base.cpp:285-299`）。所以"精确的转换"这个选项在标量上只剩**拓宽**，而拓宽今天也只以"张量 cast"的形式存在——一个 reduced 标量根本还没有。

**选项 c1：标量收窄存在，沿用 `change_precision`。** 代价：`castSafetyFrom` 要放行（或标量转换不走它，另设一条转换路径，`base.cpp:264-305`）；转换实现（`src/builtin/operators/cast.cpp` 的槽实现、`include/camel/core/data/primary.h:243-278` 的 `convertTo`）；转换函数重载与解析表（`stdlib/signatures/globals.cml:32-120`、`src/builtin/modules/globals.cpp:95-240`）；精度门要认新的 URI（`module.cpp:88`）；打印（同 (a)）；`x as bf16` 还要过 `builder_expr.cpp:1031-1053` 的 CAST 检查。数值上，f64→bf16 走 f32 与直接走，按 §2.1 引的定理逐位相同，但要在 f16 次正规边界上验证。若作者采纳训练设计的读法（显式转换不需要许可，§3.1），这一条就变成"放行 + 撤掉这道门"，比沿用许可少改精度门，却要让张量 `cast` 的现有行为和用例一起改。

**选项 c2：只允许精确（不改变值）的转换。** 这几乎不是一条实现，而是一条规则：只有拓宽，而从 bf16 到 f32/f64 的拓宽已经由张量 `cast` 提供，标量侧只需要让一个 bf16 标量能被读出来——可这个标量今天不存在。若选 c2 且没有 (a)，`bf16(x)` 没有可接受的源类型（f32/f64→bf16 永远可能舍入），所以 c2 实际上等于"没有标量转换"。它的成本就是明确写下这条规则，并在帮助文本里说清"读入更宽类型不需要许可，写入 reduced 只能通过张量 cast"。

**选项 c3：没有标量转换（今天）。** 零代码；bf16 仍只是张量元素类型。

**未知（c）**：张量索引 `take`/切片是否可能产出一个 bf16 标量（它返回 `Tensor`，所以大概不能；要试）；一个两字节的值在 8 字节帧槽里"低位保留、高位清零"的约定是否在所有后端一致（`toSlot`/`fromSlot` 说明有约定，要试跨函数传入传出）。

### 2.4 (d) 对这类数组的归约可以返回元素类型吗；用什么累加

**问题。** `sum::<bf16>(xs: bf16[]): bf16`、`min/max: bf16?` 可以成立吗？如果成立，累加器是什么？

**事实与两种既有答案。** 数组的泛型体是 `reduce<(a, b) => a + b, 0>`，返回 `T`，空数组返回 0（`stdlib/array.cml:163-165`）；`min`/`max` 用 `math::min`/`max`、返回 `T?`（`:170-179`）。张量模块给出的先例**不唯一**：轴 `sum` 保持元素类型、double 累加、存回时舍入一次（`reduce.cpp:150-189`），全 `sum` 返回 f64、double 累加（`:229-248`），矩阵乘保持元素类型、**f32** 累加（`gemm.cpp:329-339`）。"什么累加"今天就有两个答案，作者要选一个并说明为什么归约不是矩阵乘。

**选项 d1：返回元素类型，泛型体照旧。** 需要 (a) 的 bf16 零字面量（空数组与初始值）、(b) 的 `+`（`min`/`max` 还要 `math::min/max` 的 bf16 重载）。累加器会是**元素类型本身**：每加一次舍入一次，与张量归约的 double 累加不同。这个差别是可观测的，而且仓库已经用一个测试证明了它重要——`2^24 + 1 - 2^24` 在 double 累加下得 1，在 f32 累加下得 0（`test/plans/feat/modules/stdlib_reduced.plan.toml:40`）。要用 double 累加，泛型体表达不了，得改成原生归约或引入转换；另外返回值是一个除了"被返回"什么都不能做的值（打印、比较、转换都缺，§1.2）。

**选项 d2：返回更宽的类型（f32 或 f64），double 累加。** 不需要字面量、不需要标量算子、不需要转换、不需要改槽；读元素用 `Array::getAsDouble`，它已经处理这两种元素类型（`src/core/rtdata/array.cpp:645-665`）。但"给 `sum` 加一个专门重载"这条路被语言规则堵死了：**泛型函数不能与其他函数同名**（`docs/language/07_statements.md:196`），所以不能只写一个 `sum(xs: bf16[]): float` 挂在现有泛型旁边。只剩两条路，各有代价：

- **换名字**（例如 `sum_wide`）：一条声明 + 一个原生内核 + 一个用例，约三个文件，不改任何现有签名。代价是 `sum<bf16>` 仍然被按名拒绝，(d) 的问题在 `sum` 上依旧悬着，只是用户有了一个绕开的入口；这更像"临时出口"，不是对 (d) 的回答。
- **改 `sum` 本身**：把泛型 `sum::<T>(xs: T[]): T` 改成"无体声明 + 按元素类型的执行器"（`math:abs` 的形态，见 `docs/handover/README.cn.md:255`），或者拆成逐类型固定重载。前者要重写 `sum`/`min`/`max` 的声明与三个内核并重新钉全部数值用例，后者要放弃"一条泛型服务所有数值类型"的现状；`sum` 的契约"返回 T"（`array.cml:163`）也要改，`docs/stdlib-reference` 与用例跟着改。这是**接口语义**的变化，不只是实现。

两种 d2 都要回答一件事：`bf16[]`/`f16[]` 今天只能是空数组（§1.2），所以返回 f32 的归约在有 (a) 或 (c) 之前只对空数组有意义。这也是为什么本文把"四个否"列为代价最低的选项，而不是 d2。

**选项 d3：不返回元素类型，保持拒绝（今天）。** 零代码；`sum<bf16>` / `min<bf16>` 继续按 §1.3 的文本拒绝。需要记录两条不一致：(i) `is_ordered::<bf16>()` 为真、`min`/`max` 的守卫通过，但没有任何手段表达顺序，`sort` 在运行时拒绝（`array.cpp:96-101`）；(ii) `is_numeric::<bf16>()` 为真，泛型数值库接受这个类型后在内部拒绝——这正是 H238 想要的"在写下运算处拒绝"，但它意味着谓词的含义是"这是一个数"，不是"这个类型有这些运算"。如果选 d3，建议把这句话写进 `types.cml` 的注释，免得下一个人把它当 bug。

**未知（d）**：能否在**不**先有 (a)/(b)/(c) 的情况下用定义图写这个归约。想走"定义 + 原生内核"（7.1、10.4）就得在定义图里用 `+`/转换，绕回 (b)/(c)；直接写原生内核则违反 10.4 对"只有 I/O、布局、标量数学可以是原子算子"的限定。要试的是：把归约写成专用原生算子，看 `--verify-ops` 与语义一致性检查是否接受；以及 `Array::getAsDouble` 在 nvm/fvm/jit/prl 上对 bf16 元素的结果是否一致。

---

## 3. 适用的先例

### 3.1 7.3 的"存储时舍入一次"与 7.2 的"提高精度也算许可"（最承重）

> 默认放宽适用于张量逐元素运算，与标量相同。（`docs/design-principles.cn.md:452`）

这句话本身已经承认"标量逐元素运算"是一个存在的类别；bf16 的缺口是标量侧没有这一类。而 7.2 的许可是"`change_precision`（降低输入或累加的精度，**提高累加精度也属于此项**）"（`:436`），7.3 的表格把它写成"以较低精度（TF32、BF16 等）计算"（`:448`），并说"BF16 计算只需 `change_precision`"（`:454`）。张量模块的现行读法是：**元素类型保持不变时不算精度改变，只有改变元素类型的 `cast` 才需要许可**（`module.cpp:75-83`）。就本文所查，这是唯一一处把 7.2 与 7.3 落到代码里的解释，因此它是最承重的先例：任何 (b)/(c) 的答案都要么继承它，要么宣布张量现有行为需要返工。

这里还有一处文档与代码的冲突，作者裁定时要顺带定掉：训练设计说显式转换"是程序可见的语义，不需要数值许可"（`docs/design/training/README.cn.md:274`，写法是 `x as Tensor<bf16>`），而实现要求 `change_precision`（`modules/tensor/module.cpp:75-112`、`semantic.cpp:1415-1422`）。两者不能同时为真；选哪一边决定了 (c) 的答案。"提高精度"是否适用于"元素类型的规范算术路径"也应在裁定里明写。

### 3.2 9.9 的离散导数规则

> 任何依赖被求导参数、却没有梯度通道的连续值，都不得悄悄丢掉梯度，一律带位置拒绝。离散的值（整数、布尔）对参数的导数按定义为零。（`docs/design-principles.cn.md:563`）

bf16 是连续浮点，却没有切空间（`derivative.cpp:415-416`）。所以 9.9 不允许"让它过去然后梯度为零"或"悄悄丢掉"：要么登记切空间与规则，要么在 `grad` 处按名拒绝。它也顺带提出一个作者要回答的小问题：bf16 算"连续值"还是像整数/布尔那样"离散"？今天两种都不成立，因为没有运算可以出现在被求导函数里。

### 3.3 "声明，内核履行"

> **定义图是规范**：融合或手写的实现必须与算子的定义图一致，故障也一致。（`:431`）
>
> C++ 算子经同一套标签登记接口声明，Camel 写的函数由函数体自动推出。（`:420`）
>
> **语义原子与实现原子分开。** 一个函数的**语义**只有两种来源：原子算子，或用 Camel 写的定义；它的**执行**可以另有原生内核。……**其余一切用 Camel 定义**（默认）……**常用而热的定义带原生内核**：Camel 函数体仍是语义，`#impl<f>` 标出实现它的原生内核……差分测试保证两者结果一致。（`:603-606`）

bf16 的现成范例不是某个算子，而是精度门：核心对 bf16 一无所知，张量模块声明 `narrowingIntoReducedType`（`module.cpp:75-112`），建图器在写完节点权限后询问它（`builder_operators.cpp:2512-2532`），契约明说"只有拥有模块知道 `Tensor<dtype>` 是什么"（`operator_table.h:416-425`）。如果作者对 (a)–(c) 答"是"，这条先例说：规则应由拥有元素类型的模块声明（例如"标量运算的语义与许可由 tensor 模块登记"），而不是把 bf16 的知识写进核心；同时每个实现都必须与定义图逐位一致，包括故障。

### 3.4 常量折叠：如果算子存在，`std::opt::fold` 会把它折掉

`std::opt::fold` 对纯算子、参数全为常量时，调用的是**注册的实现本身**（`src/passes/opt/generic/generic.cpp:633-647` 的 `(*op)(*with, *norm, ctx)`），再把结果物化成静态值（`:673`）。7.4 说常量折叠仍按规范算法（`:461`），所以：

- **不是正确性问题**：折叠调用的是运行时同一个实现，不会另造一个更宽的累加器；而"f32 中间值 vs 精确值再舍入一次"在 `+ - * /` 上按训练设计的定理逐位相同（`training/README.cn.md:278`），折叠与运行不会给出不同的位。
- **是一个约束**：注册的那个实现就此成为折叠与运行两条路径共同的规范。任何第二个实现（例如用 double 累加的 Camel 定义、或 `pow`/`sqrt` 用了不同中间精度）必须与它逐位一致，否则"常量表达式"与"变量表达式"结果不同。7.1 本来就要求这一点，折叠只是把它变成立刻可见的常量。
- **是一个缺口，不是错误**：如果算子只有纯 Camel 定义、没有原生内核，折叠时 `ctx.execMgr().find(uri)` 找不到实现（`generic.cpp:636-639`），于是跳过——值没错，但"是否为编译期常量"与原生算子不同，会影响 `when` 条件与静态实参。作者应说明哪条路径是规范的。
- **许可不会被折叠绕过**：精度门在建节点时就检查了（`builder_operators.cpp:2517-2532`），折叠不重新检查。所以"标量运算不要许可"的裁定下折叠安全；"要许可"的裁定下门仍然有效。唯一要试的是：实现会随许可/属性选择内核的算子（如张量的库 GEMM 路径）被折叠时，折叠是否尊重那个选择——见 §5。

---

## 4. 如果四个答案都是"否"

这是一个自洽的选择，代价是零行代码，但要在记录里写下它留下什么。

**继续被拒绝的（都是按名的）：**

- bf16/f16 标量上的 `+ - * /`、一元 `-`、`< <= > >= == !=`、`math::min/max/clamp/abs/sqrt/pow` 等；
- bf16/f16 字面量与 `bf16(x)`/`f16(x)`/`x as bf16`；
- `sum` / `min` / `max` 对 `bf16[]`/`f16[]`（§1.3 的文本与用例）；
- `sort` / `argsort` / `merge_sorted` 对这两种数组（运行时 `RuntimeDiag::OperandTypeUnsupported`，`array.cpp:96-101`，用例 `reduced_sort_refused`）；
- 非空的 `bf16[]` / `f16[]` 无法构造（§1.2 的推论）。

**继续可用的：**

- `Tensor<bf16>`/`Tensor<f16>` 的创建（`cast` + `change_precision`，或 `from_bytes::<bf16>` 直接读位、不舍入：`modules/tensor/module.cpp:81-82`）与读出（`cast` 到 f32/f64，精确、无许可）；
- 张量上的逐元素算术、比较、归约、矩阵乘，全部"f32 里算、存回时舍入一次"，归约 double 累加；`to_bytes`/`save_npy`/`show` 等张量级 I/O；
- `is_float`/`is_numeric`/`is_ordered` 对两种类型回答真。

**需要一并记录的两条不一致（不是缺陷，是语义选择的后果）：**

1. `is_ordered::<bf16>()` 为真，`min`/`max` 的守卫因此通过，但语言里没有任何顺序可用，`sort` 到了运行时才拒绝。要么接受"谓词只说类型属于数/有序族，不说运算存在"，要么在谓词注释里说清。
2. `is_numeric::<bf16>()` 为真，于是所有 `when is_numeric::<T>()` 的泛型函数都会实例化到它、然后在函数体里按名拒绝。这是 H238 有意为之（"在写下运算处拒绝"），但它使"谓词为真"不再等于"可以计算"。

如果选"全否"，唯一值得做的后续是把五处措辞对齐（`base.h:90-97`、`half.h:12-13`、`types.cml:17-22`、诊断帮助 `semantic.cpp:1428`、训练设计 §6.1–6.2 `training/README.cn.md:272-280`），因为它们今天用不同的句子说同一件事。

**按代码面把选项排一次**（本文不推荐任何一个，只给作者一个量级）：

1. 零行：四个"否"。
2. 最小的"是"：**(b) 只加比较**。语义在 `half.h:177-182` 已经写好，需要的是核心的一个算子组（`globals.cpp:1169-1222` 旁）加一组内核；不碰存储、字面量、转换、槽。代价是它不解锁任何归约，而且今天没有值可比较。
3. 次小、且真正改变用户能写什么的入口：**(a) 或 (c)** 之一。两者都要动"reduced 值占不占槽"这条规定与打印，所以都比 2 贵；但只有它们能产生值，其余三个问题的"是"都以此为前提。
4. 更大：**(b) 带许可**——每个算式都带标签、精度门要认标量 URI、张量路径也要一致或声明例外；以及 **(d) 返回元素类型**——需要 (a) 的零与 (b) 的 `+`（`min`/`max` 还要 `math` 的 bf16 重载），外加一个与张量归约一致的累加器决策。

---

## 5. 定价不了的部分（未知，以及要试什么）

1. **bf16 标量能否穿过帧、静态区、JIT。** 要试：短接两处门（`base.cpp:294-298`、`builder.cpp:4114-4117`）后写 `let x: bf16 = 1.0`、跨函数传参、存进结构体，在 nvm/fvm/jit/prl 上跑并比对；用 `std::gir` 看 DATA 节点与帧布局。
2. **模块级 `__add__` 重载与核心组的合并是否歧义。** 4.3 只说"用户的重载比内建的更具体时优先"、命名空间块里声明的运算符属于模块（`docs/design-principles.cn.md:250`）；两个模块各自声明的同名算子组在核心调用点上如何合并，本文没有找到明文。`globals.cpp` 的组与张量模块导出的同名组是两条注册路径（`registry.cpp:245-268`）；要试一个真实调用点。
3. **常量折叠是否尊重"实现随许可选择"的算子。** 要试：带 `#strict` 与不带，对同一个张量 GEMM 常量表达式跑 `std::opt::fold`，比较字节；看折叠有没有读到节点的权限。
4. **`std::cpp` 与 ONNX 对 reduced 标量算子的态度。** ONNX 的 lower 按 `":op/add_" + t` 逐类型查表（`lowering.cpp:1181`），未知后缀大概直接拒绝；要试 `onnx::export` 与 `std::cpp`。
5. **`Tensor<bf16>` 能否索引/切片出标量。** 张量的 `take`/切片返回 `Tensor`，不是标量；要试是否有别的路径。
6. **f16 次正规边界上的双重舍入。** §2.1 的定理覆盖正常值；要试一组恰好落在半个最小次正规附近的十进制文本，与参考实现比对。
7. **运行证据。** 本工作树没有构建（无 `out/`、无 `node_modules/`），§1.3 的文本来自诊断表与计划里钉死的期望；若作者要一次实跑，请在已构建的树上跑 `node scripts/test.js test/plans/feat/modules/stdlib_reduced.plan.toml`，或直接 `camel test/cases/modules/stdlib/reduced_sum_refused.cml`。