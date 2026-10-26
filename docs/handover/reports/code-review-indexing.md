# 核心切片、按位置取行与元素类型规则的代码评审

范围：核心的 `:op/slice_sub`、`:op/idx_arr`、`:op/idx_take` 与它们的伴随（`src/builtin/operators/struct.cpp`、`derivative.cpp`）；下标的逐维降低（`src/compile/gct/builder.cpp:2430–2600`、`src/compile/gir/builder_operators.cpp`）；类型规则与静态检查（`src/builtin/modules/globals.cpp:205–560`）；张量模块的下标实例与元素类型规则（`modules/tensor/ops/indexing.cpp`、`elementwise_ops.cpp`、`gradient_ops.cpp`、`linalg_ops.cpp`、`support.cpp`、`layout_ops.cpp`）；ONNX 降低（`modules/onnx/lowering.cpp`）；Tiles（`modules/tiles/src/compiler.cpp`）。读的是代码，没有编译，没有运行；"失败场景"都是从代码推出的，需要先写用例确认的已标明。行号是当前树上的。

## 总评

一个节点两种拼写（下标与函数形式）这件事本身是对的，运行时的 `sliceRange` 写得清楚。问题是**这条规则被实现了至少六次**，每一处都带着自己的一份"缺省值"和"位掩码解码"，彼此靠注释保持一致；已经出现了互相矛盾的地方（第 1、3、4 条）。其次，"模块里的切片被删除"之后留下了第二套切片和第二套按位置取行（第 2 条），以及一条规范里写了、实现里拒绝的下标形式（第 5 条）。元素类型规则则在"拒绝混合类型"之外留了一个由常量折叠决定的后门（第 6 条）。

## 严重

### 1. 切片规则的六份实现，和三份"缺省值由元数推出"的静默回退
同一条规则（起止的缺省、位掩码、步长的符号、个数 `count`）出现在：
- 运行时核 `struct.cpp:219 sliceRange`（掩码 `written & 1/2`）；
- 静态拒绝 `globals.cpp:265 staticSliceRejection`（`:286/288` 解码掩码）；
- 静态长度 `globals.cpp:380 staticSliceLength`（`:393/396` 再解码一次，个数公式第三份）；
- 下标实例 `modules/tensor/ops/indexing.cpp:385`（解码，个数公式第四份）；
- ONNX 前向 `lowering.cpp:476`（解码，个数公式第五份）与伴随 `:614`（只解码一位）；
- 反向规则 `derivative.cpp:~325 sliceSubArrayVjp`（元数缺省）。
缺省值"步长 1、掩码 3"按元数回填，在 `struct.cpp __slice_sub__`（`words >= 5 ? … : 1`、`words >= 6 ? … : 3`）、`globals.cpp:279/383/384`、`sliceSubArrayVjp`、`indexing.cpp:372–378`、`lowering.cpp:473/475` 各写了一遍；ONNX 前向接受 4–6 个字，ONNX 伴随却要求恰好 7 个字（`:586`）。
根因在注释里自己承认了：`globals.cpp:490`、`:552` 写着"the declaration's defaults do not reach a call to a native operator today (`defaultsOfCallee` finds nothing for the group) … See the report"。也就是说**应该由调用构造器在建图时补全的缺省，被每个读者各自补了一遍**；"see the report"指向仓库外的东西。
失败场景：任何一处改了缺省或掩码的位含义，其余五处不报错、各自按旧意义继续——例如把位 0/1 对调，运行时与静态检查就对不上。
修法：在 `builder_calls.cpp` 的缺省填充路径里把缺省补上，使节点**永远是六个字**；删掉所有 `words >= …`/`ctx.has(…)`/`size() > …` 回退；掩码用一个具名的小结构（`SliceBounds { startWritten, endWritten }` 加 `pack/unpack`）或枚举位，只在一处定义；个数公式写成一个函数（`sliceCount(start, end, step)`），运行时、类型规则、下标实例、ONNX 共用。

