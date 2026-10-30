> 已由作者答复，见 `docs/handover/reviews/questions-2026-10-29.md` 的裁定节；本页保留为记录。

# 对齐 JAX 的程序变换：差距清单、分批与验收

决策者清单第 14 条要的是**计划**：`vmap`、`jvp`、`checkpoint` 这类变换"按同一套算子写，先交计划"。本页给三件事：逐项的差距清单、哪一半是纯命名/纯签名、对齐之后用什么用例证明——并把要动语义的部分收成问题，交作者。

施工分支 `lane-jax`。读法：**以本分支 `cd4bbb259` 上现读的代码为准**，每一条"我们现在的行为"都带文件与行；不引旧清点稿。JAX 一侧按 JAX 的公开行为写（`jax.grad`、`jax.value_and_grad`、`jax.jvp`、`jax.vjp`、`jax.vmap`、`jax.checkpoint`、`jax.custom_jvp`、`jax.custom_vjp`）。

## 0. 本条要什么

两处原文合起来是范围：

- 决策者（`docs/handover/reviews/questions-2026-10-29.md:33`）：第 8 路的三个算子落地后，`vmap`、`jvp`、`checkpoint` 按同一套算子写，先交计划。
- 交接说明 5.26（`docs/handover/README.cn.md:505-513`）：`grad`/`vjp`（已有）、`jvp`（前向）、`custom_vjp`/`custom_jvp`、`vmap`（按维度名对齐）、`jit`（已有整图编译）、`scan`/`while_loop`/`cond`（已有 `LOOP`/`BRCH`）、`checkpoint`（重算换内存）、`pmap`/分片（接 GPU 计划）、组合 `grad(vmap(f))`/`vmap(grad(f))` 以 JAX 同名例子对照。

依赖现状，现读：`reinterp`、`split`、`transpose` 三个算子在 `src/`、`modules/`、`stdlib/` 里 **0 行**；库里只有它们的**规则面**——三个标签加标量一族 10 条规则（`stdlib/autodiff.cml:44-187`），标签按结构读回（`stdlib/graph.cml:170-189`、`modules/graph/read.cpp:560-628`，用例 `test/cases/modules/autodiff/transform_rules.cml`）。第 6 条那一路（`lane-transforms`）正在做第 1 步。所以本批**不写三个算子**，只写名字面与验收。

## 1. 差距清单

一览（"类"就是第 3 节的分组）：

| # | JAX | Camel 今天 | 类 |
|---|---|---|---|
| J1 | `grad(fun, argnums=0, has_aux=False, …)` | `grad(f)`，对第一个 `with` 参数 | 语义 |
| J2 | `value_and_grad(fun, argnums=0, …)` | `with_grad(f)` | 命名（+J1 的语义） |
| J3 | `jvp(fun, primals, tangents)` | 只有标签 `#jvp(rule)`，没有变换 | 语义 + 命名冲突 |
| J4 | `vjp(fun, *primals)` | 只有标签 `#vjp(rule)`（今天当 `custom_vjp` 用） | 语义 + 命名冲突 |
| J5 | `custom_jvp` / `custom_vjp` | `#jvp(rule)` / `#vjp(rule)` | 命名 + 规则形状（语义） |
| J6 | `linear_transpose(fun, *primals)` | 标签 `#transpose(rule)`，算子 0 行 | 语义 + 命名（与数据转置重名） |
| J7 | `vmap(fun, in_axes=0, out_axes=0)` | 没有；最近的是 `map`（秩 1、批一个数组） | 语义 |
| J8 | `checkpoint(fun)`（=`remat`） | 没有；"checkpoint" 一词已被持久化占用 | 语义 + 命名 |
| J9 | `jit(fun, static_argnums=…)` | `std::jit`（整图编译的一个遍） | 无（5.26 记"已有"） |
| J10 | `lax.scan` / `while_loop` / `cond` | `LOOP` / `BRCH`（语言层） | 无（5.26 记"已有"） |
| J11 | `pmap` / `shard_map` | 没有；多设备只有 `--devices` 选卡 | 语义（等 GPU G4–G5） |
| J12 | `checkify` | `attempt(f)` | 命名（有意保留） |
| J13 | `lax.stop_gradient` | `stop_gradient` | 无（同名同义） |
| J14 | 组合 `grad(vmap(f))`、`vmap(grad(f))` | 写不出来（`vmap` 还不是值） | 语义 |

