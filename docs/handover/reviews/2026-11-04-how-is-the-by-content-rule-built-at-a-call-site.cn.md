# 「答 `(value, tangent)` 二元组就切它、转置后一半、把调用转过来」——施工规格

**这份页是施工规格，不是提案。** 来源是作者 2026-11-03 16:55 的裁定（`origin/fsh-dev` `e04ba91a8`，
`docs/handover/reviews/questions-2026-10-29.md` 末尾「裁定（2026-11-03 16:55，下一批第 3 项）」）。
裁定否定的是判据的来源（不许读 `callee->liftedFrom()`），肯定的是规则本身（两处调用点同一条），
给出的是判据（"被调函数答 `(value, tangent)` 二元组，也就是 `split` 本来就认的那种形状"）。

本页只回答施工要回答的五件事：判据在代码里怎么落地、三处入口分别是什么、两处调用点各改哪段、
哪些现有用例会因此改变行为、这份规格与裁定有无出入。**不动代码。**

---

## 一、"按内容"的判据：`exitShapeOf` 求的是图，不是签名

先回答最要紧的一点，因为它决定裁定能不能落地：**`exitShapeOf` 是对图求值的**，不是对签名求值的。

```cpp
ExitShape exitShapeOf(const rt::GCGraph *function, gc_node_ref_t exit, LoopCut &cut) {
    const rt::GCNode *node = function->node(exit);
    if (node == nullptr) { return ExitShape::NotAPair; }
    if (node->kind == rt::GCNodeKind::Fill) {
        const auto *fill = function->nodeBodyAs<rt::GCFillBody>(exit);
        if (fill != nullptr && fill->fillKind == rt::GCFillKind::Tuple &&
            function->withInputsOf(exit).size() == 2) {
            const std::span<const gc_node_ref_t> halves = function->withInputsOf(exit);
            const LoopReach value   = loopBehindExit(function, halves[0]);
            const LoopReach tangent = loopBehindExit(function, halves[1]);
            if (value.ok && tangent.ok && value.loop == tangent.loop && value.component == 0 &&
                tangent.component == 1) {
                cut.loop = value.loop; /* …四个字段… */
                return ExitShape::Loop;
            }
            return ExitShape::Straight;
        }
    }
    if (node->kind == rt::GCNodeKind::Loop) { /* 出口就是 loop 本身 → ExitShape::Loop */ }
    if (node->kind != rt::GCNodeKind::Join || …) { return ExitShape::NotAPair; }
    if (branchConditionOf(function, exit) != rt::kInvalidNodeRef) { return ExitShape::Branch; }
    cut.loop = exit;
    return ExitShape::Loop;
}
```

（`modules/transforms/split.cpp#exitShapeOf`，返回 `ExitShape` 四格：`NotAPair` / `Straight` / `Loop` / `Branch`。）

**"答 `(value, tangent)` 二元组"在代码里的精确条件 = 图侧与签名侧两条同时成立**：

| 侧 | 条件 | 源码 |
| --- | --- | --- |
| 图侧 | 出口节点是 `Fill`、`fillKind == GCFillKind::Tuple`、`withInputsOf(exit).size() == 2` | `split.cpp#exitShapeOf`（上面那段） |
| 签名侧 | `normTypesCount()` 非零且**为偶数**、`exitType()` 是 `Tuple` 且**成员恰为 2**、两成员都有类型 | `split.cpp#splitHalvesWith` |

签名侧的原话（`split.cpp#splitHalvesWith`）：

```cpp
    const size_t count = function->normTypesCount();
    if (count == 0 || count % 2 != 0) { halves.why = "`split` takes a lifted function, …"; return halves; }
    if (!function->hasExitType() || function->exitType()->code() != TypeCode::Tuple) { … return halves; }
    const auto *exitPair = tt::as_ptr<TupleType>(function->exitType());
    if (exitPair->size() != 2) { … return halves; }
```

两条缺一不可，各修一个假阳性：

- **只看图侧会错**：任何 `f(x, y) = (x, y)` 的出口都是两元 `Fill`，它不是"配对"，只是元组。
- **只看签名侧会错**：`f(x) = x` 的 `count == 1` 是奇数、被拒，但 `f(x, y) = (x, y)` 的 `count == 2` 且出口两元，
  签名侧**认它**——它真的就是"一个值进、一个对出"的形状。作者 2026-10-29 已经判过这件事该这样：
  > 用户手写一张和提升图一模一样的图，本来就该被同样对待，把它"误认成"提升图正是对的。

  （`docs/handover/reviews/questions-2026-10-29.md`，2026-11-03 18:40 那条。）

