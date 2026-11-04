# `autodiff` 能不能在改写时自己把 `split` 造出来的那个值造出来？

**状态**：待裁；我方默认见第 3 节（**丙：本轮不动**）。

写这页的直接原因：`lane-secondorder` 量二阶导数时，`grad` 撞在 `split`/`jvp`/`transpose` 造出来的**函数值**上。这页把范围量清（4 个形状 + 6 个旁证），指出拒绝那句的出处与它判的**是哪一栏**，并把"要做就得动哪三处、代价是什么"写下来。

## 1. 读数（实测，含树、时刻、负载）

树 `D:\Projects\Camel-wt\secondorder`（分支 `lane-secondorder`），基底 **`34b659107`**（实时 `origin/develop`），工作树干净（`git status --short` 只有不入库的 `probe/`）。宿主 `88zjwei`（32 核），宿主自报 `2026-11-03T15:28:40-05:00` = 北京时间 **2026-11-04 04:28**，**load 42.10 / 27.22 / 22.96**。

「应当的读数」一列是**先手算写下、再跑**的：`sq(x)=x*x`，`sq'=2x`、`sq''=2`；下面每个形状的被微函数都归约成 `2x` 或 `x*x`，所以除 P1 的 `g1` 外都应当读 `2.0`。

| # | 形状（探针） | 应当（手算） | 今天实测 | 判据 |
| --- | --- | --- | --- | --- |
| 1 | `grad(grad(f))`：`autodiff::grad` 套 `autodiff::grad`（`d1(x)=grad(sqw)<x>()`） | `g1=4.0`（`2x`）、`g2=2.0` | **`P1 g1=4.0 g2=2.0`** | **通** |
| 2 | `grad` 作用在 `split(jvp(sq))` 的**正半边** | `4.0`（`x*x` 的导数） | 拒：`the function is the result of 'transforms:split'` | 红 |
| 3 | `grad` 作用在 `vjp(sq)(x).1`（pullback 本身） | `2.0`（`2x` 的导数） | 拒：**同一句**，出处 `stdlib/vjp.cml#vjp` 的 `let (y, lin) = split(jvp(f))<..ws>(..xs)` | 红 |
| 4 | `grad` 作用在 `jvp(sq)` 返回的**线性半边**（`lifted(x,1.0).1`） | `2.0` | 拒：`the function is the result of 'transforms:jvp'` | 红 |
| 5 | **手写**同形状、不经 `split`：`let lin = (dx: float) => 2.0*v*dx`（`v=x`），`lin(1.0)` | `2.0` | **`P5a g=2.0`** | **通** |
| 6 | 手写同形状，函数值由**具名函数调用**返回（`make_forward(x)`） | `2.0` | **`P6 g=2.0`** | **通** |
| 7 | 手写同形状，但 `(y, lin)` 走**元组**再取 `lin`（`let (y, l) = forward(x)`） | `2.0` | 拒：`the function is read from a tuple, a struct or an array`（**另一句**） | 红 |
| 8 | `grad` 作用在 `transpose(line)` 的产物（`line` 手写） | `2.0` | 拒：`the function is the result of 'transforms:transpose'` | 红 |
| 9 | 用户真会写的二阶：`grad` 套**库里的** `grad`（`vjp.cml#grad`） | `2.0` | 拒：同第 3 条那句，`vjp.cml` 同一行 | 红 |
| 10 | `split(手写 lifted)`——操作数是**静态具名函数**，不经 `jvp` | `4.0` | 拒：同第 2 条那句 | 红 |

**第 5、6 条是判据**：同形状、只是由手写的闭包/具名调用造出来的函数值，`grad` 能穿；由 `split` 造出来的不行。所以缺的是**这个值"它调用谁"的信息**，不是"微一个函数值"这件事本身。第 7 条把缺的东西拆成了**两栏**：即便身份有了，把 `lin` 从元组里取出来又是一道墙。

