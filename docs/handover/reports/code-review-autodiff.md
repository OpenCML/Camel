# 自动微分代码审查

范围：`src/builtin/operators/derivative.cpp`、`include/camel/core/derivative.h`、`src/core/derivative.cpp`（注册表与切空间类型）、`modules/autodiff/{operators,pullback,rules}.cpp`（`grad`/`with_grad` 的 `std::macro` 展开走通用宏机制，`readsGraphs + macro` 标记见 `operators.cpp:163`）、`modules/tensor/ops/{vjp,elementwise_ops,reduction_ops,layout_ops,linalg_ops,gradient_ops}.cpp`、`modules/tensor/kernels/gradient.cpp`、`modules/nn/tensor_ops.cpp`、`modules/math/module.cpp`、`src/builtin/operators/struct.cpp` 的 `:tangent/*`、`modules/onnx/lowering_grad.cpp`。只读代码，没有构建、没有运行；凡标"需确认"的是由代码推断、未实测。

手算核对过且正确的 VJP：标量 `add/sub/mul/div/neg/pow(常数指数)/sqrt`、张量 `divide`（`-dy*y/rhs`）、`pow`（两个方向）、`maximum/minimum`、`sqrt`（`0.5*dy/y`）、`rsqrt`（`-0.5*dy*y^3`）、`sigmoid`、`tanh`、`log_softmax`、`gelu_grad` 的公式、`layer_norm_grad` 的 x/gamma/beta 三路、`sum/mean/sum_axis/mean_axis`、`matmul` 两路与批量/向量视图、`dtof/ftod` 回转、切片与行取的散射加。数学本身没有发现写错的，问题集中在注册、宽度、并列值和覆盖面。

按严重度排序。

## 高

### H1 每次构造 Context 都重注册内置导数，会抹掉张量的切空间并覆盖张量的规则

- `src/builtin/modules/globals.cpp:2993`：`GlobalsBuiltinModule` 构造函数每次都调 `registerBuiltinDerivatives()`；张量模块则用 `std::call_once`（`modules/tensor/ops/catalog.cpp:72`）只注册一次。
- `src/builtin/operators/derivative.cpp:505`：`setTangentSpace(Array, arraySpace)` 的 `owns == nullptr`，`src/core/derivative.cpp:56-61` 对这种空间执行 `spaces.clear()`，把张量用 `owns=&isTensorType` 注册的子集空间一并清掉。
- `src/builtin/operators/derivative.cpp:467` 的 `setRule(":op/idx_take", idxTakeArrayVjp)` 同样每次重跑，用 `insert_or_assign`（`src/core/derivative.cpp:44`）覆盖张量模块的 `takeVjp`。
- 失败场景（需确认进程内确有第二个 Context，测试进程、REPL、宏子上下文都可能）：第二个 Context 之后，张量参数的梯度累加走 `:tangent/add_arr`（按槽位逐元素），`mean_axis` 之类的梯度得到非张量布局的值，即 `registerTensorTangentSpace` 注释里记录过的 `export_grad_mean` 崩溃的同类问题；`xs[idx]` 的规则在两种实现间随创建顺序翻转。
- 修法：内置注册也走一次性（`call_once`），并且让 `setRule` 在"同 URI、不同函数"时直接抛 `logic_error`（见 H2），把静默覆盖变成启动期错误；`setTangentSpace` 的"无 owns 则清空"改为只替换同样无 owns 的那一项。

### H2 `:op/idx_take` 被两个模块各注册一份规则，后注册者静默胜出

- 核心：`src/builtin/operators/derivative.cpp:467` `idxTakeArrayVjp`（`:tangent/scatter_take_arr`，面向"槽位数组"）。张量：`modules/tensor/ops/layout_ops.cpp:641`（经 `catalog.cpp:94`）`takeVjp`（`tensor:take_grad`，面向张量）。另有 `layout_ops.cpp:759` 把同一个 `takeVjp` 登记给 `tensor:idx_take`。
- 失败场景：加载张量模块后，记录数组或嵌套数组的 `xs[idx]`（核心规则本来答的就是这类）改走 `takeVjp`，它没有 `hasTensorAdjoint` 检查，会把非张量数组交给 `tensor:take_grad`，运行期读错布局或报晦涩错误；而切空间那边已经用 `owns` 分了两种数组，规则这边没有同样的分流。
- 修法：一个 URI 只允许一个规则；规则内部按 `isTensorType(nodeType(inputs[0]))` 分流（或给 `setRule` 加 `owns` 同款谓词）。重复登记在注册表里抛错，测试里加"两个模块都加载后 `records[idx]` 与 `tensor[idx]` 都对"的用例。