所以判据落地成一个新函数，**两条一起问**，名字按它回答的事情取：

```cpp
/// Whether this graph answers the pair (value, tangent) that `split` cuts -- by its content: its exit
/// is a two-member tuple fill and its parameters come in two halves of equal length. No record of
/// where the graph came from is read: a hand-written graph of the same content is the same function.
bool answersAPair(const camel::runtime::GCGraph *callee, std::string *why = nullptr);
```

放在 `modules/transforms/split.h` 里、`exitShapeOf` 的邻居，因为它就是 `exitShapeOf` 的图侧加
`splitHalvesWith` 的签名侧，两样都是 `split.cpp` 已经有的东西，没有第三份判据要维护。

### 同形的图有几种，判定次序

`split` 自己已经把这三种收在一个入口里，**次序照抄它的**，不要另立一条：

`split.cpp#buildSplitGraph`：先 `exitShapeOf`，`ExitShape::Loop` **在 `residualsOf` 之前**就分派给
`cutLoopGraph`（循环的残差是前一半织的带子，图里还没有那张残差元组，先去读它会读空），
其余三格进 `switch`：

1. **`Straight`** —— 出口就是那个两元 `Fill`。切的是直线体，`copyHalf` 两趟走完。
2. **`Loop`** —— 出口是 `loopBehindExit` 认出来的"两条链各自投影同一个 loop 的 0/1 分量"，
   或者出口节点本身就是 `Loop` 且 `node->dataType() == funcType()->exitType()`。走 `cutLoopGraph`。
3. **`Branch`** —— 出口是 `Join`、类型是函数的出口类型、且 `branchConditionOf` 认得出条件。走 `cutBranchGraph`。
4. **`NotAPair`** —— 不是这类被调函数，保持今天那条路。

**但判据函数只回答 1–3 加签名侧；"切得动切不动"是 `buildSplitGraph` 的回答。** 这两件事必须分开记，
否则判据会把"形状对、切不动"（`Branch` 今天切不进臂）当成"不是配对"，而那不是事实。
施工时判据通过、`buildSplitGraph` 拒绝的情形，**拒绝理由原样进 `os`**（它已经有 `refusedWhy`），
调用点按"这条调用转不过去"处理，不许静默退回按出处那条路。

---

## 二、三处分别是什么

### 第一处：判据 —— `answersAPair`（新，`split.h`/`split.cpp`）

见上一节。调用点：`transpose` 的两处（第三节）、`split` 的三处（第三节第三段）。

### 第二处：做 `split` —— `buildSplitGraph`

```cpp
SplitGraph buildSplitGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);
```

（`modules/transforms/split.h#buildSplitGraph`。）产出（`split.h#SplitGraph`）：

| 字段 | 是什么 |
| --- | --- |
| `forward` | 前一半的图：**值进**，答 `(result, lin)` |
| `backward` | 后一半的图（`lin`）：**切线进**，答切线；它读的每个值都从自己的闭包里来 |
| `program` | 这两张图所在的程序 |
| `residuals` | 跨切线的值有几个（读数 `residuals=`），图算出来的，不是签名 |
| `refused*` | 没切成时的出处与理由（`refusedOrigin` / `refusedConstruct` / `refusedWhy`） |

前一半的出口类型是 **`(result, halves.backward)`**——第二格不是残差元组，是**闭包好的 `lin` 值**：

```cpp
    TupleType *forwardExit = TupleType::create(std::vector<Type *>{result, halves.backward});
    halves.forward         = FunctionType::create(withParams, forwardParams, forwardExit)
```

（`split.cpp#splitHalvesWith`。闭包由 `split.cpp#buildLinearClosure` 填：`GCFillKind::FunctionClosure`，
`with` 输入就是残差，按 `residualsOf` 的次序。）**这一点第三节要用到，也是本次唯一的硬墙。**

### 第三处：转置后一半 —— `buildTransposeGraph` / `buildTransposeMember`

```cpp
TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);          // split.h 的邻居，transpose.h
bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, GraphId member, std::ostream &os,
    const std::vector<bool> &participates = {});                    // transpose.cpp 内，两处调用点共用
```

