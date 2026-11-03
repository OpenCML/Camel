# 二阶自动微分卡在哪一层：一个"图叫得出名字"的函数值？

**状态**：待裁；我方默认见第 3 节（**甲：不松 `evidentFunction`，把二阶的可用写法钉成用例**）。
**判定：这道门今天必要**（第 2 节给出它保护什么、以及为什么放不开）。

## 1. 读数（实测）

树 `D:\Projects\Camel-wt\second` = `origin/develop` `34b659107b71`，宿主 88zjwei（32 核），
2026-11-03 17:48–17:52 EST（= 本地 11-04 06:48–06:52），1 m load 13.2–14.9（读数全是**数值**，
不是计时，负载不进入结论）。二进制就是闸门这次构建的那一份（`out/latest`，build exit 0）。
`f(x) = x³`，`f'(x) = 3x²`，`f''(x) = 6x`，**`x = 2` 手算 12**。

| 写法 | 退出码 | 读数 |
| --- | --- | --- |
| `grad(grad(cube))<2.0>()` | **0** | `nested=12.0` |
| `grad(<x: float> () => grad(cube)<x>())<2.0>()` | **0** | `lambda=12.0` |
| `grad(dcube)<2.0>()`（`dcube` 是具名函数，体内 `grad(cube)<x>()`） | **0** | `12.0` |
| `let g = dcube; grad(<x: float> () => g<x>())`（内层变换**当成值**捕获） | 1 | 见下 R1 |
| `vjp(vjp(cube))` | 1 | 见下 R2（类型层，先于那道门） |
| `grad(vjp_value)`，`vjp_value` 体内 `vjp(cube)(x).0` | 1 | 见下 R3 |
| `grad(slope)`，`slope` 体内 `vjp(cube)(x).1(1.0)` | 1 | R3 + 见下 R4 |

**R1**（捕获一个函数值）：
`autodiff: cannot differentiate through this call of a function value: which function it calls is
not evident where it is called: the function is a parameter, or a captured value, of the function
being differentiated.`——位置在**我自己的**lambda 里 `g<x>()`。
注意 `g` 是**具名的静态函数**：值在程序里是有的，图里那个调用点上没有。

**R2**：`Invalid call to 'jvp' ... this one's result is a '(float, (ct: float) => (float,))', which has
none: the lifted signature answers the pair (value, tangent)`——`vjp` 的结果带一个回传，回传没有
切空间，所以 `vjp` **套不起来**；要二阶必须把结果压成标量（下表两行）。

**R3**（`vjp` 的正向半边）：`cannot differentiate through this call of a function value: which
function it calls is not evident where it is called: the function is the result of
'transforms:split'`，位置在 **`stdlib/vjp.cml#vjp` 的 `split(jvp(f))<..ws>(..xs)`**，并附
`note: the function value is chosen here` 指向同一个 `split`。加 `std::macro` 只是在外面多包一层
`RuntimeDiag::MacroEvaluationFailed`，里层文字不变。

**R4**（`vjp` 的反向半边，摘出回传再调用）：`... the function is read from a tuple, a struct or an
array`。

## 2. 那道门

判据原文（`modules/autodiff/pullback.cpp#ForwardRewrite::evidentFunction`，注释与代码是同一句话）：

> The function a function value is, where the graph itself says so: a constant function, or a
> closure built in this graph (FILL) over a constant function. Null for anything else (a port, the
> result of a branch or of a call, an element of a container).

**为真**：`Data` 节点上带静态 `::Function*`（且 `function->graph() != nullptr`），或
`Fill(FunctionClosure)` 且**恰好一个**输入、那个输入是这种常量（即无捕获的闭包）。
**为假**：`Port`（参数 / 捕获）、`Join`（运行期选的臂）、`Accs`（从元组/结构体/数组读出的）、
`Oper`（任何算子结果，今天就是 `transforms:split`）、`Func`/`Call`（调用结果）。

绕过路径 `#ForwardRewrite::macroCallee`：**只认本模块自己的宏算子**——按 `OperId` 匹配
`autodiff:grad`、`autodiff:with_grad`、`autodiff:grad_aux`、`autodiff:with_grad_aux`，要求那个
算子节点的输入是静态函数值，然后**当场**调 `buildGradientGraph` 建出梯度图并把它 root 住，于是
外层的重写把这次调用当成普通直接调用处理。这就是 `grad` 套 `grad` 能答 12 的全部原因
（提交 `47945c596` 的原话：*higher-order gradients built inside a differentiated function are
rewritten like any other direct call*）。**`vjp` 不在这个表里，因为它已经不是本模块的算子了**：
`vjp` 是 `stdlib/vjp.cml` 的函数，由 `jvp`、`split`、`transpose` 拼出来。

**它在保护什么**（三个出处，说的是同一件事）：

