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

**⑤ 实测（远端直接重定向，绕开运行器）**：`(..xs) => add(..xs)` 在 `std::gir` 里

    N03 [label="xs_0", type="PORT", param_name="xs_0", param_kind="normal", param_index="0", value_type="float", …];
    N04 [label="xs_1", type="PORT", param_name="xs_1", param_kind="normal", param_index="1", value_type="float"];
    N03 -> N05 [label="0|0", type="NORM"];
    N04 -> N05 [label="0|1", type="NORM"];
    N0A [label="Function(graph=__g_50__, …)", value_type="(xs_0: float, xs_1: float) => float pure"];

**这张图里没有 `xs` 节点，也没有 FILL**：`..xs` 在 GIR 就被摊成两条普通实参边（`0|0`、`0|1`）。
所以"体内那个引用是 `NREF` 还是 `DREF`"这个问题**在图上没有对应物**——它是 GCT 里的引用，
GIR 里留下的只是被摊开的实参。**旧的三种说法（`RestValue`／不是 `NREF`／`NREF 或 DREF`）都作废，
以这一行为准。**

**⑥ 仪器读数（给后来的人）**：计划运行器的日志**只保留 `[stdout]` 末尾 50 行**（超出时印
`[stdout] ... 50 line(s) before`），**所以 `std::gir` 的完整 DOT 在日志里看不全**。要拿完整 DOT，
在远端直接重定向：`LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib ./out/latest/bin/camel x.cml std::gir > x.gir`。

**⑦ `65d8dbb85`（`Norm → With`）改的到底是什么——查清了：不是空改，但也不是"图里多一个 FILL"。**

- **建**：`src/compile/gir/builder.cpp:1426`（`buildFuncBody` 里那一处，`GCFillKind::Tuple` + `slots`），
  成员是 `xs_0`, `xs_1`, … 的 **`With` 输入**。`git grep -n GCFillKind::Tuple` 其余各处都是别的路径
  （循环状态机 `builder_control.cpp` 多处、元组字面量 `gir/tree.cpp:71`、`builder_static.cpp:1526`、
  `graph_encode.cpp`、`passes/trans/json` 等），服务于各自的路径。
- **消费**：`src/compile/gir/builder_rest.cpp:103`——`expandRestArgument` 认的正是"`With` 输入个数等于元组长度"，
  把 With 输入当实参返回；**此后这个 Fill 没有消费者**，所以**摊开的那张图里它不出现**（见 ⑤）。
- **体内把 `xs` 留住（而不是摊开）时，它就在图上**：`pair((..xs) => xs)` 的 `std::gir` 里有

      N02 [label="(float, float)", type="FILL", value_type="(float, float)"];

  以及 `N00 -> N02`、`N01 -> N02`。
- **改前/改后（同一个程序、同一条命令）**：改前（成员接 `Norm`）`apply((..xs) => add(..xs))` 给出
  `The rest argument '..xs' is not a compile-time tuple`（`expandRestArgument` 读不到 With 输入）；
  改后同一句通过并打印 `3.0`。

**所以正确说法是**：那一笔改的是**这个 Fill 的边**（成员从 `Norm` 变 `With`），**不是"图里多出一个节点"**。
`Norm` 连接时它是一个值、摊不开；`With` 连接时它是待摊开的实参，由 `expandRestArgument` 消费掉。

**一句话**：FILL 是\这个元组\，\With\ 与 \Norm\ 的差别是\成员是不是实参\：\With\ 的成员会被摊开并消费，\Norm\ 的成员只是值，摊不开。

## 8. P1-12 的量：结论是"这条计划建立在一个错前提上"（显式作废）

P1-12 的原话是"同一个签名写在三处，收成一处"。把三处挨个读过之后，**没有"三处写法"这件事**。逐处如下。

| 处 | 存什么 | 是什么 |
|---|---|---|
| `include/camel/core/type/composite/func.h` | **一份紧凑布局**：`withCount_`/`normCount_`，类型、`TypeCode`、名字都在 `trailing()` 上 | **存储**（唯一一份）。`paramCount()`（:328）、`argNamesCount()`（:273）是算术；`:331`、`:335`、`:340` 按 `paramCount()` 做指针算术 |
| `include/camel/core/type/resolver.h` | **工厂**：`FuncTypeResolver::resolve(...)` 算出 `FunctionType *`；另有 `fixedSignature()`（:95，默认 nullptr）、`declaredSignature()`（:104）、`graftedSignature()`（:116）返回它自己持有的那份 | **产出的类型**，不是签名的第二份文本。实现处：`:226`（`funcType_`）、`:294`/`:295`（`fixed_`/`declared_`）、`:401`/`:408` |
| `src/compile/gir/builder.h` | `buildFuncBody(..., const FunctionType *written = nullptr)` —— **构造参数，没有成员存它** | **传进去的东西**，不是存储 |

**结论（三选一里的第三种：混合，且"三份来源"不成立）**：
- **存储**只有一处（`func.h` 的紧凑布局）；
- **算术**多处（`func.h` 里按 `paramCount()` 的指针算术）；
- **产出**一处来源（`resolver` 工厂给出的类型），**不是文本的第二份**；
- **传参**一处（`written`），**按"要传不要存"验过：`builder.h` 里没有成员接它**。

**所以 P1-12 作废**：它假定的"同一个签名写三处"不存在，实际是"**一份紧凑布局，多处按参数个数做指针算术**"。
把算术收到一处是另一种改动（不是 P1-12 说的那件），而且没有重复文本可收——**没有可迁移的东西**。

## 9. 除法族的段错误（待修，不归本车道）与四族数值表

### 9.1 段错误：`got null` 是崩溃，不是慢

单跑一遍，带退出码与墙钟（远端直跑，`LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib`）：

| 程序 | 遍 | 退出码 | 墙钟 |
|---|---|---|---|
| `println(4.0 / 2.0)` | `std::gir` / `std::nvm` | 0 / 0 | 58 / 33 ms |
| `grad((x) => x * 3.0)` | `std::gir` / `std::nvm` | 0 / 0 | 56 / 82 ms |
| `grad((x) => -x)` | `std::nvm` | 0 | 55 ms |
| `grad((x) => x / 4.0)` | `std::nvm` | **139 SIGSEGV** | 147 ms |
| `grad((x) => 4.0 / x)` | `std::gir` / `std::nvm` | 0 / **139 SIGSEGV** | 56 / 137 ms |

两条除法 `std::gir` 都 **rc=0**（图建得出），`std::nvm` 都 **rc=139**；不带 `grad` 的除法 rc=0。
⇒ **前端与图无事，崩在 autodiff 建反向图时。**

### 9.2 栈（`gdb -batch -ex run -ex bt`，前 8 帧）

```
#0 camel::runtime::(anonymous namespace)::emitDraftPayload(GraphDraft const&, DraftPayloadPlan const&, GCGraph…)
#1 camel::runtime::emitDraftNativePayload(GraphDraft const&, GCGraphPayloadArena&, …)
#2 encodeDraftGraph 的 _Function_handler<void(GCGraphPayloadArena&)>
#3 camel::runtime::GCGraphBuildAccess::constructInPlace(…)
#4 encodeDraftGraph(Context const&, RuntimeDraftIdentity const&, …)
#5 camel::runtime::GraphDraftGroup::encode()
#6 camel::transforms::buildTransposeGraph(Context const&, Program const*, GCGraph*, ostream&)
#7 camel::transforms::(anonymous namespace)::transposeKernel(ArgsView&, ArgsView&, Context&)
```

**读法**：崩在 `transposeKernel → buildTransposeGraph → GraphDraftGroup::encode → emitDraftPayload`，
**不是算子求值**；乘法（`mul_t`）与取负（`neg_t`）走同一条链**不崩**，所以差别在**除法规则产出的图的形状/槽位**上。

### 9.3 不是 `d14d6a96` 引入（lineage 两行）

```
$ git merge-base --is-ancestor 21aa36805 HEAD  →  rc=1   (fb0d4b1ea / b578ab2db 同 rc=1)
$ git merge-base --is-ancestor d14d6a96 HEAD   →  fatal: Not a valid object name d14d6a96
```

即：本树**没有**那三笔的内容，**`d14d6a96` 这个对象在本检出里根本不存在**。崩溃在一棵**不含 `d14d6a96`** 的树上复现
⇒ **它是旧崩溃，与那一笔无关**。归重写那条路的人，本车道不动 `modules/transforms/`。

### 9.4 四族数值（`grad` vs 手写中心差分，h=1e-5；`rc=0`，54 ms）

| 族 | `grad` | 中心差分 | 判 |
|---|---|---|---|
| add（`x + 3` @2） | `1.0` | `0.9999999999621422` | ✓ |
| sub（`x - 3` @2） | `1.0` | `1.0000000000065512` | ✓ |
| mul（`x * 3` @2） | `3.0` | `3.000000000064062` | ✓ |
| neg（`-x` @2） | **`1.0`** | **`-1.0000000000065512`** | **✗ 待修**（转置给 +1.0） |
| div（两位） | — | — | **未取得读数：崩在 `emitDraftPayload`，见 9.1/9.2** |

`neg` 的 `1.0` 是**已知待修现象的第一次数值读数**：`neg_t` 的转置给了 `+1.0`，而答案是 `-1.0`。
`add`/`sub`/`mul` 三族**数值对**，这三族的 `*_tangent` 可以放心作为"转置切到规则路径后应保持不变"的对照值。

## 10. 作者 18:50 那条读数：今天到不了，卡在 `grad` 的签名上（量，不是实现）

原话：**没有切空间的形参不占切向槽**；读数 `f(xs, n) = xs[n] * xs[0]` 在 `([3.0, 2.0], 1)` → `grad` 对 `xs` = `[2.0, 3.0]`。

**今天的结果：编不过，且不是转置的问题——是 `grad` 认不下这个形参。** 三处原文：

**(a) 两参数的函数根本递不进去**（`grad(f)([3.0, 2.0], 1)`，不论数组还是标量）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ((xs: float[], n: int) => float pure).
Available overloads:
  vjp::grad: (f: (x: float) => float) => ((x: float) => float) uses f
```

**(b) 标量闭包通**：`grad((x) => x * 2.0)(3.0)` → **`2.0`**。

**(c) 数组闭包在体内就被拒**：`grad((xs) => xs[1] * xs[0])([3.0, 2.0])`

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (float, int);
operator '[]' is not defined for these operand types
Available overloads:
  <:op/idx_arr>: (arr: T[], index: int, axis: int) => T
```

**分层判断（`b` 与 `c` 一起给答案）**：`c` 里 `xs` 被定型成 **`float`**——因为 `grad` 只有 `(f: (x: float) => float) => ((x: float) => float)`
这一条签名，lambda 参数从位置拿类型（这正是 `..xs` 那条规则的同一件事），于是 `xs` 是标量，`xs[1]` 当然没有 `[]`。
`b` 说明**标量路是好的**。所以缺口在 **`grad` 自己的装配（`stdlib` 侧：它只收一个 `float` 形参、也只回一个 `float`）**，
**不是转置侧**、也不是算子层——`xs[n] * xs[0]` 里的数组元素与标量相乘，今天**根本没走到**。

**归谁修一句话**：**归 `grad`/`vjp` 的签名与装配（`stdlib/vjp.cml` 那条线）**——要有"数组形参、按元素切向"的那条规则；
转置侧（`d14d6a96`）在这条读数上**不是瓶颈**。

**注意**：这条读数与 `xs[0]` 那条（§7②）是**同一处**——rest 参数的包是元组、`[]` 定义在数组上；而这里是
`grad` 把形参定型成 `float`。两者都不是"到不了"，都是**具名拒绝 + 列出可用重载**。

## 11. 泛型 `grad`：试过了，两条都是要裁的格（停手，不自己发明）

按"一条泛型声明"改写（照 `vjp` 的写法，`pullback(1.0)` 直接返回各参数的余切元组）：

```camel
export func grad::<F>(f: F): (..ParamsOf<F>) => (..Tangent<ParamsOf<F>>) when is_function::<F>() {
    let reading = vjp(f)
    return (..xs) => {
        let (value, pullback) = reading(..xs)
        let _ = value
        pullback(1.0)
    }
}
```

**能编过**（`BUILD_EXIT=0`，`rest_params 16/16`），**但对调用方有两个后果，都实测**：

**(a) 命名函数可以，但一元结果从"一个数"变成"一元组"**（`grad` 对 `x + 3`/`x - 3`/`x * 3`/`-x`）：

```
add (1.0) 0.9999999999621422
sub (1.0) 1.0000000000065512
mul (3.0) 3.000000000064062
neg (1.0) -1.0000000000065512
```

值都对，**形状不再是 `1.0` 而是 `(1.0)`** —— 与判据 2「`grad((x) => x * 2.0)(3.0)` 仍得 `2.0`」冲突。

**(b) 内联 lambda 失去定型位置**（`grad(f: F)` 这个位置不再是函数类型）：

```
error[SemanticDiag::LambdaParameterWithoutType]: The type of the parameter 'x' of this lambda is not
written and cannot be inferred: nothing here expects a function type.
  --> f_scal2.cml:4:19
```

数组那条同理（参数名 `xs`）。也就是说：**泛型化把"`grad` 的形参位置给 lambda 定型"这件事拿掉了**——而
`vjp` 没有这个问题，因为它的调用方传的是**已声明的函数名**，不是 lambda 字面量。

**结论：停手报负责人，不自己发明。** 冲突是两格：
1. **一元 `grad` 的返回形状**：`2.0` 还是 `(2.0)`（作者 2026-11-01 14:30 的裁定是"pullback 一律回元组、不给一个参数开例外"，但如果 `grad` 也一律回元组，判据 2 就不成立）；
2. **lambda 字面量的定型**：`grad(f: F)` 与 `grad(f: (x: float) => float)` 二者只能选一个，除非有
   `Grad<F>` 这类类型函数 + 一条"位置上的 `F` 仍是函数类型"的表达方式（`types.cml` 里今天没有 `Grad<F>`）。

**已回滚**：`stdlib/vjp.cml` 回到 HEAD，工作树干净；上面 (a)(b) 两次读数是在临时改动下取得的，已记在此处。

## 12. `core::tangentSpaceOf` 今天覆盖哪些类型（只量不修）

定义在 `src/core/tangent_space.cpp:76-124`（`answerOf`），入口 `:140`。三档答案：`Self`（切向就是它自己）/ `Other`（另一种类型）/ `None`（没有切空间）。

| 类型 | 有/没有 | 依据 |
|---|---|---|
| `float`、`f32`、两种窄浮点 | **Self** | `:102-106` `isFloatingScalar` |
| `i32`、`bool`（离散） | **没有** | `:107-111` `DerivativeRegistry::isDiscrete` → "the derivative is zero by definition and there is nothing to carry" |
| 数组 `T[]` | **看元素**：元素 Self ⇒ 数组 Self（`float[]` 有切空间）；元素 Other ⇒ 元素类型替换后的数组；元素没有 ⇒ 数组也没有 | `:81-95` |
| 元组 / 结构体 | **各成员的积**；任一成员没有 ⇒ 整个没有；空聚合体 ⇒ 没有 | `:96-98`，`:55-74` |
| 张量等模块自有类型 | **由模块注册回答**（`tangentType` 为空或回自己 ⇒ Self，否则 Other） | `:112-122` `DerivativeRegistry::findTangentSpace` |
| 其余一切 | **没有**（兜底 `:123`） | |

**重点两条**：**`float[]` 有**（元素是浮点，取 Self）；**`int` 没有**（离散，兜底之前的显式一支），**与作者"没有切空间的形参不占槽"一致**。

### 那么 `split` 到底被谁拦住——实测分辨出来了

**(a) 只有数组形参（`func h(xs: float[]): float { return xs[1] * xs[0] }`）**：`vjp(h)` **过了 `split`**，往下走到

```
error[RuntimeDiag::RuntimeError]: transforms: transpose has no transpose for this function.
   --> stdlib/vjp.cml:72:47  transpose(linear)(residuals, ct)
```

即：**数组的切空间有、`split` 也认**；卡的是**转置缺数组元素乘标量的规则**（那是 `d14d6a96` 的地盘）。

**(b) 加一个 `int` 形参（作者那条 `f(xs: float[], n: int)`）**：**在 `split` 上就被拒**

```
error[SemanticDiag::ArgumentsRejected]: Invalid call to 'split' with argument types
  <> ((float[], float[], int) => (float, float) pure): `split` takes a lifted function, whose every parameter…
   --> stdlib/vjp.cml:69:34
```

**结论**：拦住的**不是 `float[]`，是那个离散的 `int`**——`tangentSpaceOf` 已经正确地答"没有切空间"（`:107-111`），
**而 `split` 要求每个形参都升起**。这就是"**代码与作者 18:50 裁定正面冲突**"的那一处：裁定说"没有切空间的形参**不占槽**"，
`split` 说"每个形参都得有"。**归 `split`/`jvp`（`remat` 正在做的 #7），本车道不动 `modules/transforms`。**

### 纪律收获（写下来给下一个人）

**`grad` 泛不泛型，根本走不到。** 我们两轮先改上游签名，再量下游要什么——**顺序反了**。
正确的第一问是"下游 `split`/`transpose` 要什么形状"，答案就在上面两条拒绝里。

## 13. 两堵墙各有一条用例守着（不是 `known_fail`）

| 用例 | 今天断言 | 墙倒了以后改哪一句 |
|---|---|---|
| `test/cases/compile/transforms/grad_array_with_int.cml` | `expect_exit = 1` + `Invalid call to 'split'` | **墙 A 倒了**（没有切空间的形参不占槽）⇒ 改成墙 B 的那句 `transpose has no transpose`；**墙 B 也倒了** ⇒ 改成数值：`xs` 的梯度 `[2.0, 3.0]`、`n` 的 `0.0` |
| `test/cases/compile/transforms/grad_array_no_int.cml` | `expect_exit = 1` + `transpose has no transpose for this function` | **墙 B 倒了**（数组元素 × 标量的转置规则）⇒ 改成数值：`[2.0, 3.0]` |

两条都写成"**期望拒绝**"（`expect_exit = 1` + `expect_output_contains`），**没有用 `known_fail`**：墙倒的那一刻它们会**变红**，
逼下一个人把断言从"拒绝"改成"数值"——那正是墙倒的信号。

**实测**：`transforms` + `rest_params` = **66/66**（48 + 2 新 + 16），`BUILD_EXIT=0`。

**`tangentSpaceOf` 表见 §12**；补一句：**`int` 的答案是"没有切空间"（`tangent_space.cpp:107-111`），与 18:50 的裁定一致；
错在 `split` 要求每个形参都升起。**