顶层走 `buildTransposeGraph`（`transpose_operator.cpp#transposeKernel` 就是它的值层调用者）；
`transpose` 自己在走体、遇到一条调用要转时走 `buildTransposeMember` 并**共用 `state.callees`**
（一个被调函数一个成员，同一个 `TransposeState` 里只建一次）。后一半 `lin` 的参数全是线性输入，
所以 `participates` 传**空**（空 = 每个参数都参与），不要造一套全 true 的向量——空就是那个意思。

### 调用点怎么改写

对被调函数 `callee`（判据通过）执行：

1. `SplitGraph S = buildSplitGraph(context, program, callee, os)`；`S.forward == nullptr` → 按名字拒绝（第二节末句）。
2. 建 `S.forward` 的成员：`state.callees` 里按 `callee` 的 id 记一次，
   `buildTransposeMember(state, S.backward, member, os)`——**转置的是 `S.backward`，不是 `callee`**。
3. 调用点这条 `FUNC` 节点的实参，从"原调用的实参"变成"成员自己的签名要的东西"：
   `transposeSignature(S.backward->funcType(), {}, &answersOf, &capturesOf)` 定形，
   `capturesOf` 就是 `state.calleeNeeds[S.backward]`——后一半读的那些值。
4. `ct`（成员的最后一个实参）就是这条调用的余切 `local[ref]`，和今天一样。
5. 回答的 `answerCount` 个余切按 `answersOf` 的顺序落到**原调用的切线实参**上
   （即 `lin` 的形参在原调用里的位置）。

第 3 步是这次改动的收益所在：`lin` 读的值**从它自己的闭包里来**，不是从调用点的实参位置上取。
`transpose` 已经有这条通路，且它就是"按身份"而不是"按位置"：

```cpp
                            // The parameter's own value, by **identity**: the closure slot that is this
                            // parameter's value, and the port there is the value. Which parameter
                            // this is decides the slot …
                            const size_t slot   = at;
                            Type *const askType = function->funcType()->normTypeAt(at);
                            …
                            camel::core::type::typeEquals(slotMember->dataType(), askType)) {
                                forwardOf[ref] = closurePorts[slot];
```

（`modules/transforms/transpose.cpp#buildTransposeMember` 的 `forward` Port 格。）

### 唯一做不到的一处，明写在这里

**第 3 步的 `capturesOf` 在调用点取不到。** `lin` 要的值是残差，残差是**被调函数前一半算出来的**
（`residualsOf`：出口的值那一侧到过、切线那一侧也读的值）。调用点手里只有这次调用的实参；
前一半算出来的那些（`f(xs) = xs[1]*xs[0]` 里的 `xs[1]`、`xs[0]`）**不是实参，是实参算出来的**。
而前一半的出口是 `(result, lin)`——`lin` 已经闭包好了，**残差元组不作为数据露出来**，
所以调用点没有任何合法入口把 `capturesOf` 填上。

两条补法，都得作者定一条：

- **（甲）前一半的出口把残差露出来**（`(result, residuals)`，`lin` 由调用点自己闭包），
  调用点就能按 `Residual::node`/`Residual::parameter` 把 `capturesOf` 逐个填上。
  代价：`Vjp<F>` 的**可见形状变了**（`(values…) => (result, (ct) => …)` 是作者 2026-11-02 11:30 定的、
  `stdlib/types.cml#Transpose` 是它的契约），这要作者点头。
- **（乙）`transpose` 学会读闭包值**：调用点拿前一半答的 `lin` 值，把它的闭包原样交给成员
  （成员自带闭包端口，`transpose.cpp` 已经会声明 `appendClosureNode`/`setClosureType`），
  残差一步都不经过调用点。代价：调用图里要有一条"调一个闭包值"的节点，今天 `FUNC` 只认
  `GraphId`，闭包调用是 `jvp`/`vjp` 都在用但 `transpose` 没建过的形状——**这一条要先取一次读数**。

**（乙）更贴合裁定的精神**（残差跟着函数值走，闭包是它的身份，不是调用点的约定），
而且它不需要改 `Vjp<F>` 的形状。**建议按（乙）施工，并把（甲）留在页里备查。**

---

## 三、两处调用点，与 `liftedFrom` 要删的那几段

### 调用点一：直线体（`transpose.cpp#buildTransposeMember` 的 `Func` case）