读法（哪一栏是有意省略的）：逐项下文的五栏是**名称 / 语义 / 我们现在 / JAX 的行为 / 差在哪**，但十四项不是每项都写满五栏。**J1–J8 五栏齐全**。**J9–J13 是"已有、本页不动"的项**，只记判断它该不该动的那几栏：J10–J13 记**名称 + 差在哪**；J9 另记**我们现在**与**JAX 的行为**，因为它的差别在粒度（整图 vs 函数）与选择方式（遍 vs 参数），不写这两栏说不清。**J14 是组合项**，记**语义 + 我们现在 + 差在哪**：它没有自己的"名称"，也没有独立于 J3/J4/J7 的"JAX 的行为"可单列。少写的栏是**有意省略**，不是漏写——补上只是把"已有、不动"这句结论在同一条里再抄一遍；哪一条将来要动，先补齐它的栏。

逐项。

### J1 `grad`

- **名称**：`jax.grad` ⇄ `grad`，同名。
- **语义**：反向模式，把函数变成它的梯度函数。
- **我们现在**：`grad(f)` 对 `f` 的**第一个 `with` 参数**求导；普通参数与其余 `with` 参数是常量；结果是标量浮点，否则 `NotDifferentiable`；整数/布尔叶子给同型的零（与 JAX 相同）。声明在 `stdlib/signatures/autodiff.cml:29-33`，类型解析与拒绝在 `modules/autodiff/operators.cpp:37-81`，运行期建梯度图在 `operators.cpp:85-107`。
- **JAX 的行为**：默认对第 0 个**位置参数**求导；`argnums` 可给元组（多参数、多返回）；`has_aux=True` 时把 `(y, aux)` 拆开，只对 `y` 求导；`holomorphic`/`allow_int`/`reduce_axes` 各有别的用途。
- **差在哪**：求导对象怎么选（第一个 `with` 参数 vs 第 0 个位置参数）；`argnums` 的多参数形式；`has_aux` 没有。

### J2 `value_and_grad`

- **名称**：`jax.value_and_grad` ⇄ `with_grad`，名字不同。
- **语义**：一次求值同时给出 `(值, 梯度)`。
- **我们现在**：`with_grad(f)` 返回 `(f 的值, 梯度)`，顺序与 JAX 一致（`stdlib/signatures/autodiff.cml:35-39`、`modules/autodiff/operators.cpp:114-117`；用例读 `vg=(17.5, 12.5)`，`test/plans/feat/modules/autodiff.plan.toml:14-19`）。
- **JAX 的行为**：`value_and_grad(fun, argnums=0, has_aux=False)` 返回 `(value, grad)`。
- **差在哪**：只剩名字，以及 J1 的 `argnums`/`has_aux`。

### J3 `jvp`

- **名称**：`jax.jvp` ⇄ **没有这个变换**；Camel 的 `jvp` 是标签名 `#jvp(rule)`（`stdlib/autodiff.cml:41-44`）。
- **语义**：前向模式：给定 `(x, dx)` 给 `(f(x), J·dx)`，一次一个方向。
- **我们现在**：标签只表示"某个函数自己的前向规则"；标量算术 5 条规则（`add_jvp`…`neg_jvp`，`stdlib/autodiff.cml:85-132`）；按结构读回（`graph::rule_of(g, 'jvp')`，`stdlib/graph.cml:178-189`，内核 `modules/graph/read.cpp:602-628`）。**变换本体不存在**：`jvp(f)` 没有任何东西接。
- **JAX 的行为**：`jax.jvp(fun, primals, tangents)` 返回 `(primals_out, tangents_out)`；与 `vmap` 组合得到整条雅可比，成本是每次一个方向。
- **差在哪**：没有变换（需要 `reinterp<jvp>` 或设计稿里等价的组合，`docs/design/program-transforms/README.cn.md:71-87`）。另一件：**`jvp` 这个名字已经是标签名**，而标签与函数、类型同一命名空间（`diagnostics/registry.tsv` 的 `TagUndeclared` 帮助："Tags share the namespace of functions and types"），所以 `jvp` 作函数名之前先要定命名空间怎么分。