### 2. 删除模块切片之后，还有第二套切片与第二套"按位置取行"
- `xs->slice<start, end>`（`:struct/slice_arr`，`struct.cpp:162 __slice_arr__`，类型规则 `globals.cpp:2527`，伴随 `derivative.cpp:292 sliceArrayVjp`，注册 `builtin.cpp:378`）是与 `:op/slice_sub` 并存的**第二个切片内核**：不看轴、不支持步长，且**总是复制**（逐元素 `set<slot_t>`），而 `xs[a:b]` 在紧凑数组上是共享数据块的视图（`struct.cpp:__slice_sub__`）——同一个切片，两种拷贝行为，设计原则 5.4 说切片是视图。它的规则表（负数是故障、`end < start` 为空）与 `sliceRange` 相同，只是又写了一遍。函数形式 `slice(t, axis, …)` 早已是核心节点，这个 `with` 形式只需要在建图时降成同一个节点（轴 0、步长 1、掩码 3）。
- 张量模块里 `tensor:idx_take`（`layout_ops.cpp:716`，导出为 `__idx__`）是 `take` 的别名（`subscriptTakeKernel` 直接调 `takeKernel`），与核心的 `:op/idx_take`（`struct.cpp:410 __idx_take_arr__`）是**同一个运算的两份实现**，各有一份伴随：核心的 `:tangent/scatter_take_arr`（`struct.cpp:948`，`derivative.cpp:386`）和模块的 `tensor:take_grad`。
- 更糟的是**导数规则被两处注册到同一个 URI**：`derivative.cpp:467 registry.setRule(":op/idx_take", idxTakeArrayVjp)` 与 `layout_ops.cpp:641 registerSubscriptTakeRule()`（`catalog.cpp:94` 调用）都对 `":op/idx_take"` 调 `setRule`，而 `DerivativeRegistry::setRule` 是 `insert_or_assign`（`src/core/derivative.cpp:42`）——**后注册的赢**，结果取决于程序是否加载了 tensor 模块，以及加载次序。同一个 `t[idx]` 在两种程序里走不同的伴随（一个是 `scatter_take_arr` 的逐元素 `addTangent`，一个是 `take_grad`）。
修法：删除 `:struct/slice_arr` 整条链（内核、规则、伴随、注册），`slice<a, b>` 在建图时降成 `:op/slice_sub`；删除 `tensor:idx_take` 与 `registerSubscriptTakeRule`，`take` 与 `t[idx]` 共用一个内核与一个伴随；一个 URI 只能有一处 `setRule`（给 `setRule` 加"已存在则报错"的断言，立刻能抓到这类重复）。

### 3. ONNX：`t[idx]` 与它的伴随没有降低，`t[-1]` 却被接受
- `lowering.cpp:1269` 注册的是 `"tensor:take"`；`:op/idx_take` 与 `tensor:idx_take` 都没有降低（整个 `modules/onnx/` 搜 `idx_take` 无命中），`:tangent/scatter_take_arr`、`tensor:take_grad` 也没有。规范（设计原则 5.4）里 `t[idx]` 就是 `take`，所以**写成下标的取行不能导出**，嵌入层的训练步（取行的反向是散射加）更不能导出。同一个操作用 `take(t, idx)` 写能导出，用 `t[idx]` 写就不能：导出能力取决于拼写。
- `lowering.cpp:859`（`lowerArrayIndex`）对常量下标做 `k = i < 0 ? i + n : i`：`shape(t)[-1]` 导出成最后一维，而运行时核 `ops.cpp:824` 明确 `raw < 0` 是故障。导出的图在程序会出故障的地方给出答案。`Gather` 对运行时的负下标同样按"从末尾数"读（ONNX 的语义），这里没有像切片那样加防护。
- 切片的防护（`Add(b, Gather(zeros(extent + 1), b))`，`:429–530`）依赖"`Gather` 越界时 ONNX Runtime 报错"。ONNX 规范里越界的 `Gather` 是未定义行为，CPU 以外的执行提供者（CUDA、TensorRT）常常不检查。故障被模拟成"某个运行时碰巧报错"，没有第二条保证。
修法：给 `:op/idx_take` 与 `:tangent/scatter_take_arr` 加降低（`Gather` 与 `ScatterND`/`ScatterElements`），常量下标在导出时按运行时规则检查，负数拒绝；越界防护改成显式的 `Less`/`GreaterOrEqual` 比较与一个必然失败的节点，或在导出文档里写明只在 CPU 提供者上保证，并让导出遍记录这个前提。