第 1 条与第 2–4 条的差别也是判据：**套两层 `grad` 本身通**——因为 `grad` 是 `autodiff:grad`，声明里 `.macro = true`，反向 pass 有一个旁门（第 2 节 `macroCallee`）认它；而 `vjp = transpose∘split∘jvp` 的三个算子都不是 macro，那个旁门不认，于是二阶停在第 2–4 条。

## 2. 定位：拒的是哪一句、判的是哪一栏

**句子**由 `modules/autodiff/pullback.cpp#Backward::refusalReason` 拼出（head + `which function it calls is not evident where it is called: ` + `modules/autodiff/pullback.cpp#Backward::whyNotEvident`），**推出去的是** `modules/autodiff/pullback.cpp#Backward::visit` 里 `GCNodeKind::Call` 那一支（`refused_.push_back(node)`；`Backward::reportRefusals` 按源码顺序报第一条）。`whyNotEvident` 对 `GCNodeKind::Oper` 的答案就是 `the function is the result of '<uri>'`。

**它判的不是 `calleeCuts`，也不是 `lin_g`。** 那两个是 `split` 自己的记录——`modules/transforms/split.cpp#Half::calleeCuts`（一次 transform 调用里已经切过的图）和 `modules/transforms/split.h#Residual::linearHalf`（某个残差是某个被调函数的 `lin_g`）；`modules/autodiff/**` 里 `transforms:` 这个串 **0 命中**，autodiff 一次都没读过它们。

它判的是**被调值在图里是不是一个静态函数**：`modules/autodiff/pullback.cpp#ForwardRewrite::evidentFunction` 对整个值做 `staticValue(...)` —— 必须是一个 `Data` 常量里的 `::Function*`，或一个 `Fill(FunctionClosure)` 关在常量函数上（那种闭包由 `ForwardRewrite::rewriteIndirectCall` → `rewriteClosureCall` 转成直呼）。唯一的旁门是 `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee`，它按 `OperId` 只认 `autodiff:grad`/`with_grad`/`grad_aux`/`with_grad_aux` 四个。都解析不出来时 `rewriteIndirectCall` 返回 `nullopt`（那里注释写着 `a function computed at run time`），调用节点被留着，反向走到 `visit` 的 `Call` 支就拒。

**`split` 造的值里本来有没有这个信息？有。** `modules/transforms/split_operator.cpp#splitKernel` 用 `::Function::create(built.program, built.forward->id(), function->tupleType(), …)` 交出来的就是一个**带真实 graph** 的函数值，闭包里装残差。缺的不是值上的信息，是**编译器读的那张图上**的信息：那个 graph 是 kernel 运行到那一句时由 `buildSplitGraph` 现造的，调用点节点上只有一个 `Oper(transforms:split)`。所以"能不能带上"这件事的答案是：**值上带得动，图上带不动**——除非 pass 自己在改写时把同一张图造一遍。