### J4 `vjp`

- **名称**：`jax.vjp` ⇄ **没有这个变换**；Camel 的 `vjp` 是标签名 `#vjp(rule)`（`stdlib/autodiff.cml:52-55`），今天扮演的是 JAX 的 **`custom_vjp`**：调用那个函数时用规则代替对函数体求导（`modules/autodiff/module.cpp:22-28` 的 `kVjpTag` 与 `kGraphRefRule`；用例 `custom_rule`）。
- **语义**：反向模式，给任意形状的结果（不要求标量）：返回 `(结果, 回拉函数)`。
- **我们现在**：标签可读（`rule_of`）、被 `pullback.cpp` 在调用点使用；变换本体 0 行。
- **JAX 的行为**：`jax.vjp(fun, *primals)` 返回 `(primals_out, vjp_fun)`；`vjp_fun(ct)` 给出每个 primal 的余切；`grad` 是 `vjp` 加一个 `ct = 1`。
- **差在哪**：变换不存在；名字同样冲突（函数 `vjp` vs 标签 `#vjp`）。`docs/handover/stdlib/autodiff.signatures.cml:18-23` 已记过一句："若编译器分不开，改标签名，不改变换名"。

### J5 `custom_jvp` / `custom_vjp`

- **名称**：`jax.custom_jvp`/`jax.custom_vjp` ⇄ `#jvp(rule)`/`#vjp(rule)`，名字差 `custom_` 前缀。
- **语义**：用户给某个函数指定导数，调用处不再看函数体。
- **我们现在**：`#vjp(rule)` 已接到底（`custom_rule` 用例在 nvm/fvm/jit 上三条路径都读它）；`#jvp(rule)` 只被读成规则图，还没有遍拿它当过自定义导数。
- **JAX 的行为**：装饰器；`custom_jvp` 的规则签名是 `(primals, tangents) -> (primals_out, tangents_out)`，`custom_vjp` 的规则给 `(primals_out, pullback)`。
- **差在哪**：**规则的形状**——我们的规则右边只给切向量（原值由函数体自己算，设计稿第 3 节），JAX 的规则原值与切向量一起进出；名字另差前缀。

### J6 线性转置

- **名称**：`jax.linear_transpose` ⇄ `transpose`（设计稿的第三个算子，`docs/design/program-transforms/README.cn.md:101-115`）加标签 `#transpose(rule)`（`stdlib/autodiff.cml:46-50`）。
- **语义**：对线性函数，把数据流反过来：余切进、余切出。残差是已知值。
- **我们现在**：标签加 5 条标量规则（`add_t`…`neg_t`）、`rule_of(g, 'transpose')` 可读；**算子 0 行**。
- **JAX 的行为**：`jax.linear_transpose(fun, *primals)` 返回 `ct -> cts`；不是线性的函数交进去是错误。
- **差在哪**：算子不存在。名字另有一层：Camel 已有数据转置 `tensor::transpose(t: float[..]): float[..]`（`stdlib/signatures/tensor.cml:126-129`），与函数同一命名空间；JAX 用 `linear_` 前缀正是为了分开这两件事。

### J7 `vmap`

- **名称**：`jax.vmap` ⇄ **没有**。最近的是 `map`：`map::<T, U>(xs: T[]): U[]`，`with <f: (item: T) => U>`（`stdlib/prelude.cml:483-487`）。
- **语义**：把作用于一个值的函数变长成作用于一批的函数。
- **我们现在**：`xs->map<f>` 一次批**一个**数组，批的是它**唯一那一维**（`T[]` 是秩 1；`stdlib/prelude.cml:479` 的注释写明秩 n 的含义是 P5 的活）；没有 `in_axes`/`out_axes`、没有按维度名对齐；它不是变换（不能写 `vmap(grad(f))`，因为没有 `vmap` 这个值）。
- **JAX 的行为**：`vmap(fun, in_axes=0, out_axes=0)`；可同时批多个参数（每个参数一个 `in_axes`）、可批 pytree 的每个叶子、结果是函数、任意嵌套、与 `jit`/`grad` 可交换。
- **差在哪**：四层——批几个参数（一个 vs 多个）、轴（写死第一维 vs 指定/命名）、批什么（数组的元素 vs 树的叶子）、是不是变换（值 vs 函数）。