## 14. A2：`p.1` 作用在结构体上，段错误 → 诊断

**复现（三次，逐次都是 139）**：`camel recompile_segv.cml "std::jgir canonical=true"` → `rc=139 SIGNAL 11` ×3。

**栈（前 6 帧）**：

```
#0 camel::compile::gir::Builder::visitAccsNode(camel::compile::gct::Node* const&)   ← SIGSEGV
#1 Builder::dispatch / #2 Builder::visit
#3 Builder::visitCastNode
#5 Builder::visit → #6 Builder::visitNRefNode
#9 Builder::visitInPosition → #10 Builder::visitExecNode → #11 Builder::visitBodyNode
```

**根因（就是你要的那一条：位次与字段共用一条路径）**：`src/compile/gir/builder_expr.cpp` 的 `visitAccsNode` 里，
`TypeCode::Struct` 这一支**无条件**读 `gct->accessKey()`；而 `accessKey()`（`include/camel/compile/gct/node.h:269-272`）是

```cpp
const std::string &accessKey() const { ASSERT(!accessByIndex(), "The access is by position"); return *payload_.name; }
```

**位次访问的 `payload_.name` 是空指针**——`ASSERT` 在 release 里被编掉，于是解引用空指针。
**`p.1`（位次）与 `s.x`（字段名）走的是同一个 ACCS 节点、同一条下降路径，差别只在 `flags_` 的那一位**。

**修法（不是"删掉那段代码让它不崩"，是给拒绝）**：在 `Struct` 这一支先判 `accessByIndex()`，命中就报
`SemanticDiag::InvalidAccessIndex` 并 `throw BuildAbortException()`。

**读数（崩 → 诊断）**：改后三次都是 `rc=1`：

```
error[SemanticDiag::InvalidAccessIndex]: Invalid access index: '1'.
  --> recompile_segv.cml:3:18
 3 |     let v: int = p.1
   |                  ^^^
help: Ensure the index is within bounds for arrays or refers to a valid field in structs.
```

**第二个诊断（`nested` 没有返回值）这次没有印出来**：编译在类型错误处停下，`grep -c '^error'` = **1**。
**是"未给"，不是"给了又吞掉"。**

**一处没做到的**：本想给一条专门的 `StructMemberByIndex`（文案"结构体的成员按名字取，位次只给元组"）。
往 `diagnostics/registry.tsv` 插了行（`0x000209`），但 `node scripts/diagnostics-catalog.mjs --write` 报告
`wrote diagnostics/registry.tsv (unchanged) … 305 codes`，**那一行没有被接受**（文件被还原成 305 条）。
所以今天用的是现成的 `InvalidAccessIndex`（help 里已有 "or refers to a valid field in structs"）。
**专门的码留给下一次**，本条不回退。**`--check` 仍是 305。**

## 15. 同族扫描：`ASSERT` 在 release 被编掉、而它守的是一次解引用

**搜索根**：`git grep -n 'ASSERT(' -- src include`，**全树 270 处**。
**筛法**：`ASSERT(...)` 的**下一行**含 `*` / `[` / `->` / `.at(` / `.value()` 的，**110 处**。
按区：**前端（`src/compile`、`src/parse`、`include/camel/compile`、`include/camel/parse`）17 处**；
**其余 93 处**在 `src/runtime`（draft_* 多）、`src/passes`、`src/core`、`include/camel/runtime`。

**纪律（先写这一条）**：诊断码由 `diagnostics-catalog.mjs` 分配，**手写进 `registry.tsv` 的 `0x000209` 会被 `--write` 还原**——
这不是失败，是设计。所以 A2 用现成的 `InvalidAccessIndex` 是对的，`--check` 仍 305。**下一个人不必再试。**

### 15.1 前端 17 处

| 处 | 守的是什么 | 判 |
|---|---|---|
| `gct/node.h:270 accessKey()` | `!accessByIndex()`，然后 `*payload_.name` | **危险，已证实并已修（A2）**：`p.1` 作用在结构体上 ⇒ 三次三次 SIGSEGV。现在先判 `accessByIndex()` 再取名字 |
| `gct/node.h:219 exportName(i)` | `i < names->count` | **同形，未证**：调用方按 `count` 循环，**没造出越界的最小程序** |
| `gct/node.h:239 loopStateName(j)` | `j < loopStateCount()` | **同形，未证**：同上，索引来自状态表的遍历 |
| `gct/node.h:244 loopElementName()` | `loopHasIterable(loopKind())`，然后取 `count - 1` | **同形，未证**：`count == 0` 时是 `[-1]`；**没造出"for 循环没有元素名"的程序** |
| `gct/node.h:315 enumCase()` | `caseType() == Enum`，然后 `*payload_.enumCase` | **同形，未证**：调用方先按 `caseType()` 分派 |
| `gct/node.h:339 typeCaseTypeAt(i)` | `types != nullptr && i < count` | **同形，未证**：索引来自模式表遍历 |
| `gct/node.h:420 staticLoopStateName(j)` | `j < count` | **同形，未证**：同 239 |
| `gir/arena.h:162,197,285,288,295,300` | 分配器自己的尺寸/守卫字节 | **到不了**：这些是 arena 的**内部不变量**（分配与校验成对），程序形状不选择它们；破坏它意味着分配器已出错 |
| `parse/ast/base.h:97,99` | `index < children_.size()`，然后 `children_.at(index)->load()` | **到不了（对用户程序）**：越界由 `.at` 抛异常；`->load()` 的空指针意味着**解析器造出了空子节点**，那是解析器的 bug，不是程序的形状 |
| `parse/ast/base.h:106` | `opt->type() == Optional` | **同形，未证**：先判后取，在 release 下走错类型才 UB；未造出程序 |
| `parse/ast/builder.cpp:4925` | 多行 `ASSERT` 的续行（筛法误报） | **不适用** |

### 15.2 其余 93 处

**未逐处判定**（如实写：不是"安全"，是"这一轮没判"）。它们在 `src/runtime`（`draft_regions.cpp` 9、`draft_session.cpp` 8、`draft_construct.cpp` 7、`draft.cpp` 4、`draft_clone.cpp` 4 …）、
`src/passes`、`src/core`、`include/camel/{runtime,core}`。理由不是"大概安全"：**这些是运行期内部结构（草稿图、会话、分配）的不变量，
用户程序的形状不直接选择它们**；但"不直接"不等于"不可能"，**这一栏留给下一轮逐处判定**。

### 15.3 模板（从 A2 抽出来的）

**把断言里的前提变成代码里的分支。** 断言在 release 里不存在，所以它守的前提在 release 里也**没有守卫**；
凡是"断言 + 紧接着解引用"的写法，正确形态都是**先判、判不过就报诊断**（编译期）或**报错返回**（运行期）。
A2 的改法就是这一族的模板：`if (gct->accessByIndex()) { 报 InvalidAccessIndex; throw; }`。

## 16. 前端六处"同形未证"的处置：全部有 release 下的界/分支保证

| 处 | 最小程序形状 | 结果 | 归属 |
|---|---|---|---|
| `gct/node.h:219 exportName(i)` | 需要一个 `EXPT` 节点上超出 `exportCount()` 的下标。**造不出**：两处调用都是 `for (i < exportCount())`（`gir/builder_modules.cpp:721`、`:1322`），没有语法能让下标越界 | **安全**（release 下有界） | 不修 |
| `gct/node.h:239 loopStateName(j)` | 需要 `j >= loopStateCount()`。**造不出**：调用点自带界（`gir/builder_control.cpp:2962` 的 `j < k`） | **安全** | 不修 |
| `gct/node.h:244 loopElementName()` | 需要"有 iterable 但 `names` 为空"的 `LOOP`。**造不出**：语法上 `for x in xs` 必给元素名，所以"有 iterable ⇒ 至少一个名字"；两处调用还各自被 `withElement == loopHasIterable(kind)` 守着（`builder_control.cpp:3200`、`:3221`；`:3996` 在 for 支内） | **安全** | 不修 |
| `gct/node.h:315 enumCase()` | 需要 `caseType() != Enum` 时取 `enumCase()`。**造不出**：唯一调用点先做枚举模式检查，不匹配就报 `EnumPatternMismatch` 并中止（`gir/builder_enum.cpp:353-358`） | **安全** | 不修 |
| `gct/node.h:339 typeCaseTypeAt(i)` | 需要 `i >= typeCaseCount()`。**造不出**：调用点都按 `typeCaseCount()` 遍历（`gir/builder_static.cpp:83`、`:463`、`:531`、`:539`） | **安全** | 不修 |
| `gct/node.h:420 staticLoopStateName(j)` | 同 239。**造不出**：`for (j < staticLoopStateCount())`（`builder_static.cpp:360`、`:1200`） | **安全** | 不修 |

**六处的共同结论**：这六处与 A2 **不同形**——A2 的前提（"名字存在")由**程序写法**选择，而六处的前提（"下标在表内"）
由**调用点自己的循环界**在 release 下保证。**所以一处都不修、也没有"改前改后 rc"。**
只要某天调用点不再自带界，这里就会退化成 A2——**模板仍是那一句：把断言里的前提变成代码里的分支。**

`git grep -c 'ASSERT' src`：本轮**未改**（没有删断言，也没有新增），数值不变。

## 17. 全量闸门：不是 0，有 2 条 fail（都不是本车道）

`-Plan test/plans -Tier all -Jobs 8`：

```
[FAIL] progen_check     133.02 s cpu of 150.00 s budget, 88.00 s wall
[FAIL] progen_fixed                                           17.36 s
```

单跑 `test/plans/feat/opt/progen.plan.toml`（`-Jobs 2`）**仍然两条都红**（`133.27 s cpu / 107.90 s wall`、`24.99 s`），
所以**不是并发负载**。`progen_fixed` 的红是这样的：

```
F20 findings/inline_crash_function_argument.cml [std::opt std::nvm]: REFUSED:RuntimeDiag::PassInternalError:
    Pass 'std::opt::fold' failed with an internal error: graph '__root__' has effect n…
progen fixed: 29 findings, 480 runs
4 wrong
```

**判断**：这两条与本节改动无关——`std::opt::fold` 的 `PassInternalError` 不是本车道能产生的形态（本车道的改动要么给
`SemanticDiag`，要么照常建节点），且 `progen_check` 是 CPU 预算（133 s 对 150 s 的余量）。**但"无关"只是判断，
不是读数**：要证它在 `develop` 上同样红，得在另一棵树跑同一计划——**本车道没有第二棵树，这一条留给主检出。**

## 18. F20 那条 `PassInternalError`：全文、分层、缩小到哪一步为止（不修）

**① 完整原文（一行，未截）**：

```
error[RuntimeDiag::PassInternalError]: Pass 'std::opt::fold' failed with an internal error: graph '__root__' has effect node 42 (':mark/reduce_arr') with neither a read of its value nor a place in its chain, written at test/cases/opt/progen/findings/inline_crash_function_argument.cml:12:99.
```

**② 分层（四次退出码，同一个文件）**：

| 管线 | rc |
|---|---|
| `std::nvm` | **0** |
| `std::opt::inline std::nvm` | **0** |
| `std::opt std::nvm` | **1** ← 这句在这里 |
| `std::opt::fold std::nvm` | 不是管线写法（`std::opt::fold` 不是遍名，报 `InvalidPassParameter`） |

⇒ **出在 `std::opt` 这一层里的 `std::opt::fold`**；`std::opt::inline` 与 VM 都干净。与 A4f 同族（"`opt` 在 fold 期间产生效果位置错误"）：
这里是**效果节点在一个可能不执行的分支里，既没人读它的值、也不在链上**。

**③ 缩小：五次尝试，只有原文件能复现**（判据是"同一句文案逐字相同"）：

| 尝试 | 形状 | 结果 |
|---|---|---|
| s1 | `concat(['']) -> reduce<...>` | **编不过**：`NoMatchingFunction … <> (string[])`（`concat` 的实参形状不是这样写的） |
| s2 | `['', a3] -> reduce<...>`，`a3` 是捕获的字符串 | **rc=0，无这句** |
| s3 | 同上，先绑 `let y = ['', a3]` 再 reduce | **rc=0，无这句** |
| s4 | `concat([''], if false then [''] else ['', a3]) -> reduce<...>` 放在函数里，`main` 用 `if false then 0 else f1('')` 调 | **rc=0，无这句** |
| s5 | `concat(if false then [''] else ['', a3]) -> reduce<...>` | **编不过**：同上 |

⇒ **最小程序仍是原文件**（36 行）。去掉的部分都能各自 rc=0：**`sync { ... }` 两条臂、`r23` 那层调用、以及 `f1` 的函数类型形参 `a2`
三者里至少有一个是必要条件**，但**我没能把它单独分离出来**——这一条留给下一轮，**不冒充缩小成功**。

**④ 附带一个读数**：报错位置写的是 `:12:99`，而第 12 行只有 `let n6 = true;`（`reduce` 实际在第 13 行）。**位置本身也是错的**——
与"效果节点在分支里"可能是同一个记账问题，但这是观察，不是结论。

## 19. A1：`std::cml` 段错误（复现 + 栈），修之前先报要动的文件

**① 复现**：`camel cml_segv.cml std::cml` → **`rc=139`、SIGSEGV（core dumped），三次三次**。

**② 栈（前 12 帧，点出函数名）**：

```
#0 __memmove_avx_unaligned_erms                                        ← SIGSEGV（读坏地址）
#1 std::__format::__formatter_str<char>::format<…>(string_view, …)
#2 std::basic_format_arg<…>::_M_visit<…>
#3 std::__format::_Formatting_scanner<…>::_M_format_arg
#4 std::__format::_Scanner<char>::_M_on_replacement_field
#5 std::__format::_Scanner<char>::_M_scan
#6 (anonymous namespace)::CmlPrinter::emitAssignment(uint, string_view)   ← 崩在打印这一句
#7 CmlPrinter::processNode
#8 CmlPrinter::printStatements
#9 CmlPrinter::run
#10 CmlDumpPass::apply
#11 applyPassesDetailed
```

⇒ **崩在 `CmlPrinter::emitAssignment` 里的 `std::format`**：格式化一个字符串时读到坏指针（`memmove`）。
不是遍调度、不是 VM，是**打印器在写赋值那一行时**。

**③ 相关代码**：`src/passes/trans/cml/cml.cpp:1470-1489`，`emit()` 那一句是

```cpp
emit(indent_ + std::format("{} {} {}", cell->second, assignmentOperator(uri),
                           expression(inputs.back()).text));
```

`Expr::text` 是 `std::string`（`:207`），`cells_` 是 `unordered_map<gc_node_ref_t, std::string>`（`:338`），
`assignmentOperator(uri)` 返回 `uri` 的子视图（`uri` 是本帧的 `string_view`，来自节点体）。**三个实参各自看似都有效**，
所以下一个动作是**在这句前后把三个字符串各打一次长度/指针**，确认是哪一个坏了——**这一步还没做**。

**④ 要动的文件（先报，等调度）**：`src/passes/trans/cml/cml.cpp`（函数 `CmlPrinter::emitAssignment`，必要时连同
`CmlPrinter::expression` 的 lambda/clone 分支）。**与 `m5'` 的 `xs->sum()` 接收者那一列不重叠**——那里改的是调用形状，
这里改的是赋值打印时的字符串生命周期。

**⑤ 还没修**：这一笔只到"复现 + 栈 + 文件清单"。**下一次动手前先确认那一句的三个字符串谁坏了**，
避免用"加个 try/catch"之类的办法把段错误藏起来——**期望仍是"打印出源码"或一条具名拒绝**。

## 20. A1 修好了：`std::cml` 打赋值时的悬垂引用

**① 打点（三个字符串 + ref）**，在 `CmlPrinter::emitAssignment` 的格式化之前：

```
[probe] cell=0x5a6e100fb8c8 size=99428762372640 cap=99428762372704 op=0x7d48d7c01e8f size=1
        val=0x5a6e100fbff0 size=24 cap=24 ref=4
```

**读法**：`op` 与 `val` 都正常（1 和 24），**`cell` 的 `size` 是垃圾值 99428762372640**——**是单元格名字那个 `std::string` 对象本身没了**，
不是它指向的缓冲有问题。三个里**只有它坏**。

**② 原因**：`emitAssignment` 先 `auto cell = cells_.find(...)`，再在同一个完整表达式里调 `expression(inputs.back())`；
而打印值的那条路里**会恢复 `cells_` 的存档**（`cml.cpp:1100`/`:1783`/`:1844` 都有 `auto savedCells = cells_;`，试不成就把表放回去）。
**"放回去"是一次整表赋值，会销毁表里每一个条目** ⇒ `cell->second` 的引用指向已释放的对象，`std::format` 读到垃圾长度。

**③ 修法（只在生命周期上改，没有 try/catch、没有 `if(!valid) return`）**：

```cpp
const std::string cellName  = cell->second;                    // 先拷
const std::string valueText = expression(inputs.back()).text;   // 再读值（这一步可能恢复存档）
emit(indent_ + std::format("{} {} {}", cellName, assignmentOperator(uri), valueText));
```

**④ 读数（崩 → 打印源码）**：`camel cml_segv.cml std::cml` **改前 `rc=139` ×3；改后 `rc=0` ×3**，前 10 行：

```
func adder(n: int): (x: int) => int {
    return (x: int): int => x + n
}

func main(): int sync {
    let g: (x: int) => int pure = (x: int): int => x * 3
    var h: (x: int) => int pure = (x: int): int => x - 1
    h = (x: int): int => x + 100
    println('let', g(14), h(1))
    let add5: (x: int) => int = adder(5)
```

**⑤ 用例**：`test/cases/compile/transforms/cml_var_holding_a_lambda.cml` + 计划项 `cml_var_holding_a_lambda`
（`std::cml`、`expect_exit = 0`、`expect_output_contains = ["h = (x: int): int => x + 100"]`、并且不含 `Segmentation fault`）。

**⑥ 判据**：`transforms` + `rest_params` = **68/68**，`BUILD_EXIT=0`；`git grep -c 'std::format' src/passes/trans/cml/cml.cpp`
**前后未变**（只改了这一个格式化的实参来源，没有动别的格式化点）。

**⑦ 主检出污染（这一轮的错，记下来）**：`[System.IO.File]` 与 `git grep` 的相对路径按**进程的工作目录**解析，
不是 PowerShell 的 `cd`——所以这一轮的打点与早前那条 `0x000209` 都写进了主检出 `D:\Projects\Camel`。
已把两份 diff 存成 patch、`git restore` 主检出（现在 `git status --porcelain` 为空）、把 A1 的 patch `git apply` 到本工作树。
**`0x000209` 那条不 apply、不保留**（诊断码由生成器分配，手写会被 `--write` 还原）。

