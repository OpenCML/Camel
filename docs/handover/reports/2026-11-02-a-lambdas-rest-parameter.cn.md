# 2026-11-02：lambda 的剩余形参——解析进、拒得出，以及它今天停在哪一级

这一页记三件事：诊断码的两种写法（新纪律）、`(..xs) => body` 这一笔的读数，以及
`addRestArg(pack)` 与 `addNormArg(pack)` 的实测——后者是"lambda 的形参表要用哪一个语义"的答案，
也是 `vjp` 并成一条之前还没有跨过的那一格。

## 1. 注册表码与打印码：引用时必须说清是哪一个

同一个诊断有两种写法，混着引会被当成笔误（2026-11-02 负责人就怀疑过 `0x000209` 是截断）：

- **注册表码**：`diagnostics/registry.tsv` 里那一列，只有**后 6 位**，如 `0x000209`、`0x000191`；
  `include/camel/core/error/diagnostics/messages/*.h` 的枚举也是这一种（由脚本生成）。
- **打印码**：报告里的 `code=0x…`，由
  `makeDiagCode(type, specific) = (type << 24) | specific` 合成，类型前缀见
  `include/camel/core/error/diagnostics/base.h`（`SemanticDiag = 0x03`）。
  于是注册表 `0x000191` 打印为 `0x03000191`，`0x000209` 打印为 **`0x03000209`**。

写"新增一个码"时给出注册表码，写"贴报告原文"时给出打印码，两者不要互相代入。

## 2. `(..xs) => body`：昨天的形状是崩溃，今天是具名诊断

- 解析：`lambdaAhead()` 认 `(..` 开头（仍以 `)` 之后的 `=>` 或返回类型的 `:` 判定），
  `parseFuncData()` 给形参表 `allowRest`。
- 语义：`builder.cpp` 里那句
  `throw std::runtime_error("A rest parameter in a lambda.")` 换成
  `SemanticDiag::LambdaRestParameterNotLowered`（注册表码 `0x000209`，打印码 `0x03000209`），
  位置取 `paramLoad->tokenRange()`。
- 读数：`.../lambda_rest_untyped.cml:9:18: [Error]: The rest parameter '..xs' of a lambda is not
  lowered yet. …`，退出码 1；A/B 换回 `throw` 时两条用例红，报文是
  `camel: internal error: A rest parameter in a lambda.` + `This is a defect of the compiler, not an
  error of the program.` ——**内部错误与程序错误的分野，就是这条诊断存在的理由**。
- 账面：`--check` 306 → 307（`edit` 15 不变、`reason` 291 → 292）；`--verify-doc` 干净；
  `--coverage` `0 of 307`；`--name-coverage` `5 of 239 (recorded 5)`，分子不变、分母 +1。

这一笔**不改变任何可运行程序的行为**：没有 `..` 的 lambda 一个字没动（`vjp_api` 9/9、
`transforms` 45/45 不变）。

## 3. `addRestArg(pack)` 与 `addNormArg(pack)`：实测，两者都不是 lambda 的答案

两边今天各自被谁用（`git grep`）：

- `addRestArg(ident, pack)`（`func.h:91`）：一个槽 + `hasRest_`，调用处写 `..args` 才展开，
  体内看到的是静态元组。用在**声明**（`builder.cpp:7140`）、签名拷贝（`resolver.cpp:164`、
  `defun/defunctionalize.cpp:347`、`retype.cpp:86`）、类型变量替换（`typevar.cpp:309`）。
- `addNormArg(name_i, pack, false)`（`builder.cpp:7095`，`packOf` 的那条路）：一个槽，槽的类型是
  包本身。`typevar.cpp:331` 对同一件事做另一件事——把包**摊成每个成员一个形参**
  （`addNormArg(name + "_" + k, members[k], false)`）。

"摊平"这一侧已经有绿用例钉住：`test/cases/compile/type_packs/function_pack_params.cml` 里
`type Vjp<F> = (..ParamsOf<F>) => (ResultOf<F>, Pullback<F>)`，并断言
`is_same::<Vjp<F3>, (a: int, b: string, c: bool) => …>` ——**函数类型里的包就是一张形参表**。

把 `packOf` 对 `open` 放开（`if (open || signature)` → `if (signature)`）之后实测三条，全部失败：

1. `(..xs) => 0.0`（无类型）：`[Error]: The generic function '' cannot be given its types here:
   nothing here says which function is wanted …` —— 早先那条具名诊断被挤掉了：`packOf` 走到
   `signatureVisit_` 分支，把 `xs` 绑成了类型变量，lambda 变成无法实例化的泛型。