### J8 `checkpoint`

- **名称**：`jax.checkpoint`（JAX 自己的别名是 `jax.remat`）⇄ **没有**。Camel 里"checkpoint"是**持久化**：`ckpt` 模块的 `save_tree`/`load_tree`（`stdlib/ckpt.cml:48`、`:60`），用例 `test/plans/feat/modules/checkpoints.plan.toml`、`ckpt.plan.toml`，实现 `modules/ckpt`。
- **语义**：值上是恒等；反向时**重算**被标注的那段而不是存中间量，用计算换内存。
- **我们现在**：0 行（`remat`/重算在 `src`、`modules`、`stdlib` 里无命中）。
- **JAX 的行为**：`checkpoint(fun)` 返回 `fun` 本身（值不变），反向时重算；可带 policy；与 `grad`、`vmap` 组合。
- **差在哪**：变换不存在；名字还与持久化撞车。JAX 的别名 `remat` 在 Camel 里是空的，可以直接用。

### J9 `jit`

- **名称**：`jax.jit` ⇄ `std::jit`（一个遍，不是函数变换）。
- **我们现在**：`camel prog.cml std::jit` 整图编译后执行；另有 `std::fvm`（字节码）与 `std::nvm`（树的解释器）。
- **JAX 的行为**：按函数编译，`static_argnums` 选择哪些参数进常量；同一个函数可被多处 `jit`。
- **差在哪**：粒度（整图 vs 函数）与选择方式（遍 vs 参数）。5.26 记"已有"，本项不动。

### J10 `scan` / `while_loop` / `cond`

- **名称**：⇄ `LOOP`、`BRCH` 两个图节点与 `for`/`if`；流上的 `unfold`（`stdlib/stream.cml`）。
- **差在哪**：没有同名的库函数；结构在语言层。5.26 记"已有"，本项不动。

### J11 `pmap` / `shard_map`

- **名称**：⇄ 没有。多设备只有选卡：`--devices`（`tools/camel-cli/config.cpp:327`、`:658`）。
- **差在哪**：把一个函数按设备铺开、以及分片（`shard_map`）都不存在；GPU 那一路（清单第 12 条）在做 G1–G3。本项等 GPU G4–G5，不在本页排期。

### J12 `checkify`

- **名称**：`jax.checkify` ⇄ `attempt`（`include/camel/execute/fault_isolation.h:7-36`，`kFaultIsolationName`）：`attempt(f)` 的参数与 `f` 一样，结果类型是 `f` 的结果加 `!`。
- **差在哪**：名字不同，想法相同（设计原则 2 节已定名 `attempt`）。有意保留，不改。

### J13 `stop_gradient`

- **名称**：`jax.lax.stop_gradient` ⇄ `stop_gradient`（`stdlib/autodiff.cml:57-61`；内核与 `noGradient` 在 `modules/autodiff/operators.cpp:119-123`、`:171-173`）。
- **差在哪**：无。同名同义。

### J14 组合

- **语义**：JAX 里变换都是"函数到函数"，所以任意嵌套都成立；5.26 的验收就是 `grad(vmap(f))`、`vmap(grad(f))` 对 JAX 同名例子。
- **我们现在**：写不出来——`vmap` 不是值。`grad`/`with_grad` 之间的组合是值级的（`grad` 套 `grad` 可以），但 `map` 里传一个带 `with` 参数的变换函数（`xs->map<grad(f)>` 一类）本批**没有验证过**，不下断言。
- **差在哪**：缺 J3、J4、J7 三个变换；组合的验收等它们落地（§4.3）。

## 2. 三个算子的位置（本批不写，写在这里免得计划悬空）

