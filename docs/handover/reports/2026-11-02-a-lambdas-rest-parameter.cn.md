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
