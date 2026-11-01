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

**这条现在会直接点名**：`SyntaxDiag::RangeRightIsLink`（`0x02000013`）说"`..` 比 `->` 弱，调用落在最后一个值上，要加括号"。
**优先级本身没改**（改不改是作者的裁决）；**改的只是"被误解时不至于只看到一条签名不匹配"**。

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