### 4. ONNX 切片伴随：为统一写法放弃了 `Pad`，常见情形的索引张量巨大
`lowering.cpp:567–670 lowerScatterSliceArr` 对任何步长都建完整的 `ScatterND`：对 `dx` 的每个元素生成一个 `rank` 维的 int64 坐标（`Range`、`Unsqueeze`、`Mul`、`Add` 逐维叠加后再相加）。最常见的情形是步长 1 的切片（`x[:, a:b]`、`x[a:b]`），原来用 `Pad`（一个常量填充）就够。
失败场景：反向一个 `float32[1024, 4096]` 的切片梯度（约 4M 元素，rank 2）要多出 `4M × 2 × 8 B = 64 MB` 的索引张量，每步训练一次；rank 4 的特征图更大。ORT 的 `ScatterND` 在 GPU 上还需要拷贝整个 `zeros` 再写入。
修法：步长为 1（或可证明连续）时发 `Pad`；其余步长再用 `ScatterND`。两个出口都在同一个函数里，由步长常量选择。

## 高

### 5. 规范写了、实现拒绝：`t[:, 0]`、`t[a:b, c]`
设计原则 5.4 的"下标与切片"一条写明：每一维写一个位置、一个范围或一个整数数组，"写位置的那一维从结果里去掉"，"`t[:, 0]` 是第二维的第 0 个，`t[a:b, c]` 在第一维切片、在第二维取位置"，"哪一维、什么秩都是同一套规则"。实现里 `gct/builder.cpp:~2530` 对这两种形式直接报 `ConstructNotSupported`（"a subscript that names a range or a position on more than one dimension"），并且有计划用例把这个拒绝钉死（`test/plans/feat/modules/tensor/subscript.plan.toml:81`）。这是**规范与实现的偏差用一条"不支持"诊断盖住**。
修法：要么实现——降成"先取位置（`idx_arr`/`select` 去掉那一维）再切片"的两个节点；要么把原则改成现实，并在同一个提交里改计划用例。不要让规范领先实现而不留下待办。

### 6. 元素类型规则被"常量"后门掏空
`elementwise_ops.cpp:117 operandType` 对"张量加标量"的处理：`ctx.sourceLevel()` 为假（任何重新推导类型的遍），或标量是"常量"（`isConstantScalar`：字面量，**或编译器折叠成常量的局部变量**，`:75–79`）时，**不拒绝**，按 `computedElementType` 算（浮点张量取张量自己的类型，整数张量遇浮点标量取 f32）。
- 失败场景一：`let k = 2.0` 是 `float`（f64）变量；`x * k`（`x: f32[..]`）按规则（5.1：变量不隐式转换）应当被拒，但 `k` 被折叠成常量，于是**静默窄化为 f32**。同一个值作为函数参数 `k: float` 传入就被拒——接受与否取决于有没有被折叠，优化遍的行为成了语义。
- 失败场景二：整数张量遇到浮点常量，结果类型变成 f32（整数→浮点的隐式提升），原则 5.1 明确没有。
- 注释自己写着缺口："the narrowing is a defect the literal rule does not yet cover, not a value to report"，以及"A pass that reads the graph again infers without `sourceLevel` and keeps that tolerance"：规则只在源码层生效，再推导时放行，所以**同一张图在两个阶段可以有两种类型**。
`:90–115` 的注释是两段不同主题的文档注释连在同一个函数上（标量那段与"两个张量"那段），且大段叙述历史（"has always given"、"not yet cover"），应当只写规则。
修法：只有**写出来的字面量**取上下文类型（这在字面量规则里完成，进入运算符之前）；其余一律拒绝，删除 `isConstantScalar` 与 `computedElementType`；再推导遍不放宽，遇到混合类型就是内部错误，要追到产生它的那个遍。