2. `(..xs: float[..]) => 0.0`：`camel: internal error: Runtime graph 'main' has an undetermined
   type: '..float[..]' in its static slots ('(void, (xs_0: ..float[..]) => float pure, int)')` ——
   包原样进了运行时图。
3. `(..ys: ..ParamsOf<F2>) => 7`（`F2 = (a: int, b: float) => float`）：
   `[Error]: Function of type '(ys_0: ..pack<int, float>) => int pure' cannot be called with
   argument types: <> (int, float)` —— 包是**一个**槽，`ys_0`，调用没有把它摊开。

**结论**：`addNormArg(pack)` 给出的是"一个槽带一个包"，摊平发生在**函数类型被实例化**的那一步
（所以 `Vjp<F3>` 与写出来的形参表 `is_same` 为真）；lambda 的形参表**没有那一步**——它没有调用处、
也没有"它站在哪个函数类型里"的读数，于是包活到图上，或者调用无法展开。`addRestArg(pack)` 是另一个
世界：那条路要求调用处写 `..args`，体内拿到静态元组，那是声明与 `RestValue` 的事情。

**所以缺的是**：lambda 的形参表要**按它所在位置的函数类型摊平**——这正是 17:30 停下的那件事
（"前端在建图时替它建形参"）的实测形态：今天没有那个时刻，两个方向都会落进内部错误或无法调用。
`vjp` 停在**这一级**：`(..ParamsOf<F>) => …` 作为**类型**已经可用（`Vjp<F>` 有绿用例），作为
**lambda 的形参表**还没有；`..xs` 的取值（元组）是再下一格 `NK::RestValue`，`src/compile` 里至今
没有它的下降路径。

这一节的实测没有落地任何代码：试完即回退。

## 3½. 18:50 裁定：实现，不是拒绝（覆盖上文"相撞"的定性）

作者 2026-11-01 18:50 直接裁了这一格，方向比本页的默认建议更实：

> **`LambdaRestParameterNotLowered` 是"暂不支持"诊断**——`858e9d409` 把崩溃换成了具名诊断，比崩溃
> 好，但文案 "is not lowered yet" 属于明令禁止的那一类。16:10 已经裁定 lambda 收 `..xs`，**类型取自
> 期望类型**；**下一笔就是实现它**，这条诊断在同一笔里删掉。
>
> **按期望类型展开成具名形参**：实例化时 `ParamsOf<F>` 已经具体，`..xs` 就是那几个形参。

所以上文第 3 节"两条路都撞墙"的结论要改一句话：**它不是撞墙，是缺了一个可用的更早时刻**——lambda
建形参时要读**期望类型**，而 17:30 停下的门槛（"建图时替它建形参"）由这条裁定放开。第 4 节"`vjp` 走
返回类型、不走形参表"也随之作废：`vjp` 并成一条泛型就等这一步。

**落地要点（下一笔）**：`(..xs)` 展开成 `(xs_0: T1, xs_1: T2, …)`，每一格来自 `ParamsOf<期望类型>`
的第 k 个成员；`(..xs: ..ParamsOf<F>)` 用写出来的那个包；`(..xs: float[..])` 的每一格是 `float[..]`。
`..xs` 作为**元组**在体内取值仍是 `NK::RestValue`（`src/compile` 里没有下降路径），那是与"形参表"相邻
的另一格——`vjp` 的体用不到它，所以并成一条不必等它。

## 4. 结论：`Vjp<F>` 作为类型可用；作为 lambda 形参表——18:50 裁定后由"不可用"改为"下一笔实现"

**`Vjp<F>` 作为类型可用**（`d8105565a` 已落地）：`Pullback<F>`、`Vjp<F>`、`Jvp<F>`、`Transpose<F>`
写进 `stdlib/types.cml`，用的全是 `types` 已有的词（`ParamsOf`、`ResultOf`、`Tangent`），**没有给类型
模型加任何字段**——原则 2.3 的正面样板，也是 P1-12 里"从内容推得出"的那一半：

    export type Pullback<F>  = (ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)
    export type Vjp<F>       = (..ParamsOf<F>) => (ResultOf<F>, Pullback<F>)
    export type Jvp<F>       = (..ParamsOf<F>, ..Tangent<ParamsOf<F>>) => (ResultOf<F>, Tangent<ResultOf<F>>)
    export type Transpose<F> = (..ParamsOf<F>, ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)

