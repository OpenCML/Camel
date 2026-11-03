# 写在代码里的类型和名字：四（五）类普查

范围：`modules/transforms/**`、`modules/autodiff/**`、`modules/tensor/**`。判据来自
`docs/design-principles.cn.md` 9.9 / 5.1 / 5.4：**一个值的切向属于它的类型**，`zerosLikeUri`、
`addUri`、`scatterUri` 是**类型**的回答，不是算子表里的某一个名字；`TensorFacts` 的 `dtype`
同理。判词三种：**恒等**（写死的常量语义就是它的答案，不改）、**已证明错**（有读数/有 lane）、
**潜在**（写一句“什么条件下会错”）。

## 第一类：写在代码里的算子名

| 位置 | 这一行写的是什么 | 本该等于什么 | 判 |
|---|---|---|---|
| `transpose.cpp#zeroOf` | `:tangent/zeros_arr` | `space.space->zerosLikeUri` | **已证明错**（`lane-zerocrash`，不在我范围内） |
| `transpose.cpp#buildTransposeMember#accumulate` | `TypeCode::Array ? ":tangent/add_arr" : ":op/add_d"` | 数值数组的 `addUri` 是 `tensor:add` | **潜在**：数组余切的和；今天 transpose 的 `float` 闸门挡住了数组退出，无已知红 |
| `derivative.cpp#idxArrayVjp` | `":tangent/zeros_arr"` | 同 `zeroOf` | **潜在**：实测 `t[:, idx]` 在 f32 / f64 上余切正确（`[1,1]` 行取两次得 `sum=4.0`），名字与注册表的答案在数值和类型上都一致 |
| `derivative.cpp#idxTakeArrayVjp` | 同上 | 同上 | 同上 |
| `layout_ops.cpp#takeVjp` | 同上（三词 `m[:, idx]` 的零） | 同上 | 同上；条件写在 `gradient_ops.cpp#registerTensorTangentSpace`：数组这个 `TypeCode` 被两种值共用，差异只在“元素是结构/元组的数组”上 |
| `pullback.cpp#metadataOper` | 五个名字的清单（`tensor:shape`、`:op/len_arr`、`:op/len_str`、`tensor:rank`、`tensor:numel`） | “这个算子只读元数据”这一事实 | **潜在**：新增一个元数据算子会被当成读值，污染活跃性分析 |
| `pullback.cpp#controlInput` | `"tensor:where"` / `":op/where"` | 同一算子的两个拼写 | **恒等** |
| `transforms/module.cpp#printRuleUris` | `":op/div_d"` 闸门 + `genericRuleInstance(..., {Type::Float64()}, {})` | 规则自己的类型 | **潜在**：数组形状的法则没有 float 实例时静默 `break`，打印的法则比存在少（只读 pass） |
| `rule_expand.cpp#expandRuleByName` | 实例化失败后重试 `{Type::Float64()}` | 操作数的元素类型 | **潜在**：`mul_t` 类法则的 `T` 只出现一次、却有两个操作数，重试是承重的；f32 数组操作数会实例化出 `float` 法则。见 `docs/handover/questions/2026-11-04-should-a-law-retried-at-float-be-a-refusal.cn.md` |
| `layout_ops.cpp#concatVjp` | `:op/slice_sub`（两处） | 同一节点，与下标共用核心算子 | **恒等**（注释即理由） |
| `transpose.cpp#primitive` | `Type::Float64()` 的 lambda | —— | **死代码**（构建报 unused variable） |

## 第二类：写在代码里的类型

`transpose.cpp` 里十来个 `Type::Float64()` 的判词只有一个：**恒等，但其条件是闸门**。
`transpose_operator.cpp#transposeOperatorInfer` 拒绝一切 `exitType() != Float64` 的函数，
所以那些回落今天写的就是真答案；闸门一放宽，它们同时变成缺陷。这一条是普查的结论，不是安慰：
改这一族的正确做法是**先改闸门，再逐处问类型**，而不是逐处换常量。