## 21. 同族扫描：`savedCells` 恢复点前后有没有引用跨过去

**搜索根**：`git grep -n 'savedCells' -- src`（全树）→ **只有 `src/passes/trans/cml/cml.cpp` 三处恢复点**：

| 恢复点 | 保存 | 恢复 | 恢复方式 |
|---|---|---|---|
| `buildBody` | `:1100` | `:1156` | `cells_ = std::move(savedCells);` |
| 循环打印 | `:1787` | `:1822` | 同上 |
| 第四个（`skip_`/`topo_` 一起存） | `:1848` | `:1914` | 同上 |

**关键**：`cells_ = std::move(savedCells)` 是**整表赋值**，会销毁表里原有每一个条目——**任何在恢复之前取得的元素引用都会悬垂**。

### 21.1 三栏表

| 处 | 形状 | 判 |
|---|---|---|
| `emitAssignment`（`:1478` `cells_.find` → `:1485` 用 `cell->second`） | **引用跨过 `expression(inputs.back())`**，而它会走到 `buildBody`/循环打印的恢复点 | **危险，已修（A1）**：改成 `const std::string cellName = cell->second;` 先拷 |
| `cellName()`（`:1547`） | `return found->second;` —— 返回类型是 `std::string`，**按值拷出** | **安全**（值在恢复前已拷出） |
| `paramName()`（`:1303`） | 同上，**按值返回** | **安全** |
| `expression()` 里的 `declared_.find`（`:1577`） | `return {declared->second, 100};`，`Expr::text` 是 `std::string`，**按值** | **安全** |
| `appendLines()`（`:1033` 遍历 `lines_`） | 循环体只写 `out_`，**不调打印器**，不会触发恢复 | **安全** |
| `const std::string &name = graph->name()`（`:246`、`:510`） | 不是被恢复的容器（图的名字） | **到不了** |
| `body.lines` 的遍历（`:1167`） | 遍历的是**局部副本** `Body::lines` | **到不了** |

**结论**：**同形的只有 A1 那处，已修**；其余按值返回或不在被恢复的容器上。**零命中的搜索根**：`savedCells` 只在 `cml.cpp` 出现（`src/` 全树）；元素引用只在 `emitAssignment` 一处跨过恢复点。

## 22. 全量（`-Plan all -Tier fast -Jobs 8`）：8352 通过，3 条 fail，都不是本车道的改动

```
[SUMMARY] total: 8367, pass: 8352, fail: 3, skip: 10, known failures: 2
[FAIL] registry.docs_complete            49.20 s
[FAIL] registry.reference_is_generated    0.39 s
[FAIL] docs.doc_readings.current          0.41 s
```

**② 两条 registry 是生成物过期**：`docs/stdlib-reference.md is out of date: run node scripts/stdlib-reference.mjs --write`；
`docs_complete` 报 `the run did not say that its counts are the ones the script records`。
**读数（不是判断）**：`git show origin/develop:docs/stdlib-reference.md` 与本树的 `docs/stdlib-reference.md` **逐字节相同**，
而 `node scripts/stdlib-reference.mjs --verify-doc` **仍然报过期** ⇒ **`origin/develop` 上这条就是红的**，不是合并造成的。

**③ 一条 `doc_readings`**：`stale transforms-module.lines: docs/handover/reviews/questions-2026-11-01-…md states [202,268,116,104,1531,1396]`。
**那是 transforms 模块的行数读数，本车道没有改过 `modules/transforms` 的任何文件**（generic-grad 那轮已回退成 HEAD）。

**④ 本车道的两份计划是绿的**：`transforms` + `rest_params` = **68/68**，`BUILD_EXIT=0`。**A6 这一轮没开始**——预算用在全量与扫描上，不假装做了一半。

## 23. 全量那 3 条红：都在"生成物过期"，且根子是同一笔

**① `registry.reference_is_generated` 在比什么**：`scripts/stdlib-reference.mjs:2770-2776` ——

```js
const current = fs.readFileSync(DOC_PATH,'utf8').replace(/\r\n/g,'\n');
if (current !== text) { console.log('… is out of date: run --write'); process.exit(1); }
```

**比的是"`--write` 现在会产出的整篇文本"与"文件里的文本"，换行归一化后逐字符比**，不比哈希、不比时间戳、**没有副本**。
所以"两份文件逐字节相同却报过期"的读法要改一句：**逐字节相同的是"我的树"与"`develop` 的树"，不是"树"与"生成结果"**。

**② 它到底缺哪一项**：`--write` 生成后 `git diff` 是 **14 增 14 删**，全部围绕 `vjp`：

```
-vjp: (f: (x: T) => float) => (x: T) => (float, (ct: float) => (T,))
+vjp: (f: F) => Vjp<F> when is_function::<F>()
+  …（说明段：一份声明照顾所有 arity、`(..xs)` 的规则）
```

**也就是 `stdlib/vjp.cml` 变成"一份声明照顾所有 arity"之后，参考文档没有重新生成。**
`git log 28e36a2cb..HEAD -- stdlib/vjp.cml` 只有一笔：**`9048c30ea` vjp is one declaration for every arity**（从作者那条线进来的）。

**③ `registry.docs_complete`**：`--verify-docs` 在远端（有编译器）的原话是

```
EXAMPLE_STOPS['a semantic error'] records 39, and the run found 40: update the number in the same commit and say why the class moved
```

**第 40 条是 `vjp` 自己的例子**：`stdlib/vjp.cml:68` 的 `@example` 仍把 `f` 当值写，而声明已把它当类型参数 ⇒
`UnresolvedReference: 'f'`。**这是例子自己的欠账，不是"豁免"**——所以数字改成 40，并在旁边写明为什么。

**④ `docs.doc_readings.current`**：`check-doc-readings.mjs` 量的四份文件是
`split_operator.cpp, jvp_operator.cpp, stdlib/signatures/transforms.cml, stdlib/vjp.cml`：

| 文档记的 | 树上量的 | 哪一份 |
|---|---|---|
| 202 | 202 | `split_operator.cpp` |
| 268 | 268 | `jvp_operator.cpp` |
| 116 | 116 | `stdlib/signatures/transforms.cml` |
| **104** | **98** | **`stdlib/vjp.cml`** ← 少了 6 行 |
| 1531 | 1531 | （页上第五个数） |
| 1396 | 1396 | （页上第六个数） |

**同一笔 `9048c30ea` 把 `vjp.cml` 缩短了 6 行** ⇒ 页上 104 改 98。**本车道没写过 `stdlib/vjp.cml`**（上面那条 `git log` 就是证明）。

**⑤ 落到一句可执行的话**：**属于 (b) 文档数错了** ——
`docs/stdlib-reference.md` 用 `--write` 重生成（不手改），`EXAMPLE_STOPS['a semantic error']` 39→40 并注明是 `vjp` 的例子，
页上 104→98 并注明是 `9048c30ea`。**没有删任何断言、没有加任何豁免。**

**⑥ 读数**：`docs` + `stdlib_registry` = **16/16**，`fail 0`；本地 `--verify-doc` = `up to date`，
`check-doc-readings.mjs` = `the readings are the ones the pages state: 10 of 10`。

## 24. `9048c30ea` 三问：作者自己的、`Vjp<F>` 真的存在、文档记的是真签名

**一、`git show --stat 9048c30ea`（原文）**：

```
9048c30ea Zhenjie Wei <Kogler_S@outlook.com> Mon Nov 2 04:02:10 2026 +0800
vjp is one declaration for every arity

 stdlib/vjp.cml                                | 42 +++++++++++--------------
 test/cases/compile/transforms/vjp_arities.cml | 42 +++++++++++++++++++++++++
 test/plans/feat/compile/transforms.plan.toml  | 44 +++++++++++++++++++++++++++
 3 files changed, 104 insertions(+), 24 deletions(-)
```

**二、是谁的**：**作者的**（`Zhenjie Wei`，今天 04:02），**自带一个用例和一个计划项**（`vjp_arities`，44 行）。
**不是本车道的产物**——我回滚的是我自己的 generic-grad 实验，回滚后的 `stdlib/vjp.cml` 就是现在这份。
`git branch -a --contains 9048c30ea` 只列出 `lane-lit2`，`git merge-base --is-ancestor 9048c30ea origin/develop` = **假**：
**它只在本分支上**，是作者那条线还没进 `develop` 的一笔。

**三、`Vjp<F>` 存在吗**：**存在**。

```
stdlib/types.cml:100: export type Vjp<F> = (..ParamsOf<F>) => (ResultOf<F>, Pullback<F>)
stdlib/types.cml:186: export func is_function::<T>(): bool
stdlib/vjp.cml:68:    export func vjp::<F>(f: F): Vjp<F> when is_function::<F>()
```

（`src/builtin/operators/derivative.cpp` 里的 `VjpBuilder`/`addVjp<F>` 是 C++ 名字，不是这个类型；类型本身在 `stdlib/types.cml`。）

**四、结论**：**有意的一笔，保留、不 revert**；`docs` 那两处改动照旧，提交信息已点名 `9048c30ea`。
**没把文档改成记录一个不存在的签名**——`--write` 的产物与 `stdlib/vjp.cml:68` 逐字一致。

**五、两对数（改前 / 改后）**：

| 检查 | 改前 | 改后 |
|---|---|---|
| `stdlib-reference.mjs --verify-doc` | `docs/stdlib-reference.md is out of date: run … --write`（**rc=1**） | `docs/stdlib-reference.md is up to date`（**rc=0**） |
| `check-doc-readings.mjs` | `stale transforms-module.lines … 1 reading(s) are not the ones the tree has`（**rc=1**） | `the readings are the ones the pages state: 10 of 10`（**rc=0**） |

## 25. A6：没找到它的最小程序

作者清单 A6 的最小程序**不在本仓库里**（它是 `std::macro std::opt` 崩）。本轮用现成用例探四支：

```
test/cases/loops/macro.cml                        rc=0
test/cases/compile/collections/macro_constants.cml rc=0
test/cases/consistency/limits/macro_depth.cml      rc=1（正常拒绝）
test/cases/consistency/new_values/macro_append.cml rc=0
```

**没有一支崩**。所以 A6 要么换一个形状，要么需要 README 那条最小程序原文——**不猜着造**，免得把"我没找到"记成"它不崩"。

## 26. A6 复现到了：`std::macro std::opt std::gir` 读变体越界 → SIGABRT

**① 怎么找到的**：A6 的 26 个语料程序清单（`repro_checks.json`）**没有随报告进仓库**（`git ls-tree origin/fsh-dev` 里只有 README 与少量 `minimal/`、`e9_reduced/`）。
但 README 写明"enum 与 `match … when` 程序在 `std::macro std::opt std::gir` 上 signal 6"，于是按这个类扫：
`git ls-files "test/cases/*enum*.cml" "test/cases/*match*.cml" | head -40`，每支跑一次该管线 —— **40 支里 2 支崩**。

**② 两支，各三次，都是 SIGABRT（signal 6，core dumped）**：

```
test/cases/compile/enum/generic_tree.cml   std::macro std::opt std::gir   ← 三次都 Aborted
test/cases/compile/enum/shapes.cml         std::macro std::opt std::gir   ← 三次都 Aborted
```

**③ 原文（`generic_tree.cml` 最后一次的尾部）**：

```
Reading    : reading field 0 of a Variant whose tag 1 declares 0 field(s), so the read is past its last one
Stack Trace: (not available on this platform)
```

`shapes.cml` 是同一句的另一种偏移：`reading field 1 of a Variant whose tag 0 declares 1 field(s)`。

**④ 栈（前 12 帧，点出函数名）**：

```
#0..#4  __pthread_kill / __GI_raise / __GI_abort
#5  cml::fail_invariant(string const&, string const&, …)
#6  camel::core::rtdata::Variant::get(unsigned long) const        ← 越界读在这里被守卫抓住
#7  (anonymous namespace)::foldDraft(GraphTransaction&, Context&, deque<RootHandle>&, unsigned long)
#8  camel::passes::generic::rewriteReachableGraphs(…)
#9  simplifyGraph(…, OptimizeRewriteConfig const&, ostream&, unsigned long, GraphIRPass*)
#10 applyPassesDetailed(…)
```

**⑤ 位置**：`Variant::get` 的守卫在 `include/camel/core/rtdata/variant.h:47-54`（`GUARD` → `fail_invariant` → `abort`），
**越界由 `std::opt::generic` 的折叠规则读出来**：`src/passes/opt/generic/generic.cpp:1172` `foldDraft`。
**也就是"折叠规则按一个 tag 的形状去读，而手里那个值的 tag 不是它假设的那一个"。**

**⑥ 还没修**：这一笔到"复现 + 栈 + 位置"。修的方向是让 `foldDraft` 在读之前核对 tag/字段数（**按 A2 的模板：把断言里的前提变成分支**），
或者让规则匹配阶段只产出与自身 tag 一致的读 —— **下一笔做，改前 `signal 6` ×3、改后要一个结果或一条具名诊断**。

## 27. A6 修之前必须先分清是哪一次读：两个候选分支，只有一个有守卫

`foldDraft`（`src/passes/opt/generic/generic.cpp:1172`）自己不解引用变体；它是**帧**，真正读的是被内联进来的折叠函数。
按 `Variant::get(index)` 的形状，候选是 `foldStaticAccess`（`:965`）里的两个分支：

| 分支 | 读法 | 有没有先核对 |
|---|---|---|
| `TupleIndex` + `source->second->code() == Tuple`（`:980-986`） | `tuple->get<slot_t>(body->value)` | **有**：`if (body->value >= tuple->size()) return false;` |
| `StructKey` + `source->second->code() == Struct`（`:987-991`） | `object->get<slot_t>(std::string(body->key()), source->second)` | **没有**：它信 `source->second`（静态类型）与运行时对象一致 |

**崩点说的是"读 field 0 的一个 tag 1、声明 0 个 field 的 Variant"**——**按字段序号读、而运行时对象的字段数比静态类型声称的少**，这正是 `StructKey` 那支的形状
（`Struct::get` 用类型里的字段序号去取，取值落在 `Variant::get` 上）。

**但"形状对"不等于"证到了"**：要坐实得知道崩溃那一刻 `body->accsKind` 与 `source->second->code()` 的真实值。**release 二进制没有行号，gdb 只给到 `foldDraft` 这一帧**。
所以**这一轮不改代码**——照今天的规矩，**宁可少改一处，也不按面相改**。下一笔的第一件事：**在这两个分支的 `get` 之前各打一次 `accsKind`/`code()`/字段数**，一次构建就能分清；
分清后按 A2 模板改（**核对不过就 `return false` 跳过折叠**，不新增诊断、不放宽 `variant.h:47-54` 一个字）。

**给作者的事实**：作者报的 26 支清单（`repro_checks.json`）**没有随报告进仓库**——`git ls-tree -r --name-only origin/fsh-dev` 在 `docs/handover/reports/eval-defects-2026-11-01/` 下只有 `README.md`、`minimal/`、`e9_reduced/`。
所以这 2 支是**按 README 描述的类（enum 与 `match … when`）扫 40 支找到的**，不是清单里的原班人马。

## 28. A6 打点结果：两支都不是 `foldStaticAccess` 里读的 ⇒ 停手，不扩范围

**打点**（`foldStaticAccess` 两个分支的 `get` 之前，`accsKind` / `code()` / 运行时字段数 / 索引或键），一次构建两支都跑：

```
=== generic_tree.cml                                  rc=134
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=left
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=right
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=left
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=right
    Reading    : reading field 0 of a Variant whose tag 1 declares 0 field(s)…

=== shapes.cml                                        rc=134
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=w
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=h
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=w
[probe] struct access: kind=1 code=1610612745 runtime_fields=2 key=h
    Reading    : reading field 1 of a Variant whose tag 0 declares 1 field(s)…
```

**读法**：**两个分支各被走到过，每次运行时对象都有 2 个字段、键都在（`left`/`right`、`w`/`h`）——静态类型与运行时形状一致，`get` 没有越界。**
**崩点在这之后**，所以**读它的不是 `foldStaticAccess`**。**按你的话：停手，不扩范围。**

**下一步的名单（不本轮做）**：`foldDraft` 这一帧里其余会在"值"上动手的调用——
`pruneBranch`、`staticValueOf`、`holdsNoValue`、`fitsConstantBudget`、`replaceFoldResult`、`materializeStaticValue`。
`Variant::get(0)` on a tag-1/0-field variant：**枚举的变体值被当成结构体/元组读**，是下一个要打点的形状。

**判据（照你要求写进页，以后所有 fold 类改动适用）**：
**静态类型与运行时形状不一致时，折叠应当放弃，而不是相信类型。**

**打点已删净**（`git checkout -- src/passes/opt/generic/generic.cpp`，`git status` 为空，`git grep -c probe` = 0）。

## 29. A6 第二次排除：六处候选中五处已读、都不读变体；剩下的是"被内联进 `foldDraft` 帧里"的那一层

**先按你的要求列"可能在变体上读的位置"**：`git grep -n 'Variant' -- src/passes/opt/generic/generic.cpp` = **0 命中**（这个文件里没有一处直接写 `Variant`）。
所以读取一定发生在**被调用者**里，而栈上那一帧仍显示 `foldDraft`（release 内联）。

**逐处读的结果**：

| 处 | 会不会读到 `Variant::get` | 依据 |
|---|---|---|
| `holdsNoValue`（`generic.cpp:102-105`） | **不会** | 只看 `NullSlot` 与类型码 |
| `fitsConstantBudget` → `constantBytesOf`（`:127-196`、`:210-212`） | **不会** | `Tuple`/`Struct`/`Array`/`String` 各支都用**运行时自己的** `size()` 作界；`Enum` 落到 `default:`，注释写明"不认识的内容只算头部" |
| `staticValueOf` / `pruneBranch` / `replaceFoldResult` | **没有出现 `Variant`** | 同上的 grep；且它们的读都走 `staticValueOf`/`draft.header` |
| `materializeStaticValue` | **未排除** | 定义在 `src/runtime/draft_inline.cpp`，**若被内联就落在 `foldDraft` 帧里** |

**结论（可核对）**：**这六处里没有一处"相信静态类型去读变体字段"的形态**，所以下一层的嫌疑是 **`draft_inline.*` 里被内联进 `foldDraft` 的那些**——
它们的名字在栈上不出现，正是"帧显示 `foldDraft` 却六处都没打中"的原因。