设计稿把 JAX 的 `jvp` 拆成"规则 + 通用算子"：前向是 `reinterp<jvp>`，线性化是 `split(reinterp<jvp>(f))`，反向是再转置（`docs/design/program-transforms/README.cn.md:4.1-4.4`）。对应到本页：

- J3 `jvp` = `reinterp<jvp>`（第 6 条第 3 步）；
- J6 线性转置 = `transpose`（第 5 步）；
- J4 `vjp` = 库里 `split` + `transpose` 的组合（第 4、5 步）；
- J7 `vmap` 是**另一个规则集**上的 `reinterp`（`Lift<T> = T[?]`、逐元素规则），不是新算子——这是本页对第 14 条的核心判断：`vmap` 不需要新机制，只需要一组合规则和一个按维度名对齐的轴参数；
- J8 `checkpoint` 是**图变换**（把一段子图在反向里重挂一份），与三个算子并列，但可以先于它们独立做（不改值语义）。

## 3. 分类

### 3.1 纯命名/纯签名（可小步落地）

| 项 | 落地形态 | 本批 |
|---|---|---|
| `map` 的批处理合同 | 一次一个数组、只有那一维（J7 的签名边界）写进库的说明，不写 JAX | **落**（`stdlib/prelude.cml` + 生成页） |
| `with_grad` ⇄ `value_and_grad` | 改名 | 不落：设计原则 9.9 与库说明都以 `with_grad` 为规范名，改名要连原则、教程、用例一起改；`docs/handover/stdlib/autodiff.notes.md:10` 记过一次"名字保留"。要改先问作者（Q6） |
| `#jvp`/`#vjp` ⇄ `#custom_jvp`/`#custom_vjp` | 标签改名 | 不落：三个标签刚由 `lane-transforms` 落地并在用，改名要那一路一起动；且这是命名空间问题的解法之一（Q2） |
| `transpose` ⇄ `linear_transpose` | 算子改名 | 不落：算子还不存在（Q3） |
| `checkpoint` ⇄ `remat` | 名字选择 | 不落：变换还不存在；`remat` 空着（Q4） |
| `attempt` ⇄ `checkify` | — | 不改：有意保留 |

一句结论：**第 14 条今天能"只动名字"的地方只有 `map` 的签名边界一处**；其余四处改名都要么动规范名、要么动别人正在用的标签、要么给还不存在的变换先占名字，都不是纯命名能单独收口的。

### 3.2 要动语义（必须先问作者）

J1（`argnums`/`has_aux`）、J3（`jvp` 变换 + 命名空间）、J4（`vjp` 变换）、J5（规则形状）、J6（`transpose` 算子）、J7（`vmap`）、J8（重算换内存）、J11（`pmap`/分片）、J14（组合）。问题在 §6。

## 4. 最小可执行验证

原则：**每一条都用数字或诊断说话，不用文档声称**。分三层。

### 4.1 今天就能跑（本批落地）

新计划 `test/plans/feat/modules/jax_alignment.plan.toml`，四组用例，都在 fast 层：

| 用例 | 断言 | 为什么这就是"对齐" |
|---|---|---|
| `batching.axis0`（nvm/fvm/jit） | `[0.0, 1.0, 2.0]->map<(x) => x*x>` 是 `[0.0, 1.0, 4.0]` | JAX 文档里 `jax.vmap` 的第一个例子就是 `jnp.arange(3.)` 上逐元素平方 |
| `batching.struct`（nvm/fvm/jit） | `Point[]` 上 `p.w * 2.0` 是 `[2.0, 4.0, 6.0]` | 批的是"数组的元素"，元素是结构体也一样，这就是 J7 里我们**有**的那一半 |
| `derivatives.value_and_grad`（nvm/fvm/jit） | `grad(f)<2.0>() = 4.0`，`with_grad(f)<2.0>() = (4.0, 4.0)`，`stop_gradient` 之后仍是 `4.0` | JAX 的 `jax.grad(f)(2.0)` 与 `jax.value_and_grad(f)(2.0)`（`f(x)=x*x`）的数，且顺序是"值在前" |
| `absent.vmap` / `absent.jvp` / `absent.vjp` / `absent.checkpoint` | 四个名字今天都**不是值**：`SemanticDiag::UnresolvedReference`，消息点名该名字 | 这是"还没对齐"的**负例**；对齐落地时，这四条各自改成 4.2 的正例，一改就翻 |