| 位置 | 写法 | 判 |
|---|---|---|
| `transpose.cpp#cotangentTypeOfResultType`、`#addUriFor` 尾、`#accumulate`、闭包成员端口（`declared != nullptr ? declared : Float64()`）、被调方答案类型（`TupleType::create(answerCount, Float64())`）、loop 状态余切端口（`vector(stateCount, Float64())`）、`addFuncNode(transposed, Float64())` | `Type::Float64()` | **恒等（在闸门下）**；被调方那一处的正确答案其实在手（`transposed->funcType()`），闸门放宽后应改读它 |
| `transpose.cpp#…` join 先建 `Float64()` 再改写 | —— | **恒等**（用前已改写） |
| `creation_ops.cpp#resultElement` | `… : TypeCode::Float32` | **潜在但不可达**：只有 rank-0 结果的类型不是数组时才落到默认，而 `zeros::<f64>([])` 在调用处就被拒绝（`NoMatchingFunction`，`int[]` 不接受 `[]`） |
| `gradient_ops.cpp#permuteInverseInfer` | 三个分支 `tensorOf(TypeCode::Float32, …)`，不读 `ctx.facts(0).dtype` | **潜在**：只有重推者能看见（`onnx/exporter.cpp#inferFacts` 调 `def->infer`）。实测运行值是 f64（48 字节、6 个 double，绑到 `f64[2, 3]`） |
| `layout_ops.cpp#elementAtInfer` | `t.dtype.value_or(TypeCode::Float64)` | **恒等**：内核 `pick_at` 取的就是元素槽本身；f32 / f64 / int / 泛型 `T[..]` 四种实测都对 |
| `layout_ops.cpp#oneHotInfer` | `tensorOf(TypeCode::Float32, out)` | **恒等**（调用不带元素类型，内核就产 f32） |
| `utility_ops.cpp#indexInfer` | `return Type::Float64()` | **恒等**：`f32[3]` / `f64[3]` / `int[3]` 的下标带着正确元素类型，核心解析器先答，这里只服务“元素类型没定”的形状 |
| `elementwise_ops.cpp#binaryInfer` / `#unaryInfer` | `f64 in → f64 out, else f32` | **恒等**（两种浮点宽度，规则写死） |
| `vjp.cpp#addFloatOper` + `#floatGradientAtWidth` | `Type::Float64()` 配 `:op/dtof` / `:op/ftod` | **恒等**（算子名自己说了宽度，随后显式窄回去） |
| `indexing.cpp#lawIndex` | `{f32→0, i64→1, bool→2}`，其余 `nullopt` | **潜在**：f64 张量没有下标实例（覆盖缺口，不是错答） |
| `reduction_ops.cpp#floatLikeFirst` / `#softmaxInfer` | `dtype == Float64 ? Float64 : Float32` | **潜在但不可达**：源码里已经没有“元素类型未知”的张量拼写（`Tensor` 这个别名已删除），只有重推者能构造 |

## 第三、四类：查不到就答默认 / 只读一张表

| 位置 | 写法 | 判 |
|---|---|---|
| `transpose.cpp#cotangentTypeOf`（draft ref 拿去问**源图** → nullptr → `Float64()`） | 跨图取值 | **已证明错**（brief 第 2 条） |
| `transpose.cpp#reachesAParticipant`（只 `normInputsOf`） | 只读一张表 | **已证明错**（brief 第 5 条）；`transpose_loop_escape` 两条红与它同族 |
| `jvp.cpp#zeroConstantFor`（with 端口写 0.0） | 查不到切空间 → 零 | **已证明错**，已被另一条 lane 重做（改问 `tangentSpaceOf`，并同时读 `normInputsOf` / `withInputsOf`） |
| `jvp.cpp#makeCopy`（丢 `StructKey`） | 查不到 → 静默默认 | **已证明错**，同上 |
| `jvp.cpp` 循环取元素端口写 `zeroConstantFor` | 查不到 → 零 | **已证明错（我测的）**：`f(xs) = loop s = 0.0 for e in xs do s + e`，`jvp(f)([1.0,2.0],[1.0,1.0])` 答 `value=3.0 tangent=0.0`，手算切向是 `2.0`。**本次修正**：拒绝，见下 |
| `transforms/module.cpp#printRuleUris`（`entry == nullptr \|\| at >= instances.size()` → `break`） | 查不到 → 不打印 | **潜在**（只读 pass：静默少一行） |
| `rule_types.h#firstOperandTypeOf`（全是条件时返回条件的原类型） | 查不到 → 默认 | **恒等**（文档写了“今天没有这种情况”） |
| `vmap.cpp#batchThroughRule`（`operandType ? operandType : function->node(ref)->dataType()`） | 查不到 → 节点自己的类型 | **潜在**：注释明说不能读节点类型（那是算子**答**的类型），回落读的正是它；节点没有值操作数时才触发 |
| `tensor/module.cpp#node-role 分类器`（只 `normInputsOf`） | 只读一张表 | **潜在**（只影响诊断里的 node-role） |