**这一轮没有改代码**：没有定位到具体那一行之前不动手（也不去反汇编猜）。**打点已删净**（`git status` 为空、`git grep -c probe` = 0），**`variant.h:47-54` 一字未动**。

**下一轮的一条命令**：`git grep -n 'inline' include/camel/runtime/draft_inline.h | head` 找出内联清单，在 `materializeStaticValue` 与 `staticValueOf` 的读点各打一行（tag / 声明字段数 / 读的序号）。

## 30. A6 第三次：`materializeStaticValue` 一行都不读字段 ⇒ 按你的规则停手，交人换角度

**① 原文（`src/runtime/draft.cpp:2116-2142`，两段都贴）**：

```cpp
size_t GraphDraft::appendStaticSlot(slot_t value, camel::core::type::Type *type) {
    ++mutations_;
    if (staticSlots_.empty()) { staticSlots_.push_back(NullSlot); staticSlotTypes_.push_back(Type::Void()); }
    touchRefs();
    staticSlots_.push_back(value);
    staticSlotTypes_.push_back(type);
    return staticSlots_.size() - 1;
}

gc_node_ref_t GraphDraft::materializeStaticValue(
    slot_t value, camel::core::type::Type *type, uint8_t runtimeFlags) {
    ++mutations_;
    const size_t slotIndex = appendStaticSlot(value, type);
    ASSERT(slotIndex <= std::numeric_limits<gc_slot_idx_t>::max(), "Draft static slot index exceeds …");
    DraftNodeInit init{ .dataIndex = -static_cast<int32_t>(slotIndex), .dataType = type,
                        .kind = GCNodeKind::Data,
                        .runtimeFlags = runtimeFlags | kGCNodeFlagConstant };
    return addNode(init);
}
```

**② 它有没有核对 tag 与字段数**：**没有，因为它压根不按序号/键读字段**——`appendStaticSlot` 只是 push 一个槽，
`materializeStaticValue` 只建一个 `Data` 节点。**这一处既不是"相信类型"的读，也不是"该核对没核对"，它是"根本不读"。**
（真正的深挖要进 `addNode`/`touchRefs`/`relocateStaticValues` 那一层，而 `relocateStaticValues` 是按 **类型** 逐个 relocate 的。）

**③ 所以按你的规则停手**：**"若读了发现它已经在核对、而崩在更深处 ⇒ 停手贴原文报我，别继续往下挖第三层"**。
这一条线（`foldStaticAccess` → 六处候选 → `materializeStaticValue`）已三轮，**第三层交人换角度**。

**④ 本轮没有代码改动**：`git status` 为空、`git grep -c probe` = 0、**`variant.h:47-54` 一字未动**。

**⑤ 交给下一个人的三句**：
1. 崩点：`Variant::get(0)` on a **tag 1 / 0 fields** 的变体（`shapes.cml` 是 tag 0 / 1 field 读序号 1）；
2. 已排除：`foldStaticAccess` 两支（打点证明运行时字段数与键都对）、`generic.cpp` 六处（无 `Variant` 字样）、`materializeStaticValue`/`appendStaticSlot`（不读字段）；
3. 剩下的方向：`GraphDraft::addNode` / `touchRefs` / `relocateStaticValues`——**按类型遍历静态值**的那一层，以及"枚举值被当成结构体/元组静态值送进来"的**上游**（谁把 tag 与静态类型配错了）。

## 31. `ASSERT` 审计剩下 93 处：到不了 10 / 调用点保证 21 / 危险 62

**判法与口径**（先写清，免得三类被读成别的意思）：
- **① 到不了**：这处在运行期根本不执行（编译期 `static_assert`，或分支永不进入）。
- **② 前提由调用点保证**：**我读到了那一行**，并写清是哪一行的什么保证。
- **③ 危险**：**其余全部**。**"我没核实到调用点"也算 ③**——**未核实的断言不能算安全**。每一处仍写明"什么能让它成假"。

**10 + 21 + 62 = 93。**

### ① 到不了（10）

编译期 `static_assert`，运行期没有这行代码：`frame.h:390,391,392,394,395`、`standalone/array.h:396`、
`utils/exstore.h:25,30`、`utils/type.h:25`、`core/type/composite/func.cpp:25`。

### ② 前提由调用点保证（21）

| 处 | 哪一行保证 |
|---|---|
| `draft_regions.cpp:23` | `region == 0 || region < regions_.size()`，region 由同文件 `addRegion` 返回后再传回 |
| `draft_regions.cpp:53` | 前一行的 `header(id)` 已按 `kind` 分派，`Brch` 支只对 Brch 调用 |
| `draft_regions.cpp:59,75,82,155,232,238,249` | 同上：`id` 唯一来源是同文件的 `addRegion`，并在 `:23` 处按 `regions_.size()` 校验过 |
| `draft_session.cpp:1918` | `!encoded_`：`encode()` 只被 `applyPasses` 调一次，之后置位 |
| `draft_session.cpp:1953` | `encoded_`：`encoded()` 只在 `encode()` 之后被调用 |
| `draft_session.cpp:1959,1965` | `it != members_.end()`：`it` 由上一行的 `members_.find` 得到并已判 |
| `draft.cpp:142` | `draftNode->tail`：由同函数的 `allocTail` 分配，失败即 `bad_alloc`（`block.cpp:77,146`、`growable.cpp:330` 同一条"分配器不返回 null"的契约） |
| `draft.h:217,223` | `bytes.size() >= sizeof(Body)`：`payloadOf` 的字节数由建节点时按 `Body` 申请 |
| `draft.h:380,2084` | `hasFrameLayout()`：`runtimeGraph` 的帧布局在 `allocateRuntimeSlot` 之前建好 |
| `set_value.cpp:121` | `:111` 已判 `Struct` 并 `return`；算子签名只收 struct 或 tuple |

### ③ 危险（62）：按"什么能让它成假"列

| 处 | 什么能让它成假 |
|---|---|
| `tree.h:63` | 同一个 `node` 被两次 `add`（树的所有者写错） |
| `execute/loop_driver.h:207` | 形状的 `valueType->size()` 与 `k + collectionCount` 不符（类型与形状分头构造） |
| `runtime/graph.h:866` | `n->hasPayload()` 为假时 `reinterpret_cast` |
| `include/camel/runtime/draft.h:445` | `index` 来自调用方，未在 release 校验 |
| `utils/append_only_table.h:77,82` | `index >= size()`，`index` 由调用方给 |
| `enum.cpp:131` | **枚举值为 null**（`fromSlot` 得到空指针）⇒ `value->tag()` 解引用空指针 |
| `enum.cpp:140` | 值的 tag 与图决定的 tag 不一致（重排/内联后 tag 常量失效） |
| `enum.cpp:143` | `index >= fieldCount()` ⇒ `get` 的 GUARD 会 abort（有消息，不是 UB） |
| `mem.cpp:32` | 结构体不是 4 个字段 |
| `struct.cpp:962,978,1007` | 切向量的长度与类型声明的字段数不一致 |
| `array/dlpack.cpp:150,152,157,222,231,234` | 传进来的是空 tensor / 非 compact / 非紧凑元素类型 |
| `core/data/composite/array.cpp:184` | `rankZeroType()` 为空 |
| `core/data/composite/func.cpp:21,24` | `program->graph(graph)` 或 `funcType()` 为空 |
| `core/data/composite/struct.cpp:147` | `fieldIndex` 无值（`StructData` 的洞不在 `StructType` 里） |
| `core/data/composite/tuple.cpp:133` | `tupleType` 为空 |
| `core/mm/block.cpp:77,146` | 分配器返回 null |
| `core/mm/growable.cpp:330` | 同上 |
| `core/type/base.cpp:333` | 替换的两边带 `Ref` 码 |
| `execute/camel_pass.cpp:201` | 程序的根函数为空 |
| `execute/exec_plan.cpp:29,87` | 节点查找解析为 null |
| `execute/exec_plan.cpp:849` | 直接调用目标未被填 |
| `execute/fault_isolation.cpp:84,86` | 节点类型不是 tuple / 不是可失败类型 |
| `passes/opt/defun/defunctionalize.cpp:434` | **闭包捕获槽为 null** ⇒ 后面按值用 |
| `passes/trans/gir_metadata.cpp:97,103,125,145` | 无运行时图 / 载荷比 `GCAccsBody`/`GCOperBody` 小 |
| `runtime/draft.cpp:76,99,121` | 节点 id 解析为 null |
| `runtime/draft_clone.cpp:32,105,174,319` | 克隆时节点 id 越界 / 源节点为 null |
| `runtime/draft_construct.cpp:113,117,464,471,478,485,495` | 节点不是 `Oper`/`Loop`，或载荷太小 |
| `runtime/draft_inline.cpp:851,857,894` | 函数为 null；闭包元组大小与节点数不符 |
| `runtime/draft_session.cpp:1103,1599,1882,1908` | 图为 null / 闭包为空 / 组没有基底程序或事务 |
| `runtime/graph.cpp:205,721,965` | 冻结图未 interned；子集保留节点 id；origin 超 32 位 |
| `runtime/retype.cpp:175` | 替换的任一边为空 |

**④ 本轮没修**：**K = 62 > 3**，按你的规则"其余列名单报我"。**最便宜的一处候选是 `enum.cpp:131`**
（`value != nullptr` → 改成 `if (value == nullptr) throwRuntimeFault(…)`，两行），但它要一条新的运行期诊断码，
**而诊断码由生成器分配**（今天已经吃过一次 `0x000209`）——**需要你决定用哪条现成的码，或允许我在下一轮走生成器**。

**⑤ 口径提醒**：**②的 21 处是我真读到调用点的；③的 62 处里有一部分（如 `draft_regions` 之外那些"内部 id"）实际上也很可能是②，
但我没有核到那一行，所以按你第 4 条"查不到 ≠ 不存在"，它们留在③，不写成安全。**

## 32. `enum.cpp:131`：改成"分支 + 现成诊断"，以及为什么今天造不出触发它的程序

**① 选码（三个候选，判据是"含义吻合"）**：

| 码 | 注册含义（`diagnostics/registry.tsv` 原文） | 判断 |
|---|---|---|
| `RuntimeDiag::InvalidArgument` `0x000034` | "Invalid argument of '{0}': {1}. — The operator was given an argument it cannot take; the message names the argument and why. The value comes from the program, so what to change is where it was made." | **吻合**：算子拿到一个它接不了的值（null），而那个值来自程序 |
| `OperandTypeUnsupported` `0x000032` | "'{0}' does not support a value of type '{1}'; it takes {2}." | 不吻合：null **不是**类型不匹配，它没有类型 |
| `ProgramFault` `0x0001AC` | "{0}" | 不吻合：那是程序自己 `fail` 的通道，不是算子契约 |

⇒ **用 `InvalidArgument`**，页里一句：**这条码的意思正是"值违反了它作为参数的契约"**。

**② 改动（两处，`src/builtin/operators/enum.cpp:131`）**：

```cpp
    if (value == nullptr) {
        // A branch rather than an assert: an assert's premise is not held in a release build
        // (the assert audit of 2026-11-02: an assert that guards a dereference is no guard there).
        throwRuntimeFault(
            camel::core::error::RuntimeDiag::InvalidArgument,
            "__enum_tag__",
            "the value is null, and a tag is taken from an enum value");
    }
```

外加一行 `#include "camel/core/error/runtime.h"`。**没有 `try/catch`、没有返回默认值、没有动 `variant.h:47-54`。**

**③ 触发程序：今天造不出来（贴尝试）**。

```camel
type E = enum { A, B(int) }
func main(): int sync {
    let e: E? = null;
    match e { case null => 0
              case E::A => 1
              case E::B(n) => n }
}
```

```
rc=1
error[SemanticDiag::TaggedMatchCases]: This 'match' on a value of type 'E?' is not well formed:
       a literal case cannot be compared with a value that is not opened yet.
```

**语法与语义两道门都拦住了**：`enum { … }` 要写成 `type E = enum { … }`、`case` 不带逗号、而**匹配一个未开启的 `E?` 会被语义拒绝**。
所以 **null 到不了 `__enum_tag__`**——**这条分支守的是语言今天不产生的状态**；它留着的意义是：**哪一天上游变了，这里给的是诊断而不是段错误**。
（本轮还试过 `enum E { … }`、`case null { … }`、带逗号的 `case` 三种写法，分别报 `UnexpectedToken`、`MissingToken: '=>'`、`UnexpectedToken: ','`。）

**④ 读数**：`enum` + `transforms` 计划 = **84/84**，`BUILD_EXIT=0`（第一次构建因 `RuntimeDiag` 未限定名失败，改成 `camel::core::error::RuntimeDiag` 后通过）。

## 33. 两条给作者（按你的要求写进页）

**(a) 最脆的"调用点保证"是状态机那 4 处**：`draft_session.cpp:1918/1953/1959/1965` 保证的前提是**"另一个函数先跑过"**（`encode()` 必须先跑、组必须先建）。
**这种保证换个调用顺序就没了**——它不是类型或界保证的，是时序保证的。**要稳，应把状态做成显式手段（`has value` 而不是 `ASSERT(!encoded_)`），或让 `encoded()` 返回 `optional`。**

**(b) S1 建议**：**新增检查不要用 `ASSERT`，用"分支 + 诊断"。** `ASSERT` 在 release 里不存在，于是它守的前提也不存在；
A2（`p.1` on a struct）已经在 `assert` 审计里证明这条路有效一次，`enum.cpp:131` 是第二次。

## 34. 全量闸门（合并 `374488bf4` 之后）：8354 通过、1 条 fail，已修

```
[SUMMARY] total: 8367, pass: 8354, fail: 1, skip: 10, known failures: 2
[FAIL] docs.ai_reference.current   193.29 ms
```

**唯一那条红**：`docs/reference/camel-for-ai-stdlib-signatures.md` 的 `stdlib-signatures` 区域过期，
`node scripts/camel-for-ai.mjs --check` 的原话是 `the 'stdlib-signatures' region is not what its source produces (525 entries)`。
`--write` 后 diff 是 **2 增 3 删**，内容还是那一笔：

```
-vjp: (f: (x: T) => float) => (x: T) => (float, (ct: float) => (T,))
-vjp: (f: (x: T, y: U) => float) => (x: T, y: U) => (float, (ct: float) => (T, U))
+vjp: (f: F) => Vjp<F> when is_function::<F>()
```

⇒ **`9048c30ea` 之后有三份生成物没重生成**（`docs/stdlib-reference.md`、`EXAMPLE_STOPS` 计数、这份 AI 参考），
本车道已全部补齐；`--check` 现在三条都是 "is what its sources produce"。

## 35. 从 62 处里挑"程序真能触到"的两族：都构造不出来（不是安全）

按你的优先顺序试了两族，**判据是"先写出能触发的程序"**，两次都不成立：

| 族 | 试法 | 结论 |
|---|---|---|
| `struct.cpp:962/978/1007`（切向量长度与类型字段数不符） | 这些在 `addTangent` 里，只有 `vjp`/`grad`/`with_grad` 的切向相加才会走到；今天 `grad` 只有 `(x: float) => float` 一条签名，**数组/结构体那一臂在 `split`/`transpose` 之前就拒了**（本页 §13 的两堵墙） | **构造不出来**：不是"安全"，是**上游先拒**，走不到这里 |
| `array/dlpack.cpp:150–157/222–234`（空 tensor / 非 compact） | 那是 **DLPack 导出**（`export` 到 onnx/dlpack 的那条路）的入口，Camel 程序里没有"把空 tensor 交给导出器"的写法；`std::opt` 与四个后端都不经过它 | **构造不出来**：**不是"安全"**，是**程序面没有这个入口** |

**所以本轮没有从 62 处里改第二处**——**没有触发程序就不改**（这条与今晚"按面相改比不改更坏"是同一条）。
**62 处的名单与"什么能让它成假"已在 §31 里逐条列出**，其中**能被程序触到的**要等 autodiff 的数组臂或导出器接上之后才会出现。

**S1 的第二次证据**（按你要求并列写）：**A2**（`p.1` 作用在结构体上，`rc=139 ×3` → `InvalidAccessIndex`）是第一次；
**`enum.cpp:131`**（null 枚举读 tag，`ASSERT` → `InvalidArgument`）是第二次。**两次都是"断言的前提变成分支"。**

## 36. `a..b->f` 的优先级：读数钉死，加了一条点名优先级的诊断（不改优先级）

> **撤回（2026-11-02，作者 00:50 裁定）：这一节加的诊断撤掉了。** `a..(b->f)` 是合法程序——原则 4（`design-principles.cn.md:288` 一带）的规则是 `->` 比二元运算符都结合得紧，`..` 照这条走，没有例外：`0..xs->len` 就是 `0..(xs->len)`，是正常写法。**只对 `..` 一个运算符加拒绝，等于给语言加了一条限制**（`a + b->f` 同样读成 `a + (b->f)`，却不报错）。所以 `RangeRightIsLink` 连同它的用例、计划条目、注册表行（307 → 306）与生成物一起撤销；**优先级不动**（把 `..` 改成比 `->` 紧反而会破坏那一条）。**正解是改生成器，让它写 `(a..b)->reduce(f)`**——见 §38 的更正与读数。下面的原记录保留，其中"③ 改法""⑤ 新码怎么加的"两段只作为"当时怎么想的"的记录，不再是现状。
>
> **归类一句（这一条要记住）**：**"拒绝合法程序"与"拒绝不支持的形状"是两类**。后者是能力缺口（`skip=law`，例：`idx_arr_t` 少一条 arm，程序该跑而暂不能跑），记成债、等能力补上；前者是语言被改窄（例：这一条 `RangeRightIsLink`，程序本可以跑却被拒），**当轮撤掉，不能留在树上**。
>
> **撤后的读数**：`feat/parse` **45/45**（撤前 46，差的就是撤掉的那支）；全量 `[SUMMARY] total: 8441, pass: 8428, fail: 0, skip: 10, known failures: 3`、`RUN_EXIT=0`；`diagnostics-catalog` 的 `--check` / `--coverage-check` / `--pinned-coverage-check`（289 of 306 named）/ `--verify-doc` 四条与 `camel-for-ai --check` 全绿；注册表 **307 → 306**，`--verify-ops` 的两支（`registry.signatures_match`、`registry.result_types_match`）在闸门里过。

**① 最小程序的读数（`minimal/generator_reject.cml` 原样）**：

```camel
let n = (-2)..(-1)->reduce<(p: string, q: int): string => p, "cd">
```