## 3. 要做就得动哪三处，代价是什么

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：把三个 builder 交给 `macroCallee`** | `ForwardRewrite::macroCallee` 再认 `transforms:jvp` / `transforms:split` / `transforms:transpose` 三个 uri，各调一次 `modules/transforms/jvp.h#buildJvpGraph`、`modules/transforms/split.h#buildSplitGraph`、`modules/transforms/transpose.h#buildTransposeGraph`，把结果 `::Function` 按 `grad` 那条路的办法 root 住并交回改写 | **三处**：①`modules/autodiff/pullback.cpp`（认 uri、造图、`roots_` 生命周期、闭包搬 `f` 的 `with` 元组）；②`modules/autodiff/CMakeLists.txt` 加 `EXTRA_LIBS transforms_cmo`（今天**没有任何模块依赖 `transforms`**，`random`/`tiles` 依赖 `tensor_cmo` 是唯一先例，见 `modules/camel_module.cmake`）；③**依赖 `jvp.cpp` 与 `transpose.cpp` 的现有签名**——这两个文件正被另两条车道改写，本车道任务书把它们列在"不要碰"。另外 `buildSplitGraph`/`buildTransposeGraph` 只出图，`::Function::create` 那十行在各 `*_operator.cpp` 里（不在本车道可改范围），要抄一份到 `pullback.cpp`。能移动第 2/3/4/7/8/9/10 条 |
| **乙：改写时跑一遍算子，取返回值里的 graph** | 不跨模块：pass 对一个 `readsGraphs` 算子、操作数全静态的节点调它的 kernel 一遍，拿返回值里的 `::Function*`（自下而上：先把 `jvp(f)` 算出来，再算 `split`） | 要先裁**"pass 能不能在编译期执行一个变换算子"**：`transforms:split` 声明的是 `.pure = false`（`modules/transforms/split_operator.cpp#registerSplitOperatorMetadata`），编译期跑它等于在 pass 里跑用户可见的副作用（readings 会打 `REINTERP split function=…`）。入口在 `src/` 里今天没有，本车道不许动 `src/**` |
| **丙（默认）：本轮不动** | 只把这页与读数留下 | 4 个形状里 3 个要 `jvp`/`transpose` 的 builder，**只认 `split` 一个也一个都移动不了**——第 2 条的操作数是 `jvp(f)` 的产物，而那个产物自己就拒（第 4 条已量到）；只有第 10 条那种"操作数是静态具名函数"的形状会动。这就是丙的代价：**它不动任何一条已量的红，但它是唯一不跟两条在改的车道撞车的选择** |

## 4. 我方默认：丙，等两条车道落地再接甲

理由，按读数排：

1. 第 2、3、4、8、9 条（含**用户真会写的那一条**，第 9 条）全都要 `jvp`／`split`／`transpose` 的 builder（第 4 条只要 `jvp`，第 2 条要前两个，第 3、8、9 条三个都要），而 `modules/transforms/jvp.cpp` 与 `modules/transforms/transpose.cpp` **正在被另两条车道改**。接甲的接口正落在那两个文件的签名上，落地即撞。
2. 只做 `split` 一处的折中方案，按第 1 节**一个形状都不动**（第 2 条的操作数是 `jvp` 的产物）。不要按"先做一半"去动 `split.cpp`。
3. 甲落地时还要定"读数打不打"：`buildSplitGraph` 会往 readings 打 `REINTERP split function=…`。pass 里再造一张图，打了就比今天多一行、不打就与原来那一行不一致；而 `test/plans/feat/transforms/compositions.plan.toml` 的 `verify_script` 把 stdout **每一行**都当数字读（`map(s => Number(s.trim()))`），多一行就是红。这一处得和作者一起定。

**这一轮不改任何代码**：`modules/autodiff/**`、`modules/transforms/**`、`stdlib/vjp.cml`、`test/` 全部原样；拒绝句、读数、与那 8 条 known 都不动。门在不在，看 `feat/transforms/compositions` 的 `[SUMMARY]`：同一棵树上改前改后都是 `total: 10, pass: 2, fail: 0, known failures: 8`（`-Tier fast -Jobs 4`）。

## 要作者答的一句

**「`grad` 的图读不到"`split` 造出来的值调用谁"（值上有、图上是 `Oper`）；要让 `grad` 穿 `split`/`jvp`/`transpose` 造的函数值，我默认『丙：本轮不动，等 `jvp.cpp`/`transpose.cpp` 两条车道落地后按甲接——pass 在改写时调 `buildJvpGraph`/`buildSplitGraph`/`buildTransposeGraph` 各造一遍图，代价是 `autodiff` 第一次依赖 `transforms` 的 `.cmo`』。若宁可先定方案再动手，另一条是乙：让 pass 在编译期执行一个 `readsGraphs` 算子、取它返回的函数值里的 graph——那要先裁『pass 能不能在编译期跑一个 `.pure = false` 的算子』。」**