## 第五类（新增）：跨图使用下标/编号

`lane-zerocrash` 的原话是判据：“名字错了会崩得响，编号错了会在别的形状上静默答错。”

| 位置 | 写法 | 判 |
|---|---|---|
| `transpose.cpp#buildTransposeMember` 把 `ports[port]`（源图端口下标）交给新图 | 编号跨图 | **已证明错**，`859862b8b` 已修 |
| `transpose.cpp#cotangentTypeOf` 用 draft ref 去查源图 | ref 跨图 | **已证明错**（brief 第 2 条） |
| `split.cpp#388`（`callee ? callee->funcType() : source->funcType()`）、`#1302/#1948/#2458`（`lifted.node(ref)` vs `function->node(ref)`） | 两个面并写 | **恒等**（各自的 guard 与面都对） |

## 数

表 25 行，覆盖 45 个位置（`transpose.cpp` 那一族的十余处 `Type::Float64()` 合成一行，逐个写在行内）。
判词：**恒等约二十处**（闸门下的一族、`tensor` 的推型默认值、`vjp.cpp` 的宽度配对）、
**已证明错 7 处**（其中 4 处是别的 lane 的，我只引用并复测）、**潜在 15 处**、**死代码 1 处**
（`transpose.cpp#primitive`，构建报 unused variable）。`split.cpp` 与 `vmap.cpp` 全文没有写死的算子名
和类型（两次 grep 为空），漂移集中在 `transpose.cpp`（最大、最少重构）、`pullback.cpp`（名字）和
`tensor` 的推型默认值上。

## 本次改动（先红后绿）

一条：`jvp.cpp` 的 `for` 循环把**被迭代的数组的元素**当“没有切向”处理，于是对“数组本身有切向”
的程序答 0.0。读数在手（`value=3.0 tangent=0.0`，手算 2.0），且同一程序的 `transpose` 已经拒绝
这个形状（`range`），所以前向半场改成拒绝并点名构造：

    error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: jvp does not reinterpret a
    loop over a value that has a tangent yet (the element the loop hands its body has the tangent of
    the array's element, and no tangent of an element is carried).
      --> jvp_loop_iterable_tangent.cml:25:12        (the `loop` in `f`, with `note: raised here`
                                                      at the `jvp(f)` call on line 29)

用例 `test/cases/compile/transforms/jvp_loop_iterable_tangent.cml`，计划项
`jvp_loop_iterable_tangent`（`expect_stdout_not_contains` 就是红的那一半）。判别“这个切向是不是本
变换自己写的零”靠一张 `zeroMade` 表：`zeroConstantFor` 与 `zeroOfValueAtRunTime` 写出的零都记进去，
所以“字面量序列”和“捕获的数组”两条老路一个字节都没变。

**门读数**（host 88zjwei，1m load 14.8–20.3，**满载机，不与静默机读数相比**）：

| 目标 | 树 | 时间 | 结果 |
|---|---|---|---|
| `test/plans/feat/compile/transforms.plan.toml` | 我这份（改动前同计划 105 项） | 2026-11-04 05:58 | `total: 106, pass: 99, fail: 2, known failures: 5` |
| 同上，加 `zeroOfValueAtRunTime` 记录后复测 | 同上 | 2026-11-04 06:05 | `total: 106, pass: 99, fail: 2, known failures: 5` |

两条 fail 都是基线就红的 `transpose_loop_escape.two_values_of_two_kinds` 与
`transpose_loop_escape.second_state_of_the_same_loop`（我未增未减）；`total` 106 = 105 + 我这一项，
`pass` 99 = 98 + 1，说明没有把任何一条老绿色改红。

**没动的**：上表其余 44 处，逐条理由在表里；不可达的（`creation_ops`、`reduction_ops`、
`permuteInverseInfer`）只记录不改 —— 它们要一条能走到那里的用例，而今天没有。