### H3 `gelu_grad`、`erf_grad`、`layer_norm_grad` 对 f64 张量读错类型

- `modules/tensor/kernels/gradient.cpp:34-41` 的 `asFloat` 放行 f32 与 f64，但 `geluGrad`（380）、`erfGrad`（400）、`layerNormGrad`（417 起）随后一律 `dataAs<float>()` 并 `createTensor(TypeCode::Float32, …)`。`dataAs<T>` 不校验 dtype（`include/camel/core/rtdata/array.h:391`）。前向 `gelu/erf/layer_norm` 对 f64 是支持的（`kernels/elementwise.cpp:546`、`ops/reduction_ops.cpp:290-296` 的 `layerNormInfer` 对 f64 给 f64）。
- 失败场景：f64 模型里对含 `gelu`、`erf`、`layer_norm` 的损失求 `grad`：反向把 f64 缓冲当 f32 读，得到乱数，结果是 f32 张量却挂着 f64 的类型（`addTensorOperFor` 按原值类型标注节点），不报错。这正是 `checkGradientType` 想防的"一种宽度的数字配另一种宽度的类型"，它只检查静态类型所以拦不住。
- 同类：`ops/gradient_ops.cpp:203-224` 的 `layerNormGradInfer`、`permuteInverseInfer` 写死 `TypeCode::Float32`。
- 修法：这三个核函数按 `sumToArm` 的做法模板化到 f32/f64 两臂，其它 dtype 沿用 `refuseTensorElementType`；推断函数取输入 dtype。补 f64 的有限差分用例（现有 `tensor_grad_check.cml` 全是 f32）。

### H4 轴向 `max/min` 的梯度在并列时给每个并列元素完整的 `dy`

- `modules/tensor/ops/reduction_ops.cpp:381-392`：掩码是 `strict_eq(x, 展开的极值)`，每个并列元素都乘完整的 `dy`。
- 失败场景：一行里 k 个元素并列最大（常数初始化、relu 之后一整行 0、量化后的数据），梯度总和是 `k*dy`，不是任何合法次梯度（应当和为 `dy`），也和前向 `argmax` 只取一个下标不一致。NaN 行因 `strict_eq(NaN,NaN)=false` 得全零梯度。
- 修法：除以并列个数（JAX 做法），或与 `argmax_axis` 一致只给第一个；补并列用例（`tensor_grad_check` 的 `extrema` 用的是不并列的随机值，覆盖不到）。

## 中

### M1 规则漏掉某个活跃输入时没有任何检查，只有 `with` 值输入有

- `modules/autodiff/pullback.cpp:3896-3925` 只对"通过 `with` 传入的值输入"核对 `contributions_` 是否增长。普通输入被规则忽略就是静默的零梯度。
- 实例：`modules/nn/tensor_ops.cpp:551` `softmaxCrossEntropyVjp` 只给 `inputs[0]`（logits）梯度；若第二个参数是浮点的软标签且来自被微分的参数，它的梯度被静默丢弃。`#vjp` 自定义规则（`visitRuleSite`，`pullback.cpp:3799`）返回元组时同理。
- 修法：对每个有切空间类型且活跃的输入，规则返回后核对贡献数；规则确实要给零时用显式的"声明为零"（如 `builder.declareZero(input)`），而不是什么都不做。

### M2 梯度类型检查只比元素类型码，不比形状/秩，且任一方为空就放过

- `pullback.cpp:2889-2904` `checkGradientType`：`expected==nullptr || given==nullptr || code 相同` 即返回。自定义 `#vjp` 规则（`custom_rule.cml` 一类）返回错误形状的梯度、或任何规则给出秩不对的张量，累加器不会发现，直到下游算子读出错位的数。
- 修法：两边静态形状都已知时比较形状；规则一旦给出秩不同的梯度就拒绝；空类型改为内部错误而不是放行。

### M3 标量 `pow` 规则过严且错误无位置；标量数学函数几乎没有规则