今天是三格：记录的内核（`tangentArgs > 0`）、`primalId` 有效（只有臂那条路有）、其余按
`tangentSpaceOf` 判。**直线体这一格没有 `liftedFrom` 分支**——`grep liftedFrom modules/transforms/transpose.cpp`
只有一处，在臂里。所以直线体遇到"答配对的被调函数"时，今天走的是最后那一格，
对它自己（提升图）做成员，然后按参数位置投递余切：**这就是本车道一直在量的那个缺陷的形状。**
新增一格插在记录的内核之后、`tangentSpaceOf` 那格之前，用第二节的改写。

要动的地方（全部在 `transpose.cpp#buildTransposeMember` 的 `Func` case 内）：
判据一处、新格一处、`state.callees` 记一次；**没有要删的行**。

### 调用点二：分支臂（同一函数，臂的那段）

要删的就是按出处那一支，按符号指：

- 常量 `const GraphId primalId = callee->liftedFrom();` 及其上方注释
  （"A lifted call is transposed **through the graph it was lifted from** … `kGraphRefLiftedFrom`"）；
- `else if (rt::isValid(primalId)) { … }` 整格，包括里面的
  `calleeToTranspose = primal;`、`valueCount = primalParams; tangentBase = primalParams;
  answerCount = primalParams;`、以及那段注释（"A primal's parameters are the values the lift was applied to…"）、
  还有 `primalParams`/`primal` 的声明与 `lift=primal` 的 `arm-stop` 报文；
- 以及 `calleeToTranspose` 这个变量本身：三格并成一格之后，"转置谁"由判据给，不再是一个可变变量。

删完 `transpose.cpp` 里**不再有** `liftedFrom`（今天仅此一处）。

### 第三处：`split` 里"按出处改调用目标"（`split.cpp`）

三处读者，**都改成按内容**：

1. `split.cpp#Half::retargets` —— 整个函数换掉：判据从 `callee->liftedFrom()` 换成 `answersAPair(callee)`；
2. `split.cpp#Half::makeCopy` 的 `Func` case 里 `if (retargets(ref)) { … primal … }` 那一段 ——
   **改写方式与 `transpose` 不同，别照抄**：前一半要的是"这次调用的值分量"，
   按内容它就是**被调函数答的配对的第一格**，所以前一半**留着这条调用**、再取 `ACCS TupleIndex 0`，
   不需要"改调用目标"这回事，也不需要再切一次（再切就是无限递归）。
   代价：前一半会把切线那一路也算一遍（前一半不读它，DCE 掉），读数 `residuals=` 不变
   （残差是从**源图**读的，不是从半边读的）。
3. `split.cpp#residualsOf` 里的 `isLiftedCall`（两处调用：值跨切线时的走法、以及跨切线集合的过滤）——
   同一个判据。注意它今天照着 `arguments.size() / 2` 取"前一半实参是值"，那是**按位置的约定**，
   与本次要消灭的是同一个思路；换成按内容之后，这里要问的是"这个调用的值是哪个实参"，
   而那个答案在 `jvpSignature` 的形参清单里（值与切线各半），不在 `size()/2` 这个算式里。

删完这三处，`kGraphRefLiftedFrom` 没有读者，再按裁定删记录本身（**这一条是下一批第 6 项，本次不删**）。

---

## 四、风险：哪些现有用例会因此改变行为

判据按内容，手写的同形函数就**会**被拉进来（作者说这正是要的）。逐条列出我认为会动的：