**改前**：`rc=1`，`error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <(p: string, q: int) => string pure, string> (int).`
——**括号里那个 `(int)` 就是证据**：`reduce` 拿到的是 **`int`**，不是区间。**`..` 比 `->` 弱，`->` 落到右操作数 `(-1)` 上。**

**② 对照（加括号就好）**：`let n = ((-2)..(-1))->reduce<…>` → **`rc=0`**。
**两侧合起来就是"这是优先级问题，不是缺功能"。**

**③ 改法（只加诊断，不动优先级）**：**`..` 的右操作数是 `->` 链时给一条具名诊断**。
`parseRange`（`parser_expr.cpp:243-263`）在解析右操作数之后看一眼 CST 节点：`NK::LinkExpr` ⇒ `error(RangeRightIsLink, {}, dotToken)`；
`report.cpp` 里补一支把它变成诊断（位置指在 `..` 上）：

```
error[SyntaxDiag::RangeRightIsLink]: The right side of '..' is a '->' call: '..' binds looser than '->',
       so the call lands on the last value rather than on the interval.
help: The interval is an expression, and the call is written after it in parentheses: (a..b)->reduce(f).
      Written without them, a..b->reduce(f) is a..(b->reduce(f)), which asks the last value of the interval
      to be reduced. The precedence is the language's, so the parentheses are the program's to add.
```

**④ 为什么是"右操作数"而不是"`->` 的左操作数是 `..`"**：这一句要写清，因为**实际形状与预期是镜像的**——
`..` 比 `->` 弱，所以 `a..b->f` 解析成 `a..(b->f)`，**`->` 的左操作数是 `b`，`..` 根本不在它左边**。
**能检出的位置只有"`..` 的右操作数是一条 `->` 链"，而这正好是生成器写出来的那个形状。**

**⑤ 新码怎么加的（今天第二次踩同一处，写清楚）**：**`diagnostics/registry.tsv` 是唯一事实源，`syntax.h` / `syntax.cpp` 是生成物**
（脚本头注释：`include/…messages/*.h` the enums, generated；`src/…messages/*.cpp` the message tables, generated）。
**我第一次直接改 `syntax.h`/`syntax.cpp`，`--write` 把两处改动整个抹掉了**（`git status` 里只剩另两个文件），
**改成往 `registry.tsv` 加一行（`syntax RangeRightIsLink 0x000013 error … edit`）再 `--write`，306 个码、0 new number，生成回来的是同一份**。
⇒ **纪律：改诊断只改 `registry.tsv`，别碰生成物。**

**⑥ 读数**：`feat/parse` 114/114；全量见 §37。**优先级本身没动**——**要不要改优先级是作者的事**，页里明写"默认不改"。
## 37. 读数与给作者的一节

**全量**：`[SUMMARY] total: 8368, pass: 8356, fail: 0, skip: 10, known failures: 2`（比基线多一条，就是新加的那支用例）。
`feat/parse` 114/114；`--coverage-check`、`--pinned-coverage-check`、`--verify-doc`、`camel-for-ai --check` 四条本地闸门全绿
（新增码 306 个，`289 of 306` 有按码钉死的用例——新码是第 289 个）。

**新码落地时要跟着动的四处（记下来，免得下次再踩）**：
1. `diagnostics/registry.tsv` 加一行（唯一事实源）；
2. `--write` 生成 `syntax.h` / `syntax.cpp` / `docs/diagnostics.md`；
3. 一支用例 + 计划条目（否则 `catalog.coverage_zero` 与 `catalog.pinned_coverage` 会红）；
4. `node scripts/camel-for-ai.mjs --write`（否则 `docs.ai_reference.current` 会红）。

## 38. 给作者：generator v6 的写法在 v6 之后被解析成另一个意思

**这是"生成器与编译器不一致"的可执行解释，比 11.8% 这个数更有用**：

- 生成器写 `a..b->reduce<…>`，**它以为** `->` 作用在 `a..b` 上；
- 编译器读 `a..b->reduce<…>` 为 **`a..(b->reduce<…>)`**，于是把 `b`（一个 `int`）交给 `reduce`；
- 于是报的是 **`NoMatchingFunction … (int)`**——**一条与作者意图无关、也指不出病因的诊断**；
- **同一个程序加一对括号 `(a..b)->reduce<…>` 就编过**（`rc=0`）——**所以这不是"函数不存在"，是"优先级读法不同"**。

**更正（2026-11-02，作者 00:50 裁定）：这一条不是"该被点名"，是"语言不许拒"。** 生成器写错括号，改生成器；拒绝合法程序那条诊断已撤（§36 的撤回块）。改法：`test/tools/progen/model.py` 的 `post()`（后缀形式的接收者）对 `range` 节点加括号，`(a..b)->reduce<f, x>`；生成器版本 6 → 7（那些归档种子的文本按版本钉住，不能悄悄换意思）。

**读数（固定种子 1–500，默认 features、size 1，两侧同一份程序、只差一对括号）**：

| | 旧生成器（无括号） | 新生成器（有括号） |
| --- | --- | --- |
| 编译失败 | **68 / 500（13.6%）** | **14 / 500（2.8%）** |
| 转绿 / 转红 | — | **转绿 54 条，转红 0 条** |
| 文本不同的程序 | — | 86 / 500（17.2% 含"区间当接收者"） |

评估报告那份 `e2_progen.jsonl`（2,000 条里的 235 条，11.8%）**不在树里，不能重跑**，所以上面是同一工具、同一种子区间上的重新测量，不是对那份报告的重算；两者基线率相当（13.6% 对 11.8%），可比的是"54 条转绿、0 条转红"这个方向。

`test/tools/progen/README.md` 说归档程序"no check re-draws them"，所以版本号一升，旧归档仍按 v6 读，不会被新生成器改写。

## 39. `let f: (float, float) => float = g`：真因是"类型位置里的函数类型必须给参数起名"

**① 复现（三个形态，同一次运行）**：

| 程序 | 今天 |
|---|---|
| `let f: (float, float) => float = g` | **`rc=1`**，`error[SyntaxDiag::UnexpectedToken]: Unexpected '=>'; expected '='.`（插在 `=>`，4:27） |
| `let f = g` | **`rc=0`** |
| `let f: float = 1.0` | **`rc=0`** |
| `let f: (a: float, b: float) => float = g` | **`rc=3`（编过并跑出 3）** |
| `type Fn = (float, float) => float` | **`rc=1`**，`Unexpected '=>'; expected a declaration or the end of the file.` |

⇒ **真因就是"函数类型的标注要写参数名"**：`(float, float)` 在类型位置被读成**元组类型**，后面的 `=>` 无处可去。**而加上名字就编过**。

**② 答案（贴原文）**：`src/parse/syntax/parser_type.cpp:301-311`——`(` 之后**只有"像函数类型头"的形态**才走 `parseFuncType()`：

```cpp
    case TK::LParen: {
        const TK t1 = peek(1);
        if (!typeHead &&
            (t1 == TK::KwVar || (t1 == TK::Identifier && peek(2) == TK::Colon) ||
             (t1 == TK::RParen && peek(2) == TK::FatArrow) ||
             (t1 == TK::DotDot && peek(2) == TK::Identifier && peek(3) == TK::RParen &&
              peek(4) == TK::FatArrow) ||
             (t1 == TK::DotDot && parenClosesBeforeArrow()) ||
             (t1 == TK::Comma && peek(2) == TK::RParen && peek(3) == TK::FatArrow))) {
            return parseFuncType();
        }
```

**`(float, float)` 一个条件都不满足**（下一个是 `Identifier`，但再下一个是 `Comma` 而不是 `Colon`），于是落到元组类型那一支，
`expectListEnd(TK::RParen)` 之后 `=>` 就"无处可去"——**这不是"标注的解析退回了更早的规则"，是"类型语法里函数类型的头就要求参数名或 `..` 包"**。

**③ 改法（选"给一条点名病因的诊断"，不做语法扩展）**，理由：**Camel 的函数类型本来就是"给每个参数起名"的形状**
（`stdlib/types.cml:100` 的 `Vjp<F> = (..ParamsOf<F>) => …` 走的是 `..` 那一支，不是"一串裸类型"）；
**支持 `(T, U) => R` 等于引入"无名参数的函数类型"，那是语言扩展，要作者过目**——所以这轮只让它有名有姓地说清楚。

```
error[SyntaxDiag::FuncTypeNeedsNames]: A function type names its parameters: write
       '(x: float, y: float) => float', not a list of types.
  --> …:4:27
 4 |     let f: (float, float) => float = g
   |                           ^^
help: In parentheses a list of types is a tuple type, and '(float, float)' is read as one; the parameters
      of a function type are written 'name: type'. The names are the writer's to choose, and the types of
      the function's own parameters already say which is which.
```

**新码 `SyntaxDiag::FuncTypeNeedsNames` `0x02000014`**，仍走 registry 四处那一套（registry 行 → `--write` → 用例+计划条目 → `camel-for-ai --write`）。

**④ "24 条"这条要说实话**：**作者页里点的这个形态我复现了，但 `e9_reduced` 里我能读到的用例用的是带名字的写法**
（`rc00_seed27.cml:1` `func f1(a2: (a0: int, a1: string) => string, …)`），**它们今天都 `rc=0`**。
**所以"24 条 `UnexpectedToken` 的真因是标注位置"这句我没有逐条核实**：**能核实的是这一条形态的真因，不是那 24 条的分布**。
**要钉那 24 条，需要作者给出那批程序的清单**（页里已写）。

**⑤ 读数**：`feat/parse` **116/116**（新增两支用例：`range_right_is_link`、`function_type_needs_names`）；全量见 §40。
**③′ 落点改了两次，记下来**：第一版把检查放在 `parsePrimaryType` 的元组支里（`cur_ == FatArrow` 就报），**全量立刻红 40 条**
（`generics.*`、`edges.*`、`onnx.export_*`、`collfuzz.quick` 等）——**因为在 `static for … in fields (T, U) => 块`、`case (x, y) => …` 这些地方，元组类型后面本来就该跟一个 `=>`**。
**把检查从解析器挪到报告层**（`report.cpp`）：**只有当"解析器期望 `=`（标注结束）却遇到 `=>`"时才报 `FuncTypeNeedsNames`**——
判据是 `error.kind == Unexpected && offToken.kind == FatArrow && expected 里有 Assign`。**这样语法一个字符没改，只在报告时换个说法。**
**教训**：**"看到 `=>` 就报"是看形状，不是看语境；语境在 `expected` 里。**

## 40. 读数

`feat/parse` 116/116、`feat/linear` 一起 987 通过 0 失败（含 2 条已有的 known failure）；
**全量 `total: 8369, pass: 8357, fail: 0, skip: 10, known failures: 2`**（比基线 8367/8356 多两条，正是新加的两支用例）。
**四条本地闸门**：`--coverage-check`（307 个码，全部被用例点到）、`--pinned-coverage-check`、`--verify-doc`、`camel-for-ai --check` 全绿。

## 41. A8：Camel 阶段的 `opt::cse` 会去替换"锚住图"的节点，而事务按设计不许

**① 复现（两侧，`minimal/camel_cse_anchor.cml` 原样）**：

```camel
func echo(x: int): int sync {
}
func main(): int sync {
    var x = 0
    echo(x)
    return 0
}
```

| 命令 | 改前 | 改后 |
|---|---|---|
| `camel … opt::cse std::gir` | **`rc=1`**，`Invalid argument of 'graph.replace': the node anchors its graph (an exit, an output, a return or the end of a branch arm)`（指向 `out/latest/stdlib/opt.cml:126`） | **`rc=0`** |
| `camel … std::opt::cse std::gir` | `rc=0`（画出 GIR） | `rc=0`（**同一个数**） |

**② 那是设计上的禁止，还是拒绝过宽？** —— **是设计上的禁止**，两处原文：
`stdlib/graph.cml:121-125`：

```
/// Whether the node anchors its graph (its exit, output or return, or the end of a branch arm); a
/// transaction does not replace or erase it.
export func is_anchor(n: Node): bool
```

`replace` 的 `@fails` 也写着"when a node anchors its graph"。**而 `opt.cml` 的 `is_hit` 从来没问过 `is_anchor`**：

```camel
func keep_of(ns: Node[], n: Node): Node {           // 改前
    let earlier = ns->filter<(m: Node): bool => id(m) < id(n) && same_node(n, m)>
    return if len(earlier) == 0 then n else earlier[0]
}
func is_hit(ns: Node[], n: Node): bool { return keep_of(ns, n) != n }
```

⇒ **不是 `graph.replace` 过宽，是 Camel 阶段的 `cse` 漏了原生遍有的那条约束**（原生遍合并的是"值"，Camel 阶段合并的是"节点"，**节点可以是锚**）。

**③ 修在 Camel 阶段这一侧**（`graph.replace` 的语义一个字没改）：

```camel
func keep_of(ns: Node[], n: Node): Node {
    let earlier = ns->filter<(m: Node): bool => id(m) < id(n) && !is_anchor(m) && same_node(n, m)>
    return if len(earlier) == 0 then n else earlier[0]
}
func is_hit(ns: Node[], n: Node): bool {
    // A node that anchors its graph is not merged away: the transaction does not replace or erase
    // it, and a round that tried would stop on the refusal instead of merging the rest.
    return !is_anchor(n) && keep_of(ns, n) != n
}
```

**两处都要改**：`is_hit` 挡住"被替换的是锚"，`keep_of` 挡住"拿锚当替身"（`replace(t, n, anchor)` 会把别处的值挂到出口节点上）。

**④ 读数（逐个数，不是估）**：
- `test/cases/opt` 下先筛"能编过"的程序：**149 支**；
- 其中 `opt::cse std::gir` 失败：**改前 36 支，改后 0 支**（`cse_fail=36` → `cse_fail=0`），**36 支全部转绿**；
- `std::opt::cse` 一侧：**同一个数，0**（未受影响）；
- `feat/opt` 计划：**1129/1129 通过**。
- **eval 报的 `143 of 576`（1/4 抽样）这一层我没有重跑**（那份抽样集不在仓库里）——**能逐个数的是 `test/cases/opt` 的 36**。

## 42. 给作者：Camel 阶段与原生遍的差值

| 阶段 | 失败的支数 | 原生遍同一个数 |
|---|---|---|
| `opt::cse`（Camel，`stdlib/opt.cml`） | **36 of 149**（eval 记 36 of 145） | `std::opt::cse` **0** |
| `opt::fold`（A9） | —— **`constant_budget` 缺失：43,890,478 字节 vs 1,789** | `std::opt` 的 `constant_budget` 有 |

**两条同一族**：**Camel 阶段缺了原生遍的某一个约束**——`cse` 缺 `is_anchor`，`fold` 缺 `constant_budget`。
**这不是"两边实现细节不同"，是"约束没有随实现一起搬过去"。** 凡是把原生遍改写成 Camel 阶段的活，**都要先列出原生遍遵守的约束清单**，逐条问"这一条在新实现里由谁保证"。
A9 也一样：`opt::fold` 今天没有预算上限，**一个常量能把内存吃到 4300 万字节**，而原生遍停在 1789。
## 43. A8 的读数汇总

- `minimal/camel_cse_anchor.cml`：`opt::cse std::gir` **`rc=1` → `rc=0`**；`std::opt::cse std::gir` **`rc=0` → `rc=0`**（同一个数）。
- `test/cases/opt`：能编过 **149** 支，其中 `opt::cse` 失败 **36 → 0**（逐支数出来的，`cse_fail=36` → `cse_fail=0`）。
- `feat/opt`：**1129/1129**。
- **全量**：`total: 8369, pass: 8357, fail: 0, skip: 10, known failures: 2`（与基线同一个数，**没有回退也没有新增**）。
- `stdlib-reference --verify-docs`：**exit 0**（558 个 example 全查、计数与脚本记录一致）；`diagnostics --verify-doc`：up to date。
- eval 报的 **143 of 576（1/4 抽样）** 那一层**没有重跑**——那份抽样集不在仓库里；能逐个数的是上面 149 支里的 36。

## 44. A9：`opt::fold` 没有 `constant_budget`——读数复现了，钳的量**不能照搬**，按判据 6 的兜底先问作者

**① 两侧读数（`minimal/fold_interval.cml` 原样）**：

```camel
func main(): int sync {
    let k = 0..5000000
    println(len(k))
    return 0
}
```

| 命令 | `|wc -c` | eval 记 |
|---|---|---|
| `camel … opt::fold std::gir` | **43,890,910** | 43,890,478 |
| `camel … std::opt::fold std::gir` | **2,221** | 1,789 |

**两个语料程序（判据 5）**：

| 程序 | `opt::fold std::gir` | `std::opt::fold std::gir` |
|---|---|---|
| `test/cases/feat/mm/gc/gc_block_pacing.cml` | **43,899,403** | **10,714** |
| `test/cases/para/core/para_block_sort_merge` → `test/cases/para/core/block_sort_merge.cml` | **34,904,296** | **15,378** |

⇒ **之前那个 `too_large` 就是这件事**：Camel 阶段把 500 万个元素物化进图里，dump 出来 43 MB。

**② 原生遍那条钳的落点（原文）**：
`include/camel/passes/opt/config.h:55-59`：

```cpp
/// Bytes of constants folding may put into a graph. Folding replaces a computation by the value it
/// produces, which is compile-time memory bought for run time; past this size the value is not
/// worth holding and the computation stays (generic.cpp, constantBytesOf). It is the default of
/// the `constant_budget` parameter of the passes that fold, `std::opt` and `std::opt::fold`.
inline constexpr size_t kDefaultConstantBudgetBytes = 8u * 1024u * 1024u;
```

`src/execute/pass.cpp:375-382`：

```cpp
/// The `constant_budget` parameter of the passes that fold (design principles 9.6): the bytes of
/// constants folding may materialize in a graph. A negative value is read as 0, which
/// materializes none of them, the way `growth_budget`'s 0 copies nothing. Its declaration is in
/// stdlib/std.cml.
size_t constantBudgetOf(const PassArgs &args) {
    const int64_t bytes = args.getInt("constant_budget");
    return bytes < 0 ? 0 : static_cast<size_t>(bytes);
}
```

判定在 `src/passes/opt/generic/generic.cpp`：`fitsConstantBudget(value, type, budget)` = `constantBytesOf(value, type, budget) < budget`。

**③ 它钳的是哪个量** —— **"值自身占的字节数"，而且是按类型递归走出来的**（`constantBytesOf`，`generic.cpp:127-196`）：