### 7. 四种写法表达"两个操作数元素类型相同"，行为各不相同
- `refuseMixedElementTypes`（`support.cpp:215`）：**抛异常**，文字教用户写 `cast(value, "f32")`——而 `cast` 字符串形式已经决定删除，诊断在教一个要删的拼写。
- `commonElementType`（`support.cpp:206`）：不同则 **返回空**（静默"推不出"）。
- `gradientLikeTyped`（`gradient_ops.cpp:192`）：不同则**返回空**；调用方得到的是通用的"没有匹配的函数"，没有"请转换"的提示。
- `sameElementType`（`indexing.cpp:635`）：又写了一遍，且**任一方未知算相同**（`!lhs || !rhs ||`），与前面三个对"未知"的处理不同；注释说"下标声明必须接受签名接受的同样的调用"，靠人工保持一致。
同一个失败有的抛、有的返回空、有的放行。修法：一个谓词 `sameElement(a, b) -> {Same, Different, Unknown}`，由调用方决定未知怎么办；诊断文字只在一处，转换写法用 `f32(x)`（元素逐个转换的新写法）。

### 8. 伴随与类型推断把元素类型写死为 f32
`gradient_ops.cpp:215 permuteInverseInfer` 与 `:200 layerNormGradInfer` 的结果类型是 `tensorOf(TypeCode::Float32, …)`，不看输入。`permute_inverse` 的内核（`:157`）保持输入的元素类型，所以 f64、bf16 的转置在反向里**静态类型是 f32，值却是 f64**：`wrap(norm.resultType(), …)` 用静态类型给数组贴标签，下游读到的元素类型与数据不符。需先写用例确认（f64 的 `transpose`/`permute` 做 `grad`），但代码里没有任何地方让结果类型随输入。
修法：结果类型取 `ctx.facts(0).dtype`（与同文件的 `sameAsFirst` 一致）。

## 中

### 9. 运行时按类型分派，而类型是静态的
- `ops.cpp:804 __builtin__idx__` 注册在 `op/idx_arr` 下，函数里却对 `norm.code(1)` 与容器类型做 `switch`（元组、数组、字符串、映射……）：解析器已经按类型选好了 URI，内核又在运行时再选一次。应当一个 URI 一个内核（`idx_arr`、`idx_str`、`idx_tuple`），分派留在类型规则里。
- `struct.cpp __slice_sub__`、`__idx_take_arr__`、`scatterSliceGradient` 逐元素 `get<slot_t>/set<slot_t>(…, arrType)`、`getSlot/setSlot`，每次读写都过一遍元素类型的运行时转换；紧凑数值数组可以按块 `memcpy`（外层 × count × 内层的三重块拷贝，不是每元素 `%` 和 `/` 各 `rank` 次，见 `parentIndexOfSlice`）。`x[:, a:b]` 及其反向每步训练都走这条路。
- `__tangent_scatter_slice_arr__`（`:1007`）与 `__tangent_scatter_take_arr__`（`:948`）逐元素调 `zeroTangent(ctx, primalElem, tangentElem, …)`、`addTangent(ctx, elemType, …)`：类型在建图时已知，应当在建图时选好零与加的内核（或先整块填零，再按行累加）。
- `sumToLaw`（`indexing.cpp:828`）用"f32→0、f64→1"这种位置编号选择规律：bf16/f16 没有实例，融合静默关闭。

### 10. 溢出与边界
- 个数公式 `(end - start + step - 1) / step`（`struct.cpp:226/231`、`globals.cpp:402`、`indexing.cpp:439`、`lowering.cpp:539`）在 `step` 接近 `INT64_MAX` 时有符号溢出（未定义行为）；`-step` 在 `step == INT64_MIN` 时同样。写成 `(end - start - 1) / step + 1`（`end > start` 时）即可。
- 常量轴越界（如 `slice(t, 7, 0, 1)`，`t` 是二维）：`sliceResultType`（`globals.cpp:~463`）对"轴不在秩内"返回"值自己的类型"，`staticSliceRejection` 也不查轴——编译通过，运行时故障。轴常量与秩都已知的时候应当是编译期诊断。
- `sliceAxis`（`struct.cpp:248`）接受负轴（`-1` 为最后一轴），但错误信息写的是"between 0 and rank-1"。负轴到底合不合法，规范里没有写（下标是不许负的）：要么拒绝，要么把文字改对。
- `staticSliceRejection` 对负步长的起点只查 `from <= extent`，运行时要求 `start < extent`（`sliceRange` 的反向分支）：`t[5::-1]`（长度 5）编译通过、运行时故障。静态检查应当与 `indexing.cpp` 的区间（反向起点上限是 `extent - 1`）一致。

