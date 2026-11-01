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