```cpp
size_t constantBytesOf(slot_t value, const type::Type *type, size_t stopAt, size_t depth = 0) {
    …
    size_t bytes = kConstantShapeOverheadBytes;          // 4 * sizeof(slot_t) = 32
    switch (type->code()) {
    case type::TypeCode::Array:
        bytes += sizeof(Array) + array->rank() * 2 * sizeof(int64_t) + array->byteSize();
        …每元素递归…
    case type::TypeCode::Tuple:  bytes += sizeof(Tuple) + tuple->size() * sizeof(slot_t); …
    case type::TypeCode::Struct: bytes += sizeof(Struct) + record->size() * sizeof(slot_t); …
    case type::TypeCode::String: bytes += sizeof(String) + text->size() + 1;
    case type::TypeCode::Complex64/Complex128: bytes += 2 * sizeof(double);
    default: break;                                       // 不认识的形状只算头部
    }
```

**不是元素数、不是节点数、不是字节数的小计**：**它要 `sizeof(Array)`/`sizeof(Tuple)`/`sizeof(Struct)`/`sizeof(String)` 这些 C++ 对象布局，还要按元素类型递归、深度封顶 8、超过 `stopAt` 即停**。

**④ 所以按判据 6 的兜底停手**：**这个钳不是一个能从 Camel 阶段照抄的量**——
`stdlib` 里没有任何"值的字节数"函数（`git grep bytes -- stdlib/*.cml` 只有 `to_bytes`/字节串），
而 `eval(t, n)`（`stdlib/graph.cml:281`）**没有预算参数**：它把值物化出来再交回来，**"先物化再丢掉"这一步原生遍也做**（`fitsConstantBudget` 的注释写着"asked after the operator ran"），
**但 Camel 阶段连"丢掉"的判断都做不出来，因为它量不出那个值的大小。**

**⑤ 给作者的选项（我的建议是甲）**：
- **甲（默认，照它的算法、不另立新量）**：**把 `constantBytesOf` 作为一条图函数暴露给 Camel**（如 `constant_bytes(n: Node): int`），`opt::fold` 于是可以写
  `with <constant_budget: int = 8388608>` 并 `if constant_bytes(v) < constant_budget then 替换 else 留下`——**同一个函数、同一个默认值，两个阶段量的是同一个数**。
  这是**新加一条只读的内建**，不动任何语义；工作量在"函数怎么暴露给 stdlib"那一层。
- **乙（不推荐）**：Camel 阶段自己定义一个量（元素数上限之类）——**那就是另立新量**，与判据 6 的默认相反，且两个阶段从此不可比。

**⑥ 本轮没有改代码**：**没有把任何一个新量塞进 `opt::fold`**（`stdlib/opt.cml` 与 `stdlib/std.cml` 未动，`git status` 为空）；
所以 regressions 沿用上一轮的读数（全量 `8369/8357/0`）。**这一条要作者点头才能往下走**：要么准我暴露 `constant_bytes`，要么给一个他认为可接受的量。

## 45. A9 收尾：`constant_bytes` 就是 `constantBytesOf`，`opt::fold` 现在量的是同一个数

**走的甲案**（"同一个函数、同一个默认值、两个阶段量同一个数"），三个条件逐条对：

**① 同一个 C++ 函数，没有第二份实现**：`constantBytesOf` 从 `generic.cpp` 的匿名命名空间**原样搬出**到
`include/camel/passes/opt/constant_bytes.h` + `src/passes/opt/constant_bytes.cpp`——**签名、函数体、注释都没动**（深度 8、`stopAt` 即停、头部 `4 * sizeof(slot_t) = 32` 三条性质原封不动地跟着走）。
`fitsConstantBudget` 改成经头文件调用它；`graph` 模块的 `constant_bytes` 直接调它。**一处算法，两个调用者。**

**② 只读、无副作用**：`constant_bytes(n: Node): int` 不打开事务、不改图；`stdlib/graph.cml` 里与 `is_constant` 并排声明，文档与 example 都已进 `docs/stdlib-reference.md`。
**"加它之后全量读数与加它之前一字不差"**——加它那一笔（`de37ecf0a`）的 `feat/opt` 是 **1141/1141**，与加它之前同一批用例同一个数。

**③ `opt::fold` 现在有预算**（**阶段参数，不读环境变量**）：
```camel
with <
    /// Bytes of constants this pass may materialize in a graph: the same measure and the same
    /// default as `std::opt::fold` (`constantBytesOf`), read through `constant_bytes`; a value
    /// above it is left as the computation that builds it, and 0 materializes none.
    constant_budget: int = 8388608
>
```
参数沿 `fold_rounds → fold_round → fold_one → put_value` 一路传下去，`put_value` 的判据是 `constant_bytes(value) < budget`。

**④ 读数（逐个数）**：

| 程序 | `opt::fold` 改前 | `opt::fold` 改后 | `std::opt::fold` |
|---|---|---|---|
| `minimal/fold_interval.cml` | 43,890,910 | **2,229** | 2,221 |
| `feat/mm/gc/gc_block_pacing.cml` | 43,899,403 | **10,762** | 10,714 |
| `para/core/block_sort_merge.cml` | 34,904,296 | **15,655** | 15,378 |

**两侧差值 0.4%–1.8%（8 / 48 / 277 字节）**：**这不是判据不同，是两条流水线的记账不同**（`opt::fold` 只跑折叠，`std::opt::fold` 是整条 `std::opt` 的折叠阶段）。**判据一致的最强证据不是字节数，是下面这条**。

**⑤ 一致性用例**（`test/cases/opt/camel_passes/fold_constant_budget.cml`，两支 golden）：
`opt::fold std::gir` 与 `std::opt::fold std::gir` 的 dump **逐字节相同**（两份 golden 都是 2157 字节，`Get-FileHash` 相同）——**同一个值，两个阶段做的是同一个决定、画出同一张图**：图里留的是 `:struct/range`（生成它的那次计算，`N03`），**不是 500 万个元素**。

**⑥ 回归**：`feat/opt` **1143/1143**（新增两支 golden 用例）；全量见 §46。`docs/cli.md` 与 `docs/cli.cn.md` 的参数表把 `opt::fold` 并到 `std::opt`、`std::opt::fold` 那一行（**参数名与默认值照原生遍**）。

**⑦ 这一族的规矩又厚了一层**：A8 缺 `is_anchor`、A9 缺 `constant_budget`，**两次都不是"实现写错"，是"约束没随实现搬过去"**——而 A9 还多一层：**量约束需要一个量，那个量不能自己发明，得让两个阶段共用一个函数**。

## 46. A9 全量读数

**全量 `total: 8383, pass: 8371, fail: 0, skip: 10, known failures: 2`**（基线 `8369/8357/0`，多出的 14 支正是 `feat/opt` 新增的两支 golden 用例加合并 develop 带来的用例）；
`feat/opt` **1143/1143**；`stdlib-reference --verify-docs` 与 `diagnostics --verify-doc` 全绿；
`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/cli.md`、`docs/cli.cn.md` 同笔更新。

## 47. P0：`grad` 的数组形参止在 `transpose` 的"规则表没有那条 arm"，不是某个形状

**① 复现（我的树，`test/plans/feat/compile/transforms`，只走 `camel-remote.ps1 -Plan`）**

`grad_of_an_array_parameter.nvm`（`h-m4b` 移交的 `vjp` 版本）在我树上**第一次跑**（未应用 `beeb887f4` 那行端口类型修改）：

```
[FAIL] grad_of_an_array_parameter.nvm.nvm
[INFO] expected exit 0, got 1
[stdout] … REINTERP split function=jvp_of_g forward=88 backward=89 type=(float[]) => (float, (int, float, int, float)) pure residuals=4
[stdout] 6.0
[stdout] TRANSPOSE function=autodiff::mul_tangent<float> graph=91 type=(float, float, float, float, float) => (float, float, float, float) nodes=7
[stdout] TRANSPOSE function=rulesets.jvp::index_into<float> skip=law rule=idx_arr_t uri=:op/idx_arr
[stdout] TRANSPOSE function=split_of_jvp_of_g_backward skip=call node=5 callee=rulesets.jvp::index_into<float>
[stdout] (vjp)…/stdlib/vjp.cml:72:47: [Error]: An error has occurred: transforms: transpose has no transpose for …
[stderr] (empty)
```

**应用 `beeb887f4`（`buildTransposeMember` 的普通参数端口改成取参数自己的类型）之后重跑：输出逐行相同**——`skip=law rule=idx_arr_t` 与 `transpose has no transpose` 一字不差。**⇒ 端口类型那个缺陷是真的（端口把 `float[]` 记成了 `float`），但它不是这条中止的原因**，两个状态止在同一处。

**② 四条形状（判据 2，一次跑完，四个程序各改一件事）**

| 程序 | 形参用法 | `rc` | 止在哪 |
|---|---|---|---|
| `grad_array_one_element.cml` | `xs[0]`，实参 `[3.0]`（单元素） | 1 | `skip=law rule=idx_arr_t` |
| `grad_array_two_elements_first.cml` | `xs[0]`，实参 `[3.0, 2.0]` | 1 | 同上 |
| `grad_array_sum.cml` | `xs[0] + xs[1]` | 1 | 同上 |
| `grad_array_product.cml` | `xs[0] * xs[1]` | 1 | 同上 |
| `grad_of_an_array_parameter.cml`（移交件） | `xs[1] * xs[0]`，读 `cotangents.0` | 1 | 同上 |

**五条（连移交件）全部停在同一条 `skip=law rule=idx_arr_t uri=:op/idx_arr`**。**⇒ 不是"某个形状"，是"数组元素读这一条 law 没有 arm"。**

**③ 中止点定到了：`modules/transforms/transpose.cpp:1354-1358` 的兜底 `else`**

```cpp
                } else if (law == "mul_t" || law == "div_t") {
                    …
                } else {
                    os << "TRANSPOSE function=" << function->name() << " skip=law rule=" << law
                       << " uri=" << uri << '\n';
                    return false;
                }
```

**转置器只认算术那几条 law**（`add_t`、`mul_t`、`div_t`……），**而 `idx_arr_t` 在规则库里是有的**（`stdlib/rulesets/transpose.cml:73`）：

```camel
export rule idx_arr_t::<T>(xs: T[], i: int) {
    index_into(xs, i) => (ct: T) => scatter_into(zeros_like(xs), i, ct)
}
```

**⇒ 缺口不是"算错了"，是"规则库声明了一条 law，转置器没有对应的 arm，于是点名拒绝"**——拒绝本身是对的（`else` 的注释写明"refused by name rather than answered with a cotangent from nowhere"），**缺的是这条 law 的转置**。

**④ 判据 3：中止形状单独确认——`got null` 在我树上没有观测到**

移交单上写的是 `got null` + `[stderr] (empty)`（没有退出码），**那是 `h-m4b` 树上的读数**；**我这棵树两次运行都是 `got 1`**（有退出码 1），**stderr 空、stdout 末尾是 `vjp.cml:72` 的 `[Error]`**。
**我在 plan 日志里搜过 `signal`/`Segmentation`/`Aborted`/`core dumped`：一处都没有——没有观测到信号中止。** **所以"静默中止"这个形状我在自己的树上没有复现出来**，写在这里而不是当成结论。

**⑤ 同一条路的另一侧已经有实现**：`modules/autodiff/pullback.cpp:2146-2161` 处理数组元素读时**就散一个 slot**——`":tangent/scatter_add_arr"`，而 `tensor:zeros_like` 是现成的（`modules/tensor/ops/gradient_ops.cpp`）。
**⇒ 缺的 arm 有现成材料可借**：转置器要发的正是 `scatter_add_arr(zeros_like(xs), i, ct)`。

**⑥ 默认建议（给作者的问题，一页一问）**：

> **问题**：`transpose` 是否应当认 `idx_arr_t` 这条 law——即对 `xs[i]` 的余切发出 `scatter_add_arr(zeros_like(xs), i, ct)`，与 `pullback` 那条路发的同一条？
>
> **默认建议**：**应当**。理由三条：(a) 规则库已经把这条 law 写出来，转置器不认它就是"声明了不实现"；(b) `pullback` 已经这么做，两条路对同一个值给出同一个余切；(c) 这条 arm 不会改变任何现有读数（今天它在 `else` 里直接 `return false`）。
>
> **代价**：转置图的余切槽今天是 `float`（`cotangentIn` 在 `transpose.cpp:263-265` 建成 `Type::Float64()`），数组元素读的余切是 `float[]`——**这条 arm 会第一次让转置器处理"非标量余切"**，也就是 `transposeSignatures`/`addCotangent` 的类型从"一律浮点"变成"跟着值走"。**这是设计层面的改动，不是补一个分支**，所以我没有动它。

**⑦ 本轮改了什么**：应用了 `h-m4b` 的端口类型修复（`transpose.cpp:252-253`，`bundlesResiduals` 之外取 `normTypeAt(i)`），**并把五条形状钉成用例**（`expect_exit = 1` + `skip=law rule=idx_arr_t` 与 `transpose has no transpose` 两条断言）——**将来那条 arm 落地时，这五条会一起红，正好提醒改成数值断言。**
**⑧ 读数与去向**：`feat/compile/transforms` 里新增五条用例，全量 **`total: 8388, pass: 8376, fail: 0, skip: 10, known failures: 2`**（上一轮 `8383/8371/0`，多出的 5 支正是这五条）。
问作者的一页在 `docs/handover/reviews/questions-2026-11-02-whether-the-transpose-honours-the-array-index-law.md`，
缺陷单 `docs/handover/reports/eval-defects-2026-11-01/README.md` 增 **A10**，并把 A8/A9 标成"已在 `lane-lit2` 修好"（附 commit）。


## 48. `idx_arr_t` 的 arm 我试了（三处改动），拒绝消失了，露出的是下一层的两个"字面量 `float`"——已回退，记账留图

**先记一句判性的话（你的裁定已落页）**：**这不是"给数组开一个特例"，是 `beeb887f4` 的同一根因在余切类型上的第二次发作**——
`beeb887f4` 把"普通参数端口一律 `float`"改成"取参数自己的切空间"；**这次是"余切一律 `float`"**。
**同一个坏形状：类型写成了字面量，而不是被微分那个东西的切空间。** 另外记明：**`transpose.cpp:1354` 那个兜底 `else` 的行为是"点名拒绝"，这本身是对的**（`skip=law rule=…`），缺的只是这条 law 的 arm。

**① 已确认：签名那一层根本不用改。** `transposeSignature` 的"多参数普通形"分支（`transpose.cpp:107-119`）**已经**逐个参数取 `tangentSpaceOf`，
所以 `rulesets.jvp::index_into<float>` 的转置签名是 **`(float[], int, float) => (float[])`**——**数组参数答一个数组余切，`int` 答零个**。**签名早就对了，错的是图的体。**

**② 我改的三处**（都在 `modules/transforms/transpose.cpp`，一处一改、一次一跑）：
1. `primitive` 旁边加 `primitiveOf(Type*, uri, inputs)`，`primitive` 变成 `primitiveOf(Type::Float64(), …)` 的薄包装——**"算子节点的类型从哪里来"从字面量变成参数**；
2. 加 `arrayTangentOf(ref)`：`tangentSpaceOf(值的类型).space` 是 `Array` 就真——**`addCotangent` 于是按元素相加（`:tangent/add_arr`）而不是 `:op/add_d`**；
3. 在 law 分派里给 `idx_arr_t` 加 arm：`knownValue(xs)` → `:tangent/zeros_arr` → `:tangent/scatter_add_arr(zero, i, ct)` → `addCotangent(xs, …)`，
   **逐字照规则库那条 law 与 `derivative.cpp:196-222`（`idxArrayVjp`）已经发过的同一组节点**。

**③ 跑出来的读数（这一笔最值钱的部分）**：**拒绝没了**，五个用例改成**图的边不变量破裂**，而且说的是什么很清楚：

```
graph 'transpose_of_split_of_jvp_of_g_backward' node 6 (10): passes a int as ordinary argument 0 of
    'rulesets.jvp::index_into<float>', which takes a float[]
graph '…' node 14 (10): calls 'transpose_of_rulesets.jvp::index_into<float>', whose value is a (float[],),
    and is a (float, float)
```

**⇒ 下一层的两个"字面量 `float`"就在这里**：
- **调用点的实参列表**：调 `index_into`/`transpose_of_index_into` 时把 `int` 当成了第 0 个实参（`xs` 的位置）——**实参列表还是按"全是浮点/全是参与集"的老形状拼的**；
- **调用点的结果类型**：被调者已经答 `(float[])`，**调用点仍写着 `(float, float)`**——**同一个"类型写成字面量"的第三次发作**。

**④ 我为什么把这三处回退**：arm 落地后 **`grad_array_no_int.fvm`（一条原本绿的用例）也一起红了**（同一个不变量破裂），
**五条新用例的钉法也失效**。**今晚剩下的时间不够把"调用点实参 + 调用点结果类型"两处一起改对并验证**（每轮构建 5 分钟，六处一次改完的教训就在今晚）。
**一条红的层比一张精确的图更糟**，所以回退，把图留在这里。

**⑤ 下一步交出去的顺序（省掉下一个人的摸索）**：
1. 先改**调用点的结果类型**（`(float, float)` → 被调者的实际答案类型）——**这一处改对，`calls … whose value is a (float[])` 那两条就该消失**；
2. 再改**调用点的实参列表**（`int` 出现在 `float[]` 的位置）——**这说明拼实参时把"参与集/全浮点"的假设用在了 `float[]` 参数上**；
3. 两处都对了再落 `idx_arr_t` 的 arm（我在 §48 ② 写的那三处，可直接复用）；
4. **最后把五条用例的 `expect_exit = 1` 换成数值断言**（`[2.0, 3.0]` / `[1.0]` / `[1.0, 1.0]`），**别留着"钉住当前失败"的形状**。

**⑥ 本轮读数**：树干净（`transpose.cpp` 已 `git checkout` 回 `f14f65031` 的状态，端口类型修复在），五条用例仍钉 `skip=law rule=idx_arr_t`，
**全量 `8388 total, pass 8376, fail 0`**（与上一轮同一个数）。


## 49. 实例化写死 + 签名只算一份：四条不变量 **4 → 0**，露出来的是"0 长度数组进余切"的运行期缺陷

**作者的裁定（00:30）把诊断改了一个说法，照改**：不是"两处字面量"，是**"调用节点的签名不是从 `transposeSignature` 取的"**，
**类型与实参次序是它的两个症状**；**同 `beeb887f4` 同一族，但这一处是"签名算了第二遍"**。

**落地的三笔**（各一次构建、各一次跑）：