- `src/builtin/operators/derivative.cpp:125-128`：只要指数不是编译期常量就抛 `std::runtime_error`。指数是数据参数（不活跃）时本应只对底数求导，却被拒绝；改成判断 `b.isActive(exponent)`。指数活跃时应像张量版 `powVjp`（`elementwise_ops.cpp:383`）那样给 `dy*y*ln(base)`，而不是拒绝。这里抛的是无源码位置的 `runtime_error`，应走 `refuseVjp`/`failAt` 带 `call.origin`。
- `modules/math/module.cpp:84` 只注册了 `math:sqrt`；`exp/log/log2/log10/log1p/expm1/sin/cos/tan/asin/acos/atan/sinh/cosh/tanh/abs/atan2/pow/hypot` 的标量版（`stdlib/math.cml`）没有规则。`grad` 要求损失是 float 标量，`grad(f)` 里写 `exp(x)` 会得到"no derivative rule for operator 'math:exp_d'"（拒绝是响亮的，不是静默的，但这是最常见的入口）。
- 修法：用一张表生成这批规则（公式都是一行），逐个配有限差分用例；`abs` 在 0 处张量版给 `+dy`（`elementwise_ops.cpp:430`），与 JAX 的 0 不同，裁定一个并写进用例。

### M4 高阶导数覆盖面窄，且有悬空引用

- 带 `setVjp` 的梯度算子只有 `sum_to/broadcast_like/expand_axis/cast_like/permute_inverse` 及若干 `noGradient`（`gradient_ops.cpp:451-466`）。`gelu_grad/erf_grad/layer_norm_grad/softmax_grad/matmul_grad_*/take_grad/row_add_at` 以及 `nn:*_grad` 没有规则，对含它们的网络再求一次 `grad` 会被拒绝（响亮）。`second_order*.cml` 只覆盖数组。
- `src/builtin/operators/derivative.cpp:349-356` 说 `:tangent/init_arr`、`last_arr` 故意不给规则，并写"See the report for the two operations that would unblock them"，这份 report 不在仓库里。
- 修法：要么把"二阶只支持哪些算子"写进 `docs/design-principles` 9.9 并加一个拒绝信息的用例，要么补规则（`softmax_grad`、`matmul_grad_*` 最有用）；删掉悬空引用。

### M5 切空间运算在运行期按类型码分派，数组散射逐次整体复制

- `src/builtin/operators/struct.cpp:714`（`zeroTangent`）、`:823`（`addTangent`）在运行时对 `tangentType->code()` 做 `switch` 并逐元素递归取槽位；`TangentSpace` 里本已有 `addUri/zerosLikeUri/scatterUri`，编译期就能挑好。
- `:965` `__tangent_scatter_add_arr__` 每次调用复制整个 `base`：对长度 n 的数组做 `for` 循环，反向要做 n 次、每次 O(n)，总 O(n²)；`scatter_slice_arr` 用逐元素 `zeroTangent` 递归建零数组。
- 修法：按元素类型在 `registerBuiltinDerivatives` 里选定专用算子；`base` 无别名时就地散射。

### M6 `sum_to` 的 f32 累加不符合 7.3

- `modules/tensor/kernels/gradient.cpp` 的 `sumToArm<Float32>` 用 f32 累加器；`design-principles` 7.3 规定浮点归约用 f64 累加、舍入一次。`modules/tiles/README.md:441` 把它当作有意的"与 `sum_axis` 不同"。大批量时偏置梯度与 `sum_axis` 对同一批数不一致。
- 修法：需要裁定（改成 f64 累加，或在 7.3 里为梯度核写明例外）。

### M7 引擎里写死了模块算子的 URI

- `pullback.cpp:3140`（`tensor:where`、`:op/where`）和 `:3154-3167` 的 `metadataOper`（`tensor:shape/rank/numel`、`:op/len_arr/len_str`）。`derivative.h:7-11` 的前提是"引擎不必认识模块"。另外 `tensor:dim` 不在 `metadataOper` 里而 `numel` 在，两者性质相同；`dim` 作为活跃张量的形状查询被用来 reshape 时会被当作数据污染而拒绝（需确认）。
- 修法：用 `OperatorTable` 的 trait 表达"控制输入下标""形状元数据"，模块自己声明。

### M8 ONNX/GPU 复用面

- `modules/onnx/lowering_grad.cpp:694-715` 覆盖了主要梯度算子，但没有 `tensor:take_grad`、`tensor:row_add_at`、`nn:avg_pool2d_grad`、`nn:softmax_cross_entropy_grad`、`nn:softmax_cross_entropy_labels_grad`、`nn:embedding_table_grad`，也没有除 `scatter_slice_arr` 外的 `:tangent/*`；`ctx.input(…, TypeCode::Float32)` 写死（`:141`、`:205`）。含 `take`、embedding、平均池化、交叉熵的模型，`grad` 之后无法导出（应为 ExportError，需确认是否带算子名）。`tiles/README.md:439-446` 已承认只覆盖子集。
- 修法：补上面几个，f64 一并按 dtype 参数化；导出失败的信息要带算子 URI。