| 用例 | 为什么 |
| --- | --- |
| `test/cases/modules/autodiff/jax_has_aux.cml` | `loss(): (float, float)` 是手写配对，但**只有一个参数**（`count == 1` 奇数）→ 签名侧不过判据，**不动**。这条要钉住，它是"手写配对"最容易误伤的形状。 |
| `test/cases/modules/autodiff/grad_tuple_read.cml` | `pair(x, y): (float, float)`：两参数 + 两元出口，**过判据**。今天只是被调用、没被转置，所以不动；但它是"会被当成提升图"的第一个真实形状，将来它若出现在被转置的体里就会走 split 那条路。 |
| `test/cases/compile/transforms/transpose_tuple_both_slots.cml` 等 `transpose_tuple_*` | `both(x: float): float` 出口是 `float`，元组在**体内**——判据看的是出口，不是体，**不动**。 |
| `test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml` | 它的墙正是"出口是**一个调用的对**，不是两元 `Fill`"，`exitShapeOf` 给 `NotAPair`，裁定用的同一个判据，**仍然红，且仍然以同一个理由红**。不许因为这条改动把它弄绿。 |
| `test/plans/feat/transforms/split.plan.toml`（`split_call_one` 那条） | 它的 `desc` 明写"值半边读 `kGraphRefLiftedFrom` 这条记录、据此改调 `sq`"。按内容改成"留着调用取第 0 格"之后，**数字与 `residuals=` 都不变，但那条 `desc` 与 `REINTERP` 报文里的 `liftedFrom=` 要改**（`jvp.cpp` 的读数是唯一印它的地方，计划里没有断言它，只有这一条 `desc` 提到）。 |
| `test/cases/compile/transforms/transpose_call.cml`、`split_call_one` 对应的 `transforms` 条目 | 直线体一旦遇到"被调函数答配对"，就从"转置它自己"变成"切它、转置后一半"。**读数会多出 `split`/成员的建图行**，数字应当不变；断言 stderr 文案的条目要跟着改。 |
| `vjp_loop_*` / `transpose_loop_*` | 循环那条路 `ExitShape::Loop` 先分派 `cutLoopGraph`，与本次同一判据、不同切法。数字应当不变；风险在 `residuals=` 与带子的读数会动。 |
| `matmul_transpose_*` / `transpose_array_product` | `matmul(da, b)` 是记录的内核形状（`tangentArgs`），不是配对 → **不动**。 |

**哨兵**（裁定给的）：`split` 19/19、`transforms` 不新增红、库层 `with_grad` 在 `grad_first_with_parameter`
上与引擎同答。第三条是本次唯一能证明"按内容"没走歪的那条——手写的 `with_grad` 与引擎的
`with_grad` 走的是同一条 composition，只有判据对了两边才同答。

---

## 五、给作者的一句话

**有出入，两处，一处是硬墙。**

1. **硬墙**：裁定的"切它、转置后一半、把调用转过来"在**调用点**少一环——后一半要的残差是前一半算出来的，
   而前一半的出口是 `(result, lin)`，残差被 `lin` 的闭包包住了、不作为数据露出来，调用点无从填。
   要不（甲）让前一半的出口把残差露出来（改 `Vjp<F>` 的可见形状，要您点头），
   要不（乙）让 `transpose` 学会"调一个闭包值"（内部改动，建议这条）。
2. **不是硬墙但与裁定的字面不同**：`split` 里"按出处改调用目标"那一处，改成按内容之后**不再"改目标"**——
   前一半留着那条调用、取配对的第一格；照字面"改成按内容改目标"，前一半会再切一次被调函数，
   那是无限递归。这一点写进了第三节第三段。
3. 另外：判据"答配对"落地成 **图侧（出口是两元 `Fill`）+ 签名侧（形参偶数、出口两元）** 两条同时成立，
   **不是**单看 `exitShapeOf`。只单看一侧会把 `f(x, y) = (x, y)` 这种手写元组也拉进来——
   作者说拉进来是对的，但那要两条一起才拉得**准**（`(x, y) => (x, y)` 与 `(float) => (float, float)` 不是一回事）。

---

## 附：本页引用的符号

| 符号 | 位置 |
| --- | --- |
| `exitShapeOf` / `ExitShape` | `modules/transforms/split.cpp` |
| `splitHalvesWith` / `SplitHalves` | `modules/transforms/split.cpp`、`modules/transforms/split.h` |
| `buildSplitGraph` / `SplitGraph` | `modules/transforms/split.cpp`、`modules/transforms/split.h` |
| `residualsOf` / `Residual` | `modules/transforms/split.cpp`、`modules/transforms/split.h` |
| `buildLinearClosure`（匿名命名空间，要提出来） | `modules/transforms/split.cpp` |
| `Half::retargets` / `Half::makeCopy` / `isLiftedCall` | `modules/transforms/split.cpp` |
| `buildTransposeGraph` / `buildTransposeMember` / `transposeSignature` | `modules/transforms/transpose.cpp`、`modules/transforms/transpose.h` |
| `transposeKernel`（值层的 `transpose`） | `modules/transforms/transpose_operator.cpp` |
| `jvpSignature` 的形参次序（值与切线各半） | `modules/transforms/jvp.cpp` |