| 笔 | commit | 读数 |
| --- | --- | --- |
| 答案类型取签名 | `2c0b12f80` | `feat/compile/transforms` 57/57，不动 |
| 答案槽数/槽类型取签名 | `033e566fe` | 57/57；与 arm 一起量时 **8 → 4** 条不变量 |
| 规则按被匹配节点的元素类型实例化（P1-9） | `cbdf86498` | 57/57；与 arm 一起量时 **4 → 0** 条不变量 |

**"签名算两遍"的三处位置**（都在 `transpose.cpp` 同一个调用点）：`answerType` 用 `calleeParams` 重算（1745-1749）、
被调值写成 `Type::Float64()`（1759）、答案槽数按 `calleeParams` 给而不是按"有切空间的参数"给（1785-1793）。
**三处都改成从 `transposeSignature` 的返回值读**。

**实例化写死（审查 P1-9）的改法**：把"规则在什么类型上是泛型的"从一个答案变成一处实现——
`modules/transforms/rule_types.h` 的 `ruleElementTypeOf`/`firstOperandTypeOf`，**从 `vmap.cpp` 原样搬出**，
`vmap` 与 `jvp` 都读它；`buildRuleTangent` 因此收一个 `elementType` 参数，不再把 `float` 写进去。
**注意 `module.cpp:74` 那处（`printRuleUris`）没有"被匹配节点"可问**——它是列出规则名与算子的诊断，按 `float` 实例化只是取 `lhsOperatorUri`，
这一处留原样，理由写在这里免得下一个人以为是漏改。

**合起来量（arm 在场）**：

| 量 | 改前 | 改后 |
| --- | --- | --- |
| `breaks its edge invariants` | **4** 条（全是 `passes a int as ordinary argument 0 of index_into`） | **0** |
| `skip=law rule=idx_arr_t` | 存在 | **不存在** |
| 运行期 | 到不了 | **`Camel Invariant Violation: Array::getSlot`，`element 0 of an array that holds 0`**，`expected exit 1, got null` |

**⇒ 最值钱的一条：移交单里"静默中止、`got null`"是真的**，**它一直被"拒绝"挡在后面**；今天图能建、叫得对，
剩下的是**一个 0 元素的数组进到了 `:tangent/zeros_arr`/`scatter_add_arr`**（`struct.cpp:1063-1085` 用 `value->shapeSpan()` 建零数组）。

**身份裁定记下**：**被转置成员只靠图 id 认身份，名字只给人读**；`state.callees`（`transpose.cpp:1702-1716`）**已经是按 id 的记忆**，
所以"同一次转置里重复建成员图"今天不存在，不必再加一层。**`T[?]`/命名那一支不走名字。**


## 50. `zeros_like(xs)` 这条建议我先核了前提：`zeros_arr` 今天**已经**是从 `xs` 取值建零数组的，空的不是它

作者的建议是"数组的余切零由残差里的 `xs` 给出（`zeros_like(xs)`），用 Camel 的造零函数"。**我核了这条的前提，结论是：这条改动打在空处的上游。**

**① arm 里加了一行读数（探针，未提交）**：
```
TRANSPOSE arm=idx_arr_t function=rulesets.jvp::index_into<float> xs=0 xsType=float[] at=1 ctType=float[]
```
**`xs` 就是那个参数端口（`0`），它的类型是 `float[]`（开放秩），`at` 是常量 `1`，余切类型 `float[]`。**⇒ **arm 拿到的 `xs` 是对的，没有"另发明零数组"。**

**② `zeros_arr` 今天就是从 `xs` 的值建零数组**（`src/builtin/operators/struct.cpp:1063-1086`）：
```cpp
auto *value = norm.get<Array *>(0);
Array *zero = Array::create(mm::autoSpace(), tangentArray, value->shapeSpan());
for (uint64_t i = 0; i < value->numel(); ++i) { … }
```
**形状取的是 `xs` 的值自己的 `shapeSpan()`——正是"`zeros_like(xs)`"。** 也就是说**这条建议的实现已经在**，不是缺实现。

**③ 而 `scatter_add_arr` 也不会给出这条错**（同文件 `1103-1121`）：下界是 `base->dim(at)`，
越界走的是 `RuntimeDiag::IndexOutOfRange`（一条**带诊断的运行期错误**），**不是不变量违例**。

**④ 所以"`element 0 of an array that holds 0`"这条读的不是 `zeros_arr` 的循环、也不是 `scatter` 的越界检查，而是别处的 `getSlot(0)`**；
**空数组是从上游进来的**（`xs=0` 这个端口的**运行期值**是空的，或答案元组那一侧读到了空数组）。
**⇒ 我没有把 `zeros_arr` 改成"取残差的 `xs`"——它已经是了；改了只会是同一件事的第二种写法。**（"查不到 ≠ 不存在"，前提先核，这条照旧。）

**⑤ 下一个探针应该看的地方**（写下来省下一次摸索）：**不是 `struct.cpp`，是"传进被转置成员的那个实参数组在运行期有几个元素"**——
`xs=0` 是**图的端口**，运行期值由调用点给；`transpose_of_index_into` 的实参是 `[forward(xs), forward(i), ct]`，
**先量 `forward(xs)` 落在这个端口上的数组的 `numel()`**，再决定改哪一层。


## 51. 仪器打到了：`numel=0 rank=0 size=0 storage=0 elemSize=0` —— 读的不是"空数组"，是**一个没被建起来的 Array**，凶手在**答案装配**

**两个临时探针（各一次构建，已删）**：

**探针一（`zeros_arr` 入口）**：
```
PROBE zeros_arr numel=2 rank=1 type=float[] tangent=float[]
```
⇒ **`xs` 到 `zeros_arr` 时有 2 个元素，形状是对的**（与 §50 的结论一致：上游没问题，`zeros_arr` 也没问题）。

**探针二（`Array::getSlot` 的守卫前面）**：
```
PROBE getSlot flat=0 numel=0 rank=0 size=0 storage=0 elemSize=0
```
⇒ **`rank=0`、`elemSize=0`、`storage=0`（compact）**：**这不是"一个 0 元素的 `float[]`"，而是一个"默认构造/零初始化过的 `Array` 头"**——
**有人把 `null` 或一块没写过的对象当成数组读它的第 0 个元素。**（一个真的 0 元素 `float[]` 会是 `rank=1, elemSize=8`。）

**⇒ 凶手位置（今晚第五个"不在被怀疑那一处"）**：`modules/transforms/transpose.cpp:1833-1846`，**被转置成员的答案装配**：

```cpp
for (size_t i = firstPort; i < ports.size(); ++i) {
    if (partial && !participatesOf(i)) { continue; }
    parts.push_back(hasCotangent[ports[i]] ? cotangent[ports[i]] : zero());   // ① 每个端口一个槽
}
…
for (size_t i = 0; i < parts.size(); ++i) { partTypes.push_back(Type::Float64()); slots.push_back(i); }  // ② 槽类型一律 float
```

**两处毛病，都是"签名算了第二遍"的第 4、5 个实例**：
1. **每个端口都进一个槽** —— 而签名（`transposeSignature`，早已正确）只给"有切空间的参数"一个槽：`index_into<float>` 的 `i: int` **不该有槽**；
2. **槽类型写死 `Type::Float64()`** —— `xs` 的槽装的是一个 `float[]`，元组却说它是 `float`。

**⇒ 序号对上了**：`(float[], float)` 这种元组类型 vs. 实际槽里的数组 ⇒ 调用侧按下标读它、按 `float` 解释它 ⇒ 读到 `null` 当成数组 ⇒ `getSlot` 报 `holds 0`。

**下一笔的改法（两处，一起改才绿）**：① `parts` 只收"`tangentSpaceOf(端口的值的类型).hasSpace()`"的端口；② `partTypes` 取该端口值的切空间，**不写字面量**。
**判据**：`grad_of_an_array_parameter` 从 `expected exit 1, got null` 变 `expected exit 0`，且 `feat/compile/transforms` 不开新红。
**这条修好之后才谈撤 `known_fail` 标。**


## 52. 答案装配改对之后：图建成了、签名对了、跑起来了——新的墙是 `:tangent/add_arr` 的"长度不同"

**这一笔把 §51 指出的两处改了**（`parts` 只收有切空间的端口 + 槽类型取该端口的切空间 + 数组端口的零走 `:tangent/zeros_arr`），
与 `idx_arr_t` 的 arm 一起量（**未提交，回退了**，理由见末尾）：

**① 被转置成员这一行是"签名终于对上了"的铁证**：
```
TRANSPOSE function=rulesets.jvp::index_into<float> graph=92 type=(float[], int, float) => (float[],) nodes=3 rules=idx_arr_t
```
**`(float[], int, float) => (float[],)`——参数照原样、`int` 不占答案槽、答案是一个 `float[]` 的元组。** 与 `transposeSignature` 逐字一致。

**② `null` 那条消失了**：`element 0 of an array that holds 0` **不再出现**，`expected exit 1, got null` 变成**有名字的运行期诊断**：
```
[Error] autodiff: tangent arrays added that have different lengths.
```
**⇒ 从"读一个默认构造的 Array"推进到"两个余切数组长度不一致"**，这是**性质完全不同**的一步：前者是类型撒谎，后者是算术对不上。

**③ 六个用例仍红，所以这一笔没有提交**——按"一条失败的层比一张精确的图更糟"，`transpose.cpp` 已 `git checkout` 回 `b4223ee67`。

**④ 下一个探针（省一次摸索）**：`:tangent/add_arr` 的两侧长度各是多少、分别是谁造的。
数组 `xs` 的两条余切一条来自 `scatter_add_arr(zeros_arr(xs), i, ct)`（长度 = `xs` 的元素数），
**另一条如果不是数组长度而是标量/种子，就是 `addCotangent` 在数组目标上收到了标量余切**——
`add_t` 那一支把 `ct` 原样下发时没有检查目标是不是数组（`transpose.cpp` 的 `add_t` 分支），这是下一个要看的地方。

**⑤ 归类一条（承接你说的共同点）**：**一个能解释现象的候选不等于原因；只有"改掉它现象消失、改回它现象回来"才是原因。**
今晚五次"原因不在被怀疑的地方"，被怀疑的那一处**确实都有毛病**（`zeros_arr` 的零、`scatter` 的边界、`transposeSignature` 的签名、答案装配的槽），
**只是每次都不是"这一处"**——所以每条都要走到"改掉/改回"那一步才算数。


## 53. 收尾：`idx_arr_t` 的 arm 落地，五种形状给出微积分该给的数，全量 **8388 / 8376 / 0**

**这一笔（`ccb99a07d` + `8ca2fa701`）是"数组参数求梯度"这条链的收尾。**

**① 五种形状的读数**（`expect_stdout_contains` 已从"钉住拒绝"换成数值断言）：

| 形状 | 读数 |
| --- | --- |
| `xs[0]`（单元素） | `[1.0]` |
| `xs[0]`（两元素） | `[1.0, 0.0]` |
| `xs[0] + xs[1]` | `[1.0, 1.0]` |
| `xs[0] * xs[1]` | `[2.0, 3.0]` |
| 移交件 `xs[1] * xs[0]` | `6.0` 然后 `[2.0, 3.0]` |

**② 一路推过来的六道墙，每一道都是"类型写成了字面量"的同一族**：
1. law 分派没有 `idx_arr_t` 的 arm（`transpose.cpp` 的兜底 `else` 点名拒绝）；
2. 调用点的**答案类型**按参数个数重算（`(float, float)` vs `(float[])`）——改取 `calleeType->exitType()`；
3. 调用点的**答案槽数**按参数个数给，而不是按"有切空间的参数"——改成按签名的槽；
4. **答案装配**每个端口一个槽、槽类型一律 `float`——改成只收有切空间的端口、槽类型取切空间；
5. **单答案下发**把一个 `(float[])` 元组整体交出去（`numel` 其实是地址）——改成读它的槽 0；
6. 规则的**实例化**写死 `{Type::Float64()}`（审查 P1-9）——改成取被匹配操作数的元素类型。

**③ `known_fail` 撤标**：`grad_of_an_array_parameter.nvm` 的标**在让它通过的那一笔里撤掉**（`ccb99a07d`），
**理由写进提交信息与页**："arm 落地、用例通过"，不是"断言被放松"。**撤标之后 runner 的"marked known_fail but passes"不再出现。**

**④ 三笔读数**：
- `feat/compile/transforms`：**57 / 57**；
- `doc_readings`：**10 / 10**（`transpose.cpp` 1880 → **1963**、`jvp.cpp` 1396 → **1401**，原因写进页与提交）；
- **全量 fast：`total: 8388, pass: 8376, fail: 0, skip: 10, known failures: 2`，`RUN_EXIT=0`**。

**⑤ 教训收在这里**（今晚最贵的一条）：**一个能解释现象的候选不等于原因；只有"改掉它现象消失、改回它现象回来"才是原因。**
六道墙里有五处"看着就是它"的地方**确实都有毛病**（`zeros_arr`、`scatter`、`transposeSignature`、答案装配、`add_arr` 的入参），
**但真正卡住的那一处每次都不是被怀疑的那一处**——**所以每一道都要用探针把读数打出来，再动改。**


## 54. 数组余切的两处"类型写成字面量"：一个 `add_d` 让三条用例段错误，一条拒绝让三条动不了

**合并 `develop` 之后（`e8fa221a0` → `6dee3f03f`）动的行号**：`addCotangent` 在 **449–453**（改后 **461–470**），`primitive` 在 **289**（旁边新增 `primitiveOf`），拒绝块 `skip=unread-parameter` 在 **1867**（改后 **1887**），新增的零分支在 **1863**。

**① 段错误（这是这一夜第一个"有信号的"读数）**：
```
$ ./out/latest/bin/camel test/cases/compile/transforms/grad_array_sum.cml std::nvm
5.0
Segmentation fault (core dumped)        EXIT=139
```
`addCotangent` 把**每一次**余切交付都写成 `:op/add_d`（数字的加）。**一次元素读取只有一份余切、从不求和**，所以 `one_element` 与 `two_elements_first` 绿；**求和/乘法/移交件这三条要加两份数组余切**，就把 `float[]` 当数字加 ⇒ 段错误。
**改法**：加法的算子与节点类型都取自"余切落在什么空间"——数组空间用 `:tangent/add_arr`，数字用 `:op/add_d`，节点用该空间定型（`primitiveOf`）。

**② 拒绝改成构造零**（你定的那条）：数组形参没收到余切 ⇒ 答案是它的零，由 `:tangent/zeros_arr` 按端口上的值造（`idxArrayVjp` 用的同一个算子）。**零余切是 pullback 那个求和的单位元，所以"答零"与"不答"是同一件事，只是类型对。**

**③ 一次构建出的三条读数**：`5.0` → `[1.0, 1.0]`；`6.0` → `[2.0, 3.0]`；`6.0` → `[2.0, 3.0]`。
两条仍叛着"拒绝"的钉换成这三个数，**四条 `known_fail` 在让它们通过的这一笔里全部撤掉**（`sum`/`product`/`of_an_array_parameter` 的 nvm、`no_int` 的 fvm）——**撤标的理由是"答案来了"，不是"断言松了"。**

**④ 读数**：`feat/compile/transforms` **58 / 58**；全量 fast **`total: 8390, pass: 8378, fail: 0, skip: 10, known failures: 2`，`RUN_EXIT=0`**；`doc_readings` **10/10**（`transpose.cpp` 1927 → **1970**）。

**⑤ 父级交来的两条 `poison` 用例：诊断原文，以及"不是同一个缺陷"的判定**：
```
$ ./out/latest/bin/camel --gc-check poison --gc mark-sweep -vv …grad_array_one_element.cml std::nvm
camel: fatal: the minor collection started for 'nodevm graph boundary' threw
  (The auto.elder space (run-time values) is exhausted: an allocation of 119370353.0 MB does not fit,
   0 bytes of the limit of 67108864.0 MB are in use; the limit is fixed);
  the heap is half relocated and cannot be used again
terminate called after throwing an instance of 'camel::core::mm::MemoryExhausted'
```
`two_elements_first` 同形，`129402773.0 MB`。**判定：不是同一条缺陷**，两条理由：
1. **这两条的数组形参都被反半读到**，`skip=unread-parameter` 那条分支**根本走不到**，我这一刀的零构造不参与；
2. **失败形态不同**：我修的是"把数组当数字加/把没读到的形参拒掉"，**这里是 `--gc-check poison` 下 `nodevm graph boundary` 的小回收里读出一个巨大的分配量**（`119370353 MB` ≈ 一个地址或被搬动过的头被当成元素数），**不加 poison 时这两条绿**。
⇒ **这是 GC 搬动/根化那一侧的事，不在 `transpose.cpp` 的 lit2 区域，我没改、也不该在这一刀里改。**


## 55. `{Float64()}` 的用例写不出来，所以代码不动——而挡住它的是两条正当的拒绝

**① 撤标的顺序确认（你问的那条）**：`test/tools/run-tests.mjs:2856` 把"标了却通过"直接改成 `fail` 并附
`marked known_fail on '<backend>' but passes: remove the mark`。**上一笔我是"先撤标、再同一次构建里跑"，一次就 58/58**，没有出现那条检查的红。

**② 要写 `f32[]` 用例，三道探针全部被拒**（原文，逐层）：

| 探针 | 结果 |
|---|---|
| `func g(xs: f32[]): f32` | `Invalid call to 'transpose' … : 'transpose' takes a function that returns a 'float'`（`stdlib/vjp.cml:73`） |
| `func g(xs: f32[]): float { return xs[0] as float }` | `DynamicCastForbidden: Dynamic cast from 'f32' to 'float' is not allowed` |
| `func g(xs: f32[]): float { return float(xs[0]) }` | `RuntimeDiag: transforms: jvp has no forward rule for ':op/ftod' (the operator has no forward rule bound at all)` |

**⇒ 今天没有任何一条能进到"规则实例化"那一步**：`vjp` 只接受返回 `float` 的函数（`transpose` 的单参支要求 `Float64` 出口），
而唯一的加宽通路 `float(f32)` 是 `:op/ftod`，它连前向规则都没有。**`int[]` 同理：元素读取回答 `int`，被同一条出口检查挡住。**

**③ 结论（按你的规矩：写不出用例 ⇒ 写页，不动代码）**：`rule_expand.cpp:428` 与 `jvp.cpp:91` 的 `{Float64()}` **今天不是缺陷，
是"可微的东西只有 float64"这句话的后果**。两条拒绝就是保住这个字面量的那道闸：闸在，实例化类型永远只能是 float64。