负例不是凑数：它们把"这个名字现在没有"钉住，任何一条在没改用例的情况下变绿或变红，都说明表面变了而验收没跟上。

### 4.2 对齐之后翻过来的（数字写死）

- **`jvp`**：`jvp(square)(2.0, 1.0)`（调用形按 §6 Q2 定）给 `(4.0, 4.0)`；再与中心差分 `((2+h)² − (2−h)²)/2h` 对照——`transform_rules` 已经在**规则层**做过这件事，这里做在**变换层**。
- **`vjp`**：`let (y, pull) = vjp(square)(2.0)`；`y == 4.0`、`pull(1.0) == 4.0`；再用伴随恒等式 `<J v, ct> == <v, Jᵀ ct>` 抽查一次矩阵乘（`transform_rules` 对规则用的就是这个恒等式）。
- **`vmap`**：`vmap(square)([0.0, 1.0, 2.0]) == [0.0, 1.0, 4.0]`；两个参数、`in_axes=(0, None)`；`vmap(grad(square))([0.0, 1.0, 2.0]) == [0.0, 2.0, 4.0]`（每元素的梯度，与直接 `map<grad(square)>` 逐元素相等）。
- **`checkpoint`**：见 4.3。

### 4.3 数值证明不了的：`checkpoint`

`checkpoint` 在值上是恒等，所以**不能用输出证明**。能证明它的只有两件事：

1. **执行次数**：被标注的函数体里放一个计数器（`sync` 块里加一并打印）。不标注时 `grad` 走一遍（1 次）；标注之后反向要重算，所以是 2 次。JAX 的 remat 就是这个现象。
2. **图**：`std::jgir` 或 `std::stats` 上，标注段在反向图里出现的是重算的子图，不是保存中间量的节点。

这两条本批都**不写**（变换不存在），但验收的形状现在就定下来，免得落地时又退回"文档说对齐了"。

### 4.4 组合（J14）

`grad(vmap(f))` 与 `vmap(grad(f))` 用 JAX 文档里的同名例子：`f(x) = x*x` 上 `vmap(grad(f))` 在 `[0,1,2]` 是 `[0,2,4]`；`grad(vmap(f))` 对一个"先批、再求和"的函数给 `[2xᵢ]`。翻转条件同上：4.1 的负例改成这两条正例。

## 5. 本批落了什么

- 一笔：`stdlib/prelude.cml` 里 `map` 的批处理合同（§3.1 第一行）+ `docs/stdlib-reference.md` 重新生成 + 本页。
- 一笔：`test/plans/feat/modules/jax_alignment.plan.toml` 与 `test/cases/modules/autodiff/jax_*.cml`（§4.1）。
- 没碰：三个算子、`#jvp`/`#transpose`/`#vjp` 三个标签、`pullback.cpp`、`ckpt` 模块、任何语义。

## 6. 交给作者的问题

每条给我的倾向，方便直接裁。

每条依据哪一项：Q1→J1+J2、Q2→J3+J4+J5、Q3→J6、Q4→J8、Q5→J7、Q6→J2。**六条没有一条依赖 J9–J13 的数据**：J9–J13 是"已有、不动"的结论（`jit` 是遍、`scan`/`while_loop`/`cond` 在语言层、`pmap`/分片等 GPU、`attempt` 有意保留、`stop_gradient` 同名同义），它们不产生待裁的问题，任何一条被裁掉或改判也不会让这五条变。反过来，J9–J13 若哪条要被推翻（例如 `jit` 要变成函数级变换），它才需要自己的问题，那一条会新开、并依赖 `std::jit` 的现有粒度。