## 低

- L1 混合宽度：`meanAxisVjp`（`reduction_ops.cpp:364-375`）用 f64 的 `extent` 去除 f32 张量，`meanVjp`（326）却特意收窄到 dy 的宽度；`where/maximum/minimum` 的零用 `addStaticFloat`（f64）配 f32 的 dy（`elementwise_ops.cpp:405-415,509-520`）。节点类型取自第一个张量输入所以不报错，是否依赖核函数的标量宽度宽容需确认；与 5.1"无隐式数值转换"不一致，应统一收窄。
- L2 `expandAxisVjp`（`gradient_ops.cpp` 约 `:230`）在缺省 `keepdims` 时取 true，而 `expandAxisKernel` 缺省为 false；目前调用方总传四个参数所以是死路径，但默认值自相矛盾。
- L3 `Engine::gradient`（`pullback.cpp:4520-4523`）先 `ASSERT` 再 `if (!signature) fail`，重复。`controlInput`（3123-3150）末尾两个 `return false` 之前有一支死分支。
- L4 `Engine::refuse`（`pullback.cpp:4150-4170`）把"函数值如何被识别"的补救说明附在每一条拒绝后面，包括"返回 int，不是 float"这类与函数值无关的，会误导。
- L5 `layerNormVjp`（`reduction_ops.cpp:425`）对 x/gamma/beta 各发一个 `layer_norm_grad`，每个都重算均值方差，三倍开销；可以一次返回三元组（`batch_norm_train_grad` 已是这样做的）。

## 大函数与文件

- `modules/autodiff/pullback.cpp` 单文件 4738 行；`Backward::run`（3089-3431）342 行且内含六七个 lambda（活跃性分析、污染传播、控制输入、元数据、联结读取…）；`Engine::buildLoopBody`（4305 起）约 175 行；`Engine::build` 约 110 行；`Backward::visitOper` 110 行。建议把"活跃性/污染分析"拆成独立类（输入：顺序+类型，输出：`active_/tainted_`），循环反向拆到 `pullback_loop.cpp`，`ForwardRewrite` 拆到自己的文件。

## 历史叙述型注释（按 CLAUDE 约定应删，只留现状）

- `src/core/derivative.cpp:193-206`：叙述 `f32[2,3]` 曾经返回 `f32[]`、"两个未初始化浮点"和测试名。
- `src/core/derivative.cpp:215-222`：`nn::sgd` 曾经拒绝秩 2 参数的经过。
- `modules/tensor/ops/gradient_ops.cpp:471-486`：`export_grad_mean` 崩溃的由来。
- `modules/tensor/ops/vjp.cpp:19-33`："the wall behind every red the f64 arm uncovered"。
- `modules/tensor/ops/elementwise_ops.cpp:523-526`："used to be dropped silently"；`layout_ops.cpp:577-580`："no longer means a slice of the module's own"。
- `modules/autodiff/pullback.cpp:3203-3232` 不止是历史，还与代码不符：注释说秩 2 以上数组的循环"cannot be reversed yet … Refuse the loop by name until an array's tangent is the array itself"，而紧随其后的代码只拒绝非数值数组；数值数组已经走 `tensor:row_add_at`。
- `src/builtin/operators/derivative.cpp:349-356` 的悬空 "See the report"（见 M4）。

## 测试缺口

现有有限差分用例：`tensor_grad_check.cml`、`nn_grad_check.cml`、`loop_grad_record_tensor_fd.cml` 等，覆盖 f32 张量算子，步长 1e-3、容差 2%。缺：

1. 任何 f64 张量的有限差分（H3 因此漏过）。
2. 并列极值与 NaN（H4）。
3. 标量 `math` 函数的梯度（M3）与 `abs` 在 0 处。
4. 同进程内创建两个 Context 之后的梯度（H1）、两个模块都加载时 `records[idx]`（H2）。
5. 软标签交叉熵对 targets 的梯度（M1）、自定义 `#vjp` 返回错误形状（M2）。
6. 二阶：含 softmax、matmul、gelu 的网络（M4）。
7. ONNX 导出含 take/embedding/avg_pool 的梯度函数（M8）。