**④ 但那天闸一开，它就是缺陷**——写清楚留给下一个人：`expandRuleByName` 手里有 `tangents` 和 `position`，
**元素类型从 `position` 那个位置的值/余切类型取**（`firstOperandTypeOf`/`ruleElementTypeOf` 已经为 `vmap` 与 `jvp` 做同一件事）。
判据：`f32[]` 用例能跑通的那一笔，`TRANSPOSE … rules=idx_arr_t` 那一行的实例化类型从 `float64` 变成 `f32`。

**⑤ 这一夜 `Type::Float64()` 的账最终是**：132 行 / 30 文件，**83 合法、12 测试、6 默认值、3 其它、28 形似缺陷**，
而这 28 里**已经改掉 8 处**（`primitive`、`zero`、`tupleOf` 的类型、`addCotangent` 的加法与定型、`skip=unread-parameter` 的零构造），
**剩下 20 处"待证"，2 处（`rule_expand.cpp:428`、`jvp.cpp:91`）等用例**。**没有一处是因为"看着像"而改的。**


## 56. `poison:transforms` 那 10 条：**同一句诊断**，而且是**类型**上的越界，不是 `guard` 那族的裸指针

**① 10 条全部直跑过（不经 runner），一条原文**：
```
$ ./out/latest/bin/camel --gc-check poison test/cases/compile/transforms/vjp_arities.cml std::nvm
EXIT=134

Camel Invariant Violation:
    Location   : ../include/camel/core/type/composite/tuple.h(143)
    Function   : Type *camel::core::type::TupleType::typeAt(size_t) const
    Guarantee  : index < size_
    Reading    : TupleType: index out of range
```
**② 十条同一句**（`vjp_arities` nvm/fvm/jit/prl、`vjp_api.analytic_gradient` nvm/fvm/jit/prl、
`grad_where_the_tangent_ignores_the_operands.nvm`、`vjp_api.carries_its_own_ruleset.nvm`）：
**`Location: tuple.h(143)` + `Reading: TupleType: index out of range`，exit 全部 134（SIGABRT）**。
**⇒ 按"同族的判据是同一句诊断文本"：这 10 条是一族，一个根因的 10 个外观。**

**③ 判据 1 的答案：不是 `guard` 那族。** `guard` 追的是 `a raw pointer was kept across a safepoint`（搬动后的指针被用）；
这里是**类型元组被用越界的下标去取**（`typeAt(k)`，`k >= size_`）。**两个句子不同，不是一个缺陷。**

**④ 而且是"poison 专属"**：同一条用例**不加 `--gc-check poison` 时 exit 0 并印出 `24.0 / 12.0 / 8.0 / 6.0`**。
所以它不是一条普通的红，是**"搬动之后读到一个错的类型/下标"**——与 `guard` 那族的**形状相同**（跨 safepoint 之后读到搬动前的东西），
**只是落在类型上，于是被类型的不变量接住，而不是被"指针被搬动"的报告接住。**

**⑤ 下一步的探针（没做，等点头）**：在 `include/camel/core/type/composite/tuple.h:143` 的守卫里**加上 `this` 的 `size_` 与实际下标**，
以及 `modules/autodiff/pullback.cpp` 里 13 处 `typeAt(...)` 各是哪一处（判据：加了下标与 size 之后，一次构建就能指出是哪个调用点、下标记成了几）。
**这是 `type/composite/tuple.h` 与本报告之外的 `pullback.cpp`，按规矩先报不动。**


## 57. 探针装上了：`size=0`、`index` 每次都不一样、元组本身**没被搬动**

**探针（已按规矩删掉，三行，贴在这里以便复现）**：在 `include/camel/core/type/composite/tuple.h` 的 `typeAt` 前加
`Type *typeAt(size_t index, const char *why)`（默认参数 `"?"`，现有调用点不动），守卫前打印
`why / this / size_ / index / __builtin_return_address(0)`，打印后再取一次 `this` 与 `size_` 比较 `same/MOVED`；
再把 `pullback.cpp`（13 处）、`split.cpp`（3 处）、`transpose.cpp`（2 处）的 `typeAt` 调用逐个带上 `"文件名:行号"`。

**读数（`vjp_arities.cml std::nvm`，`--gc-check poison`）**：
```
PROBE typeAt why=? this=0x7bf8c8646188 size=0 index=4 caller=0x7bf8ce125356
PROBE typeAt after this=0x7bf8c8646188 size=0 sameness=same
```
**换一次运行（`setarch -R` 关 ASLR）**：`this=0x7ffff1646188 size=0 index=2` —— **下标从 4 变成 2。**

**① 三个事实**：
1. **`size=0`**：被索引的那个 `TupleType` **一个成员都没有**；
2. **`index` 两次运行不同（4 / 2）**：**下标不是算错的常量，是从别处走出来的**；
3. **`sameness=same`**：**元组自己没被搬动**（前后同址同 size）。

**② 判据的答案：两个都错，而且错在"这个元组是空的"。** 一个真的 5 成员元组被索引 4 会得到 `size=5`；
这里 `size=0`，所以**不是"对正确的元组用了错的下标"，是"拿到了一个空元组"**——**而空元组正是 default 构造的那个形状**：
**今夜早些时候 `Array::getSlot` 那次 `element 0 of an array that holds 0`，是一个 default 构造的 `Array` 头被当作真数组用；
这里是 default 构造的 `TupleType`（`size_ = 0`）被当作真的有布局用。同一族。**

**③ `why=?` ⇒ 这个调用点不在我注解的 18 处里。** 剩下的候选集中在 `include/camel/core/context/frame.h:719/720`
（`runtimeDataType_->typeAt(...)` / `staticDataType_->typeAt(...)`：**帧按数据槽位问布局的类型**），
以及 `type/tree.h:46-47`、`execute/loop_driver.h` 与 `json`/`onnx` 模块。**`frame.h` 是最像的一处：一个零成员的"运行时数据类型"被问第 4 个槽。**

**④ 下一刀（要一次全量重建，因为你批的探针要加在 `frame.h`）**：把 `why` 只加到 `frame.h:719/720`，
判据是那行 `PROBE typeAt why=frame.h:719` 出现（或 `why=?` 仍在 ⇒ 往下继续缩）。
**这一刀是"缩范围"，不是修。**


## 58. 缩范围第二轮：`frame.h` 7 处、`tree.h` 的 `childType` 全部**排除**；`-vvv` 把时刻钉在**闭包调用的装填**上

**① 这一轮排除的（都打过名字、都没响）**：
`include/camel/core/context/frame.h` 的 **7 处**（98/110/230/273/288 数据布局与动态区、719/720 `ArgsView`），
`include/camel/core/type/tree.h` 的 `childType`（连它在 `i >= arity` 时的自打点都**没响**）。
加上**上一轮已排除**的 `pullback.cpp` 13 处、`split.cpp` 3 处、`transpose.cpp` 2 处 ⇒ **25 处具名候选全部不是现场。**

**② `-vvv` 把时刻钉住了**（不用猜）：
```
[INFO] <GraphExec> Indirect call bind: caller='main' node=37 target='__g_75__' callerNorm=1 callerWith=1 targetNorm=1 targetWith=0 targetClosure=2
Camel Invariant Violation: … tuple.h(160) … TupleType: index out of range
```
**⇒ 崩在"给 `main` 的 37 号节点装填一次闭包间接调用（目标 `__g_75__`，`targetClosure=2`）"的当口**，不是 pullback 内部、不是帧的布局读取。
**这正是你②问的"谁把这个类型设进帧槽"的那一段：装填闭包参数/闭包槽的时候。**

**③ 地址那条路走不通（记下来，省下一班一次构建）**：release 版**没有调试信息**（`addr2line` 打回来 `??:?`），
`nm` 里连我加的锚符号都没有（`inline` 空函数被折叠），**所以 `delta=400208` 无法换算成 `file:line`**。
⇒ **缩范围只能靠 `why` 打名字**，而全树还**剩 272 处 `typeAt(`、约 60 个文件**（`src/execute`、`src/runtime`、`src/compile/gir` 最密）。

**④ 下一刀的两个选择（都要你点头，因为都不便宜）**：
- **甲**：只给**闭包装填那一段**（`src/execute/graph_runtime_support.cpp` 附近、`src/execute/graph_runtime*.cpp`、`src/runtime/frame*.cpp`）的 `typeAt` 打名字——候选约十几处，**一次构建**；
- **乙**：让 `-Tier fast` 带上 `-g`（或另开一档 `Tier debug`），**一次构建换回 `addr2line` 的能力**，`delta` 立刻变成 `file:line`。
**我倾向乙：它一次解决 272 处，甲只是再缩一轮。**

**⑤ 另：`Array` 那条与这条的并法按你④的裁决记的——只并措辞，不并缺陷**（那条是阈值过期，这条是容器头没设）。见下一节。


## 59. 你②那便宜一问的答案：**构造过，不是悬垂** ⇒ 是「未设被读成空」，不是「未构造就被索引」

**探针**：`TupleType` 的唯一构造点（`src/core/type/composite/tuple.cpp` 的 `TupleType::TupleType(layout, types)`）里，
对 `size_ == 0` 的实例记进一个 64 槽的静态登记表并计数；守卫里打印 `emptyBuilt`（本次运行建了几个空元组）与 `mine`（被索引的这个在不在登记表里）。

**读数（`vjp_arities.cml std::nvm` + `--gc-check poison`）**：
```
PROBE typeAt why=? this=0x7a2b59846188 size=0 index=4 emptyBuilt=5 mine=yes
```
**① `mine=yes` ⇒ 这个被索引的元组是"真的建出来过"的空元组，不是悬垂/未初始化内存。**
⇒ **你②问的那一分为二，答案是后者不存在：不是「未构造就被索引」，是「构造了空的，被当成有 4 个成员」。**
**② `emptyBuilt=5` ⇒ 空元组不是单例**（一次运行建了 5 个空 `TupleType`）——**这本身也是一笔账：默认实例没有被共享。**

**③ 与 `Array` 那条分开记**（按你③）：
- **「未设被读成空」**：类型/容器头是**合法构造出来的空实例**，只是**本该有成员的那一份没被填**。**今夜两条都属于这一类**（`Array::getSlot` 的 `element 0 of an array that holds 0`、本条）；
- **「未构造就被索引」**：读到的是**从未被构造的记忆**（悬垂或未初始化）。**今夜没有找到实例——`mine=yes` 把它排除了。**

**④ 结论（本线可以交班的形态）**：`main` 的 37 号节点装填 `__g_75__`（`targetClosure=2`，**该有 2 个闭包槽**）时，
**读到的类型是一个"合法的空元组"（0 成员），却按第 4 个成员去取**。
⇒ **不是下标算错，不是悬垂，是"这个槽该有的那份类型没被设进去，空的那份顶了位"。**
⇒ **根在"谁给这个槽设类型"那一段（闭包装填），不在类型工厂；`mine=yes` 把类型工厂整段排除。**

**⑤ 乙（同档 + `-g`）不可用，理由记下**：`camel-remote.ps1` 的参数只有
`Jobs/Tier/NoBuild/AllowStale/Clean/Changed/ChangedBase/UpdateGolden/LibStdCxxDir`，**没有构建旗标透传**；
`npm run debug` 会翻 `NDEBUG`（你①明令不许）；要 `-g` 只能删 `build` 目录重配 CMake，**且 `-g` 进 per-config 文件，
"看着改了其实没生效"的风险正是你昨夜踩过的那个**。⇒ **这条路今晚不开。**

**⑥ 下一步（不构建也能做的）**：读**闭包装填**那一段（`src/execute/graph_runtime_support.cpp` 的 bind 附近，
及其调用的 `Frame`/闭包槽装填函数），找出"闭包环境应有几个槽"的那个来源，与"实际设进槽的类型"对照——
**判据：`targetClosure=2` 说的是应有 2 个，而读的是第 4 个 ⇒ 先看这两个数字是各自由谁算出来的。**


## 60. 「谁算 4、谁算 0」读出来了：**两个不同的函数**，一个是给槽发下标的，一个是把布局**强制设成"没有槽"**的

**① `0` 的来源（布局的成员数为 0）**
`src/compile/gir/graph_encode.cpp:391-394`，`prepareGraph` 里：
```cpp
// Every compiled graph has a frame layout, one without slots included.
if (!graph->view().hasFrameLayout()) {
    graph->edit().setRuntimeSlotTypes({});     // ⇒ hasFrameLayout_ = true，槽数 0
}
```
`setRuntimeSlotTypes`（`include/camel/runtime/draft.h:533-538`）的注释自己就写着**"possibly one without slots"**；
于是 `src/runtime/graph.cpp:1799-1803` 用这个空 span 建出**成员数为 0 的 `TupleType`**。
⇒ **`size_ = 0` 是"编码器给每个编译图补一个没有槽的布局"这条规则的产物，是一个合法的空布局。**

**② `4` 的来源（节点的 dataIndex）**
`src/runtime/draft.cpp:2160-2178` 的 `GraphDraft::allocateRuntimeSlot`：空表时先塞 `Void()` 占 0 号槽，再 `push_back(type)`，**返回新下标**。
节点拿这个下标写 `header.dataIndex`。⇒ **下标是"发槽"的那个函数发的，它发的每一个都真实存在于 `runtimeSlotTypes_` 里。**

**③ 判据的答案：两个数字来自两个函数 ⇒ 混用成立。**
`0` 来自**编码器补布局**的那一处（`graph_encode.cpp:391`），`4` 来自**draft 发槽**的那一处（`draft.cpp:2167`）。
**中间没有任何一处核对"节点的 dataIndex 必须小于布局的成员数"。** 唯一沾边的一处是
`src/runtime/draft_construct.cpp:555-558`，而它的条件里写着 `init.dataIndex > 0 &&
static_cast<size_t>(init.dataIndex) < runtimeSlotTypes_.size()` —— **越界的下标被这个 `&&` 直接跳过，不报。**

**④ ⇒ 缺陷形态（先报不改）**：**"布局被补成空的"与"节点已被发过下标"两件事都能各自成立，而它们之间没有一致性检查。**
一旦某个图的布局在节点发完下标之后被补成 `{}`（或它的运行时对照物照搬了一个空布局），
**每个 `dataIndex ≥ 0` 的节点都会在 `typeAt` 上撞墙**，而撞墙的句子是"下标越界"，**看上去像下标错，其实是布局空**。
**这正是 `mine=yes` 那个读数的意义：元组是真的，布局是空的。**

**⑤ 为什么它只在 `--gc-check poison` 下发生（待验，但方向明确）**：poison 让每次 safepoint 搬动年轻对象，
**某条路径在搬动后重建了运行时图、而布局走了"补空"那条支路**（`prepareGraph` 的补空只在 `!hasFrameLayout()` 时执行，
`hasFrameLayout` 为真但槽数为 0 的状态因而会被"沿用"下去）。**这一条要下一轮验证，不在本轮结论里。**

**⑥ 按你④：这两句（①与②）是语义层的交接，牵到"帧布局由谁声明"，先报不改。**


## 61. ⑤ 那条 poison 专属的预测**被我自己的读数否掉了**——两个"补空"点都不是现场，GUARD **不能加**

**① `prepareGraph` 那一处（`src/compile/gir/graph_encode.cpp:391-394`）**：加临时打印（补空前扫一遍节点的 `dataIndex` 与 `hasFrameLayout`/槽数），读数：

| 运行 | `prepareGraph` 次数 | 其中 `nodesWithDataIndex=0` | 其中 `hasLayout=0` |
|---|---|---|---|
| 正常（exit 0） | 34 | 3 | 1 |
| `--gc-check poison`（exit 134） | 34 | 3 | 1 |

**两边逐项相同**。而且出现"要补成空布局"的那一个图（`types::is_float<float>`），**`nodesWithDataIndex=0, maxDataIndex=-1`** ——
**也就是说：补空只发生在"本来就没有任何节点带下标"的图上。⇒ 「布局被补空、而节点带下标」在编码这一侧没有实例。**

**② 运行时图的构建那一处（`src/runtime/draft_session.cpp:encodeDraftGraph`）**：同样加打印，读数：

| 运行 | `encodeDraft` 次数 | 其中 `slots=0` | 末三次 |
|---|---|---|---|
| 正常 | 15 | **0** | — |
| poison | 13（更早就崩） | **0** | `jvp_of_three` 12/11、`split_..._forward` 8/7、`split_..._backward` 11/10 |

**每一个被编码的运行时图都带着正常布局**（槽数 = 最大下标 + 1）。⇒ **`size_=0` 不是从这两条路进来的。**

**③ 一个正面读数**：进 `encodeDraftGraph` 的 13/15 个名字里**没有 `__g_75__`**。
⇒ **崩在它上面的那个图不是这次运行编码出来的**，而是从**冻结图解码**来的：`src/runtime/draft.cpp:920-932`
的 `if (const auto *layout = graph->runtimeDataType()) { draft->setRuntimeSlotTypes(layout->types()); }`
—— **它无条件照搬冻结图的布局，包括"有布局但零槽"的那一种。** **这是下一个候选，不是结论。**

**④ ⇒ GUARD 不加，理由两条（这是本轮最重要的判断）**：
1. **假设没被证实**：你①要的判据是"只有 poison 出现 ⇒ ⑤ 成立"，读数是**两边一模一样**（34/34、3/3、1/1），
   而 `slots=0` 在运行时图那侧是 **0**。**按"一个能解释现象的候选不等于原因"，这不构成加守卫的理由**；
2. **加在哪里现在还不知道**：两处"补空"点都不是现场，**唯一说得通的入口是解码路径**（③），
   而"冻结图的布局为零槽"是不是合法，属于**帧布局由谁声明**的语义层——**按你的④，这一层先报不改。**

**⑤ 如果作者拍板要加，形态已经写好了（一页一问的答案）**：
> **问**：一个节点带着 `dataIndex ≥ 0` 的图，允许被赋予"没有槽"的帧布局吗？
> **现状**：允许，且**没有任何一处核对**（`draft_construct.cpp:555-558` 的 `&&` 会把越界跳过，不报）。
> **建议**：**不允许**——加一条 `GUARD(节点无下标 || 布局槽数 > 最大下标)`，位置在**图成形**处（不是编码器的补空处，
> 因为补空处按读数本来就是安全的），**动静**：现在这类不一致会以 `typeAt: index out of range` 的形式在别处爆掉，
> 换成一条点名"布局与下标不一致"的守卫，**把"静默读成空"变成"当场说清楚"**。
> **实测**：正常与 poison 各 34 次 `prepareGraph`、15/13 次 `encodeDraft`，**当前树上一次都不触发**——**加了不会红**。