1. **`grad` 要不要加 `has_aux`？** `argnums` 我倾向不加：Camel 的答案是"要微分的数据绑成 `with` 参数（lambda）"，设计原则 9.9 已经这么写；`has_aux` 没有等价的写法（现在返回元组会被 `NotDifferentiable` 拒），JAX 的对照例子常用它。加，是 API 增项；不加，就把它写进"与 JAX 的差别"。
2. **`jvp`/`vjp` 作函数名，与标签 `#jvp`/`#vjp` 撞名（同一命名空间）。** 改标签名（`#custom_jvp`/`#custom_vjp`，顺带对齐 JAX 的 `custom_*`），还是给标签单独一个命名空间？我倾向改标签名：JAX 的 `custom_` 前缀本来就是区分"用户给的导数"与"变换"，而且改名只动 `stdlib/autodiff.cml` 与两个用例。
3. **线性转置算子叫 `transpose` 还是 `linear_transpose`？** `tensor::transpose` 已占 `transpose`（数据转置），同一命名空间。我倾向 `linear_transpose`（JAX 的拼法，也正是为了这个冲突）。
4. **重算换内存的变换叫 `checkpoint` 还是 `remat`？** `ckpt` 的说明里"checkpoint"是持久化。我倾向 `remat`：JAX 的别名、空着、不会让持久化的读者误会。
5. **`vmap` 的签名做到哪一步？** 只批第一个参数的轴 0（`vmap(f)`），还是 `vmap(f, in_axes, out_axes)` 全做（含按维度名对齐，5.4 有维度名可用）？我倾向先做"一个参数、轴 0、`out_axes=0`"，与 `map` 的现有含义接上，再按维度名扩。
6. **`with_grad` 要不要改名 `value_and_grad`？** 我倾向不改：原则 9.9 与库说明都用 `with_grad`，改名的收益只是与 JAX 同拼写。

## 7. 返工（2026-10-30 07:30）：`vmap` 补了张量的前导轴

作者在 `docs/handover/reviews/questions-2026-10-29.md` 末尾追加了一节：`8275d460d` 的 `vmap` 只接受 `T[]`，是把 `map` 换个名字；JAX 的 `vmap` 主要用在张量上（`f: f32[d] => U` 作用在 `f32[B, d]` 上，按轴 0 批）。裁定：`vmap(f)` 作用在 `T[B, ..]` 上、结果是 `U[B, ..]`，与 `T[]` 同一个名字、按参数类型分派；语义先按逐行应用实现（结果与将来的向量化相同），向量化留给第 6 条的批处理规则集；多参数与 `in_axes` 照旧排在后面。

落地是一处语言原语加一条声明：

- `types` 新增编译器提供的类型函数 `RowsOf<T>`：第一维元素为 `T` 的数组类型（`RowsOf<f32[3]>` 是 `f32[?, 3]`，`RowsOf<f32>` 是 `f32[?]`），是 `ArrayType::firstDimensionElement()` 的反函数——`x[i]` 与 `for x in xs` 取的正是它说出的那一维（设计原则 5.4）。声明写不出的签名靠类型函数算出，这是设计原则 10.4／21 的那条路。
- 于是 `vmap` 只有一条声明 `vmap::<T, U>(f: (row: T) => U): (xs: RowsOf<T>) => U[]`，秩 1 批元素、秩 2 批行、秩 3 批整块：`range(0, len(xs), 1)->map<(i: int): U => f(xs[i])>`，逐行应用，与将来的向量化结果相同。
- 用例多了一条：`batching.tensor`（`jax_vmap_tensor.cml`，`vmap((r: f32[3]) => sum(r * r))` 在 `f32[4, 3]` 上是 `[14.0, 77.0, 194.0, 3.0]`），五后端。A/B：把声明改回 `(xs: T[]) => xs->map<f>`，恰好这五条红、其余全绿。
- 本页第 30 行（J7「没有；最近的是 `map`」）与第 95 行（「批的是它唯一那一维」）记的是返工之前的状态，保留作对照；多参数与 `in_axes` 仍是待做项，两处拒绝用例没变。
- 没做：`in_axes`、第二个批处理参数、按维度名对齐。多参数今天拒绝在类型层（`GenericArgumentsMismatch` 点名实参的两个参数与被调的 `f: (row: T) => U`），**没有**「不支持」字样；要诊断原样说出「多个批处理参数」，需要 `vmap` 自己的解析器（`grad` 那一套 computed signature，`ParamArgumentUnknown`/专用诊断都在 C++ 里），这一批没做。