### 11. 死代码与过时的叶子
- `globals.cpp:228–234`：一段文档注释（"Whether `t` is a tensor the module that owns tensors slices …"）后面没有任何声明，是被删的模块切片留下的孤儿。
- `tensor/ops/semantics.cpp:231`：`t.emplace("slice", …)`，对应的 `OpDef` 早已不存在。
- `modules/tiles/src/compiler.cpp:1179`：`n.op == "slice"`，没有任何算子会产生这个名字（核心切片走 `coreSlice` 分支，`:950`），这个分支永远不会进。
- `lowering.cpp` 里 `add("tensor:take", …)` 的伙伴 `tensor:take_grad`、`tensor:idx_take` 没有降低（见第 3 条），而它们本身在 `layout_ops.cpp` 里仍保留——若按第 2 条删除，应一并清理。
- `struct.cpp:160 __slice_arr__` 的注释写"It used to be cut to the array, and a negative bound counted from the end"：历史叙述。

### 12. 注释（叙述历史、引用不存在的东西）
- `globals.cpp:216` "The wording is the one the tensor module's `indexInfer` used … before the flip"；`:467–470` "the module's inference, which the core took over"；`:478` "the bare `Tensor` spelling"（该拼写已删除）；`:490/552` "See the report"（见第 1 条）。
- `struct.cpp:283–290`："The argument count still tells the two spellings apart until the declaration's defaults reach the call (see the report)"，与同函数头注释"the argument count no longer tells the two spellings apart"互相矛盾。
- `lowering.cpp:434–450`：把一次缺陷的修复过程写进了注释（"`x[3::-2]` of a four-element axis was `[]` instead of `[4, 2]`"）。这是提交信息的内容，不是规则；规则只需要说"未写的反向终点是第一个元素之前的位置，ONNX 用低于一切范围的常量表示"。
- `elementwise_ops.cpp:90–115`：见第 6 条。
- 若干注释把代码原样抄进注释（`struct.cpp:217` "intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0))"），代码一改注释就错。

### 13. 诊断质量
- `refuseMixedElementTypes` 的消息建议 `cast(value, "f32")`（第 7 条）；标量的那条消息建议 `f32(value)`，同一个模块两种建议。
- `staticIndexRejection`（`globals.cpp:218`）与 `staticSliceRejection` 的消息是裸文字（"index 5 is outside axis 0 of extent 3"），不是注册表里的具名诊断；而步长 0 却是具名诊断（`SliceStepZero`），三个同类的静态拒绝有两种形态，且 `:283` 注释特意说明"具名诊断优先于解析器的消息"——说明作者已经知道应当统一。
- `ConstructNotSupported` 的两条参数文字（`gct/builder.cpp:~2535`）把内部实现（"the slice operator takes one axis, and the index operator takes the first dimension"）当理由告诉用户。

## 缺的测试（按上面的发现）
1. 同一 `t[idx]`：加载与不加载 tensor 模块，`grad` 走的伴随是否一致（第 2 条）。
2. `t[idx]` 与 `take(t, idx)` 的 ONNX 导出对照；`shape(t)[-1]` 导出应被拒（第 3 条）。
3. 缺省回填：把 `slice(t, axis, a, b)`（四个字）与六个字的写法比较节点与导数，应当逐字相同（第 1 条）。
4. `let k = 2.0; x * k` 与 `func f(k: float) = x * k` 的接受情形必须一致（第 6 条）。
5. f64/bf16 张量的 `permute`/`transpose`、`layer_norm` 求导后，梯度的元素类型（第 8 条）。
6. 步长为 `INT64_MAX`、`INT64_MIN` 的切片；常量越界轴；负步长起点等于长度（第 10 条）。
7. `xs->slice<a, b>` 与 `xs[a:b]` 的数据块共享行为（第 2 条）。