1. `docs/technical/26_autodiff_as_graph_rewrite.md` §4.4「Following」：微分之前要先把"带函数值的
   调用"**内联**进被微分的图，"*Evident* means that the graph itself says which function the value
   is"——内联用的是 `std::opt::inline` 的 `inlineCallInOrder`，它需要**被调者的图**。
2. 同一份文档 §4.4 的开头与 `docs/design/program-transforms/README.cn.md`：求导是**图重写**，不是
   解释执行；重写发生在程序跑起来之前。
3. 代码里最硬的一条：`#Engine::planForClosure` **按图**规划，一份导数图被这个函数的所有调用点共用
   （*one derivative per function, shared by its call sites*），并且把闭包的**捕获值追加成参数**。
   所以捕获里的函数值到了导数图里是**参数**，不是节点。

`docs/design-principles.cn.md` 没有直接写这一条；能引用的是 9.9 的第一条（自动微分是库，作用于交给
它的图，处理不了的结构**在那个位置带诊断拒绝**）和第 4 节"函数值没有身份（只由它算什么决定）"——
后者说明"按结构认函数"在原则上是正当的，但今天没有任何机制把端口还原成它被填进去的那个值。

## 3. 必要 / 历史：**必要**，而且我量出了它必要的那一层

**R1 是关键读数**：`g` 是具名静态函数，程序里明明有那个 `::Function*`，照样被拒。所以这不是
"编译器没拿到静态值"，而是**图里那个调用点上没有它**：它只出现在上一层（`Fill` 的输入里）。而
导数图是**按图规划一次、所有调用点共用**的（第 2 节第 3 条），同一个闭包在不同调用点捕获的函数
可以不同——除非导数自己接受一个**函数类型的参数**，否则没有任何一张图能同时满足它们。

能破这一层的设计写在 `docs/technical/26_autodiff_as_graph_rewrite.md` §4.4 里，而且**同一节自己
承认它没做**：把流进被微分代码的函数值**按导数类型包一层**，调用点传 `D[f_value]`（*The wrapping
of function-typed parameters described above is not implemented.*）。那是**调用约定**的改动，不是
一处谓词能放的。

**R3 是另一层，同样必要**：`split` 的正向半边不是图，是 `splitKernel` **运行时**读
`function->graph()` 现建的（`modules/transforms/split_operator.cpp#splitKernel`），回传更是关在正向
半边里面、调用后现造的闭包。重写要在程序跑之前就为这次调用规划导数，而"被调者的图"此刻还不存在。
要把这条路打开，得让 `jvp`/`split`/`transpose` 在**重写期**可求值——即把 `macroCallee` 那招
（对 `autodiff` 自己的宏做的那种"当场建图"）推广到 `transforms` 的算子上，这是**跨模块的语义调用**，
不是一行开关。

## 4. 两条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：不松门，钉写法** | `evidentFunction` 不动；把能答数的三种写法钉成用例（`second_order_nest.cml`：直嵌套 12、lambda 体内 12），把两条拒绝也钉住（捕获值 / `transforms:split`），并把 `macroCallee` 注释里那半句**已经不存在的 `vjp`** 改掉 | **零风险**：不动语义、不动谓词。代价是二阶**只走 `grad` 这一条**，`vjp` 那条仍是拒绝；拒绝的位置在 `stdlib/vjp.cml` 里，用户看到的不是自己的行 |
| **乙：放开门** | 让 `evidentFunction` 认 `Oper`/`Accs` 上的函数值，重写期就地求值 `transforms` 的算子 | **要跨模块依赖**（`modules/autodiff` 调 `modules/transforms`），把"库怎么拼"从 Camel 搬回 C++；而且 `Fill` 带捕获那一支（`evidentFunction` 现在要求恰好一个输入）还不认，`lin` 正是带捕获的闭包——**一处放开不够，要两处**，且没有任何读数能证明放开后数值对 |

## 5. 我方默认（已按此开工）

**甲。** 理由：这条门今天保护的是"导数图在重写期就要存在"这条结构（第 2 节第 3 条），而 R1 证明
卡住的是**图里没有那个值**、不是**编译器没拿到静态值**——乙要改的是调用约定和跨模块分层，不是谓词。
二阶今天**能答 12**（`grad` 那条，两条写法都实测 12.0），缺的是 `vjp` 那条，而打开它需要作者裁
"函数类型的参数要不要有导数"（设计 4.4 那一段）。

要作者答的一句：

**「二阶今天只能走 `grad`：直嵌套 `grad(grad(f))` 与 lambda 体内写 `grad` 都答 12，但内层变换一旦
被当成值捕获、或经 `vjp`（`split` 的正向半边）到达调用点，就按 `evidentFunction` 拒绝——实测捕获的
是一个具名静态函数也拒绝，因为导数图按图规划一次、捕获成了它的参数。要不要把设计 4.4 那一段
（函数类型的值按导数类型包装、调用点传 `D[f_value]`）定为下一层？我默认『先不做，甲：钉住能答的
写法 + 钉住两条拒绝，`patch` 落在用例与注释』。」**