顺带纠了一处假绿：原来那个用例自己写 `type Tangent<T> = T` 当替身，于是它把 `F3` 的拉回钉成
`(ct: float) => (int, string, bool)`；用真的 `Tangent`，一个 `int` 的切空间是空元组，答案是
`((), (), ())`。改的正是那两行**量替身**的断言。

**`..xs` 作为 lambda 形参表不可用**：卡在被暂停的"按所在函数类型替 lambda 建形参"上（第 3 节的两条
内部错误）。**所以 `vjp` 该走返回类型，不走形参表。**

但"走返回类型"只解决**声明**，解决不了**体**：写成一条泛型声明并 `return transpose(split(jvp(f)))`
时，墙上原文是

    error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types:
      <> (((float) => (float, ()), ((), float) => float)).
      --> stdlib/vjp.cml:60:22   (code=0x0300000F)

—— `split(jvp(f))` 是**一对函数**，`transpose` 要的是**一个函数**（`is_function::<F>`）。把它们接起来
（forward 半边的残差喂给 transpose 的线性半边）必须由一个形参表是 `..xs` 的 lambda 来做，也就是回到被
暂停的机制上。于是 `vjp` 今天仍是**两条**声明（元数 1 与 2）；一条泛型的路要等那台机制，或等一个
不需要 lambda 的接法。

**这是 P1-12 剩下的那一半**：类型已经能在 `.cml` 里写出来并可被引用；`vjp` 的**签名**仍在 C++ 里，
因为一条泛型声明需要一个 `(..xs)` 的体。

## 5. 期望类型其实已经在现场——在 GIR，不在 `visitFuncType`

第一段的量法（"把期望类型传进 `visitFuncType`"）量出了**相反**的结论：**不用传，因为设计早就把这件事
放到 GIR 那一级了**。

`visitFuncType` 的全部调用点（`src/compile/gct/builder.cpp`，共 2 处，另 1 处是定义）：

| 行 | 是什么 | 那里知不知道期望类型 |
|---|---|---|
| 5243 | `func f::<T>(…)` 的**声明签名**（`declSignature_ = true`） | 不知道——它**就是**那个类型；且 `..xs` 在这里走 `addRestArg`，根本进不到 lambda 那一支 |
| 6529 | `visitTypeExpr` 派发 `TypeType::Func`：写在**类型位置**的函数类型（注解、泛型实参） | 不知道——注解本身就是类型，没有"期望"可言 |

**结论一句话**：要改的调用点是 **0 个**；能传的也是 0 个——因为真正的展开点在
`src/compile/gir/builder.cpp:1143-1165`，那里 `expected` 已经**在手上**：

    // A lambda takes the types it does not write from the function type of the position it
    // stands in (design principles 5.9). That expected type reaches this point through the one
    // channel of the expected type (`expected`): an argument to a parameter of function type,
    // the value of a declared type, or the result of a function whose result type is a function
    // type.
    if (expected == nullptr) { … LambdaParameterWithoutType … "nothing here expects a function type" }

也就是说：**省略类型的 lambda 形参**（`(x) => …`）早就靠 `Tree::UnwrittenParam{index, origin}` 记在
FUNC 节点上，由 GIR 按 `expected` 填；`..xs` 可以走**同一条通道**——它是"位置 k 上没写类型、且是最后一格"
的那种形参，展开规则就是"取 `ParamsOf<expected>` 的第 k 个及之后的成员"。

**这带来两个好处**：① 不需要给 `visitFuncType` 加 `Type *expected`（少一个参数、少两处传递）；
② "这里没有期望类型"的文案**已经存在且是对的**——`"nothing here expects a function type"`，它说的是
**此处不可能**，正是 18:50 裁定允许留下的那一类；`(..xs)` 没有期望类型时用它，而不是 `is not lowered yet`。

**所以判据 1 的表是一张反表**：原来打算"逐点传"，实测是"一个点都不用动，换到 GIR 那条已有的通道上"。
`..xs` 的元组取值（体内 `..xs`）仍是 `NK::RestValue`，与形参表相邻、不挡 `vjp`。

## 6. 19:10 裁定与它量到的那一格

作者 2026-11-01 19:10（M4）：**lambda 的 `..xs` 不在暂停范围内**——暂停针对的是"前端里一张按算子名查的
变换表"和"实参必须编译期已知"，而 **lambda 的形参按期望类型定型是语言本来就有的规则**（原则第 5 节：
`let f: (x: float) => float = x => …` 的 `x` 就是这么得到类型的）；`..xs` 只是这条规则**用在包上**。
所以本页第 3 节"相撞"的定性作废，第 3½ 节的裁定仍然成立，只是理由换成这一句。

**已落地（`d025d5c25`）**：吸收位 + 三形式展开，`rest_params` **15/15**；`LambdaRestParameterNotLowered`
删除，`--check` **306 → 305**；`--pinned-coverage` 同笔复核 **288 of 305**、地板仍 16。
`(..xs)`／`(..xs: float[..])` 在没有期望类型的位置给出 `"nothing here expects a function type"`
（此处不可能），在有期望类型的位置（实参位：`apply((..xs) => 0.0)`、`apply_one((..ys: float[..]) => 1.0)`）
**建得出、跑得通**（新用例 `lambda_rest_annotated.cml`）。

**`vjp` 并成一条：试过，卡在"体内取值"这一格。** 一条泛型的体是

    let (forward, linear) = split(jvp(f))
    return (..xs) => { let (value, residuals) = forward(..xs) … }

**签名这一半通了**（形参表按 `Vjp<F>` 的 `ParamsOf<F>` 展开），**体这一半不通**：

    error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'xs'.
      [in the instance 'vjp<(x: float) => float>' of the generic function 'vjp' with F = (x: float) => float]
      --> out/latest/stdlib/vjp.cml:72:44

原因：`..xs` 在**调用**里（`forward(..xs)`）走的是"展开一个编译期元组"那条规则，**要求 `xs` 是一个值**；
而 lambda 的 `..xs` 今天只展开成**形参名**（`xs_0`, `xs_1`），**没有一个叫 `xs` 的元组值**。
`vjp` 的体需要它，所以 `vjp` 并成一条要等 `NK::RestValue` 的下降（`..xs` 作为元组取值的路径），
那是与"形参表"相邻的另一格。**不是暂停、不是相撞**，是那一格还没做。

## 7. 实测三样（欠账回收，含一处用词更正）

**① 用词更正（显式作废）**：先前写的是"`NK::RestValue` 终于被下降"。**这句作废。**
`git grep -c RestValue -- src/compile` **改前 0、改后 0**——我们造的**不是** `RestValue` 节点，
而是 **`GCFillKind::Tuple` 的 Fill**，成员是它的 **`With` 输入**。
摊开要的是"这是要被摊成实参的元组"，**`With` 是那条边，`Norm` 是"这是一个值"**——
`expandRestArgument`（`src/compile/gir/builder_rest.cpp:80-118`）只认两种：
build-time tuple，或 **With 输入个数等于元组长度的 tuple FILL**。

**② `xs[0]` 实测**：**是具名拒绝，不是"到不了"**。
`apply((..xs) => xs[0])`（期望 `(a: float, b: float) => float`）得到

    No matching function found for argument types: <> ((float, float), int);
    operator '[]' is not defined for these operand types
      <:op/idx_arr>: (arr: T[], index: int, axis: int) => T
      <:op/idx_take>: (arr: T[], indices: int[], axis: int) => T[]

即：rest 参数那个包是**元组**，而 `[]` 只对 `T[]`（数组）等有定义。要让它可索引，
是给元组加索引规则的问题，不是这一格。

**③ A/B 未测，且今天不成立**：元组对**每个** rest lambda 都建（代码里没有"体内引用了才建"的判据），
所以**不读 rest 参数的 lambda 也会多一个 Fill 节点**。这一条是**明确的欠账**，
不是"已满足"。判它需要先查出体内那个引用到底是哪个节点种类（试过的 `NREF` 走法找不到它）。

**④ 体内引用 rest 名的节点种类：未实测，记录里前后两句互相矛盾，以本句为准。**
先写过"`RestValue` 终于被下降"（作废），又写过"体内引用**不是** `NREF`"（因为那次的遍历只找 `NREF`、没找到），
再写成"`NREF`/`DREF`"。**判据代码现在两种都认**（`graphNamesValue` 收 `NREF` 与 `DREF`，遇 `FUNC` 停），
**没有读 rest 名的 lambda 现在一个节点都不建**（`if (!graphNamesValue(...)) break;`），读的仍然建。
**但"到底是哪一种"没测出来**：`std::gir` 的 DOT 只在用例失败时才进日志，探针是绿的，所以那一行没拿到。
这一条留作读数，别照抄任何一句旧话。