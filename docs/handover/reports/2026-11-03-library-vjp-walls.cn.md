# 库层 `vjp` 的四堵墙，与标量模型的首步三方数

2026-11-03 18:10 +08:00，车道 `lane-s2`（HEAD `95f8c967c` 之上）。**这一页的数字是标量模型的，不是 S2 张量模型的**：两张表分开写，不要把第二张读成第一张。

## 1. 三方对照（标量模型，`pred = x * w + b`，`loss = 0.5 * (pred - t)^2`）

字面量 `x = 1.5`、`t = 2.0`、起点 `w = 0.5`、`b = 0.75`；用例 `test/cases/compile/transforms/first_step_library.cml`，计划 `test/plans/feat/showcase_s2.plan.toml#first_step.scalar_library_vjp.nvm`（1/1 绿）。

| 元素 | 库层 `vjp(loss_plain)(0.5, 0.75)` → `pullback(1.0)` | 引擎 `with_grad` × `grad` | PyTorch 2.5.1+cu121 | 库层 − PyTorch |
|---|---|---|---|---|
| `dL/dw` | **−0.75** | **−0.75** | **−0.75** | **0.0** |
| `dL/db` | **−0.5** | **−0.5** | **−0.5** | **0.0** |
| `loss` | **0.125** | **0.125** | **0.125** | **0.0** |

库层与引擎逐元素最大绝对差 **0.0**（用例自己打印 `max_abs_diff_library_vs_engine 0.000000000000e+00`）。两条 Camel 边的差别只在权重坐哪个位置：`vjp` 对普通参数求导，所以库层把 `(w, b)` 写成普通参数；引擎的 `grad`/`with_grad` 对第一个 `with` 参数求导，所以引擎把权重写在 `with` 第一位、再把偏置用 lambda 挪到前面（`grad_first_with_parameter.cml` 已钉的挪法）。

**PyTorch 那组的来历**：`D:\Temp\s2-first-step-torch.py`（脚本原文在，读得出来），在本机 `D:\Projects\CamelTest\.venv` 的 torch **2.5.1+cu121**、Python 3.11.7、float64、cpu 上跑出，时间 **2026-11-03 18:10 +08:00**，输出 `D:\Temp\s2-first-step-torch.json`。没有随机数，字面量就是用例里的那四个，所以没有种子可设。

**张量模型的三方数还没有**：`vjp` 今天反不了下面第 2 节第 2 堵墙（数组上的乘法），任何以 `sum` 收尾的 tensor loss 都过不去。

## 2. 四堵墙：哪一堵挡哪一步，缺哪个规则

按张量路径上遇到的先后排。四堵都是我在 `test/cases/compile/transforms/` 下临时用例上量到的原话，临时用例已删，原文留在 `D:\Temp\s2-*`。

| # | 挡住的步骤 | 实测消息（原话截断处标 …） | 缺的东西 / 规则名 |
|---|---|---|---|
| 1 | 被捕获的**数组常数**当算子操作数（`let x = [[…]] as float[2,2]` 然后 `x @ wq`） | `transforms: jvp has no forward transform for this function`（`stdlib/vjp.cml:73`） | `modules/transforms/jvp.cpp:64#pairTypeOf` 只给浮点标量造 `(值, 切向)` 对；数组型值的零切向没有节点可交。不是某条 rule 的问题 |
| 2 | 数组上的 `sum(w * w)` 反向 | `transforms: transpose has no transpose for this function: the rule table has no rule for a step of this body`（`stdlib/vjp.cml:74`） | `stdlib/rulesets/transpose.cml:54#mul_t` 只写到 `is_float::<T>()`（标量），缺 `T[..]` 的乘法律——即 `stdlib/rulesets/jvp.cml:278#jvp_mul_tensor` 的伴随。`stdlib/rulesets/transpose.cml:85#sum_t` 已有，不是它 |
| 3 | `softmax<1>(z)` 的前向 | ``transforms: jvp has no forward rule for `tensor:softmax` (the rule takes (float[..]), and the operator has (floa…`` | `stdlib/rulesets/jvp.cml:283#jvp_softmax` 存在，但算子的 axis 是一个端口，`portsTakeOperands`（`modules/transforms/jvp.cpp`）按 arity 拒——就是 `rules_angle_args` 那条 |
| 4 | `mean<-1, true>(...)` 的反向 | `transforms: transpose has no transpose for this function: the shape this program used has no transpose (the transform named it `arity`)` | `stdlib/rulesets/transpose.cml:179#mean_axis_t` 存在，同样卡在 axis/keepdims 的 arity 上 |

给 `lane-arity` 的三条可照抄：3、4 是同一个 arity 端口帐（axis/keepdims 是 `with` 端口，规则左右两边的 `normTypesCount()` 与节点操作数对不上）；2 是 `mul_t` 缺数组那一半，与 arity 无关，写 `T[..]` 的乘法律即可；1 不在规则表里，是 `pairTypeOf` 的标量假设。

## 3. 待办：用户面两个 `grad` 并存

`stdlib/vjp.cml:84#grad` 只收 `(x: float) => float`，实现是 `vjp(f)` 读种子 1 取 `.0`；用户面另一个 `grad`/`with_grad` 是 `stdlib/signatures/autodiff.cml:51`、`:66` 的声明，实现在 `modules/autodiff/operators.cpp:155#gradFunction`（C++ 直接 pullback，对第一个 `with` 参数求导）。作者 2026-11-03 10:00 的第 3 条要的是库层那个写成一次组合——`<w, ..rest> (..xs) => …`，体里对 `(w) => f<w, ..rest>(..xs)` 做 `vjp`——并删掉只收 `float` 的那个，用户面只剩一个。

写不出来是因为它需要**带体的 `with` 参数包**：`src/compile/gct/builder.cpp:5378` 的守卫拿掉之后，`with <..ws>` 加体里 `inner<..ws>(x)` 直接 `camel: internal error: Unknown GCT NodeType`，而 `with <..ws: int>` 更早被拒（`The rest parameter '..ws' cannot write a type`）。所以这条排在增补 1（语法那件）之后；**在那之前不要删引擎的 `pullback.cpp`**。

## 3.5 第一堵墙的措辞与读数不符（2026-11-03 20:30，车道 `lane-s2wall`，呈上）

第 2 节第 1 行把第一堵墙写成"`pairTypeOf` 只给浮点标量造 `(值, 切向)` 对"，并把责任落在 `modules/transforms/jvp.cpp#pairTypeOf`。**照这一句排期的下一面墙会派错。** 在 `af3a1c774` 上量到的三组读数：

- `pairTypeOf` 今天只问一个问题 —— `tangentSpaceOf(type).kind == Self && space == type`（`modules/transforms/jvp.cpp#pairTypeOf`，函数上方注释记着 2026-11-02 作者撤掉了第二道门）。**数组拿到的是自己类型的对**，不是"没有对"。同一句判据也在 `test/plans/feat/compile/transforms.plan.toml#transforms.jvp_matmul` 的描述里被否掉了（"that is no longer true: `jvp` writes the zero of an array type too"）。
- 无捕获、只吃一个数组参数的函数，`jvp` 正常出数（`jvp(h)([3, 5], (1, 0))` → `value=3 tangent=5`）。所以数组本身不是墙。
- 真正被拒的是**任何被捕获的值**，与类型无关：`(t: float[]) => k * t[0]` 捕获一个 `float` 也照样 `REINTERP jvp function=__g_235__ skip=body`，捕获 `float[2, 2]` 与捕获 `float[]` 报同一句。GIR 里那个节点的类型是 `CLOSURE_PORT, param_kind="capture"`：捕获是图的端口，但**不在 `withPorts` 里**，提升器只给参数、`with` 参数和循环状态建端口，捕获落在"其他端口"那一支、被原样拷贝而**没有切向**，于是体里第一个读它的算子就 `complete=false`，整个体被拒。

**更正后的措辞（建议）**：第一堵墙不是"被捕获的数组操作数没有切向量对"，而是**"被捕获的值没有端口，提升器少一类端口；任何被捕获的值都会让整个体 `skip=body`，数组只是它最常被看见的样子"**。缺的东西在端口那一层，不在 `pairTypeOf`，也不在规则表。

修法与读数见车道 `lane-s2wall`（`Lift a captured value: the lifted copy declares the closure's ports`，测量用例 `test/plans/feat/showcase_s2.plan.toml#first_step.tensor_forward_jvp`）：捕获端口按源顺序建、`appendClosureNode`、`setClosureType`，切向取自身类型的零。改后张量模型的前向出数（loss `10.34375`、`dL/dw = [[3.75, 13.5], [5.5, 19.5]]`，库层 = 引擎 = 手算 = PyTorch，逐元素 0.0），而 `vjp` 路径卡在**下一面墙 `split`**：`REINTERP split function=jvp_of_… skip=forward construct=a port uri=- why=the port is not a parameter of the graph, so neither half takes it`。

**给排期的一条**：`stdlib/rulesets/transpose.cml` 里连 `matmul_t` 都没有（只有 `add_t`/`sub_t`/`mul_t`/`div_t`/`neg_t`/`dtod_t`/`sum_t`/`mean_axis_t`/`idx_arr_t`/`take_t`），所以 MLP 的 `vjp` 会先卡在**乘积的转置**，比第 2 节第 2 行的 `mul_t` 早一个算子。（2026-11-03 21:00 补：`matmul_t` 已落地，读数见 3.6；这一句是当时的读数，留作记录。）

## 3.6 乘积的转置律落地：张量模型里 `vjp` 第一次出数（2026-11-03 21:00，车道 `lane-s2wall`）

`stdlib/rulesets/transpose.cml#matmul_t`：`a · b` 把 `ct · bᵀ` 还给 `a`、`aᵀ · ct` 还给 `b`，只用算子本身和**已知操作数**的 `tensor:transpose`，不重建任何形状、不读线性操作数的值（`knowlen` 那条还没落地，本律不依赖它）。读数（用例 `test/cases/compile/transforms/matmul_transpose_law.cml`，计划 `test/plans/feat/showcase_s2.plan.toml#matmul_t.law_read_through_vjp`）：

| 腿 | 值 | 库层 `vjp` = 引擎 `with_grad` = 手算 |
|---|---|---|
| `sum(x@w)` | 9.75 | `dL/dx = [[0.75, 1.125], [0.75, 1.125]]`，`dL/dw = [[4, 4], [6, 6]]` |
| 整步 `0.5·sum((x@w − t)²)` | 10.34375 | `dL/dx = [[0.9375, 2.34375], [1.4375, 3.875]]`，`dL/dt = [[−0.75, −2.25], [−1, −3.75]]`，`dL/dw = [[3.75, 13.5], [5.5, 19.5]]` |

`max_abs_diff_library_vs_engine w=0.0 value=0.0`，四个元素全在二进制里精确，所以断言是逐字符的、不是容差。

**这条律不是只写一条规则就够，另外两处是它的条件，都量过**：

- `jvp_matmul` 的右边原来调 helper `matmul_tangent(a, b, da, db)`，回程撞 `TRANSPOSE function=rulesets.jvp::matmul_tangent<float> skip=signature`、`skip=call-no-transpose node=6`。现在内联（与 `jvp_mul_tensor` 同形）。**真正该通的是"转置一个调用"**，`modules/transforms/transpose.cpp` 那条路没通之前，内联是唯一每个结点都有律的写法——`stdlib/rulesets/jvp.cml#jvp_matmul` 的注释里点了这条。
- `pred - target` 把数组减法放进了反向图，它没有伴随：`skip=rule uri=tensor:subtract reason=unbound`。补了 `subtract_t`（`add_tensor_t` 的镜像，`(ct, -ct)`）。

**向量的两条腿，也落地了**（2026-11-03 22:15，提交 `9856e2df4`）：`v @ w` 和 `x @ u` 各给手算 = 库层 = 引擎三方，`max_abs_diff` 三项全 `0.000000000000e+00`（两项的 `*_engine_v`/`*_engine_u` 那条腿把**向量本身**放进 `with` 子句，所以秩一的切向也是引擎的答案，不只是库层的）：

| 形 | 值 | 库层 = 引擎 = 手算 |
|---|---|---|
| `v @ w`（`v` 是行） | 3.0 | `dL/dv = [0.75, 1.125]`，`dL/dw = [[1, 1], [2, 2]]` |
| `x @ u`（`u` 是列） | 3.5 | `dL/dx = [[0.5, 0.25], [0.5, 0.25]]`，`dL/du = [4, 6]` |

秩一操作数对面那个切向是**外积**，而两个秩一的乘积是个数，所以用 `sum_to` 在**已知操作数**已有的那几条道上铺：行用 `sum_to(ct, bᵀ) * sum_to(a, bᵀ)` 再转置，列用 `sum_to(ct, a) * sum_to(b, a)`。没有一处读线性操作数的形状，也没有一处把形状从计数重建——每个 helper 都以 `sum_to(…, 操作数)` 收尾，答案的形状就是操作数自己的形状。

**写这条律前量到的两件事**（注释里都留了）：① 走查给规则的类型元组是**每个值一个类型**（切向的元素类型 + 各操作数类型，`ruleCotangent`），所以规则头必须是 `<T, A, B>` 这种类型变量——写 `T[?, k], T[k, ?]` 会被拒 `it takes 1 type parameter(s), and it was asked for 3`；`mul_tensor_t` 早就是 `<T, X, W>`，原因相同。② 一个 ruleset 每个算子+元数只有一条规则（`SemanticDiag::DuplicateRuleForOperator`），三种形状不能写成三条规则，所以秩的区分放在两个按秩重载的 helper 里（每个形状一对）。

**边界跟着挪了，也钉了**：三维及以上、以及向量·向量（答案是数）没有重载，律被选中而它的体对这两个操作数无话可说——走查**按名拒**，而且给了它自己的一类词（2026-11-03 22:25，父代理裁定：不许退回 `rule-shape`，分类名不许合并）：

```
skip=rule-ranks uri=tensor:matmul rule=matmul_t node=6 position=0 ranks=(3, 2) operands=2
reason=No matching function found for argument types: <> (float[..], float[3, 2, 2], float[2, 2]).

transforms: transpose has no transpose for this function: the law `matmul_t` has no cotangent for
`tensor:matmul` at these operands: their ranks are (3, 2), and it serves the ranks its cotangents
are written for (node 6).
```

`rule-ranks` 与 `rule-shape` 必须分得开：后者是"律的**模式**不收这些操作数、什么都还没建"，前者是"模式收了、律被选中、律的**体**对这些秩无话可说"——读者的改法不同（放宽模式 vs 补上缺的那条律）。上一版（`5a2fed990`）把秩写进模式，只能服务一种形状；要一条 key 服务三种形状，秩必然落到 helper 里，**判据的可达性随模式变宽而消失**，报文随之换成 `rule-ranks`。读数钉在 `#matmul_t.rank_three_is_refused_by_name`（两条流都钉），`#matmul_t.vector_operand_is_refused_by_shape` 已随之删除。要变成"三维也答"只需再加一对重载（`sum_to` 那一半已经就是批轴上的归约），但父代理裁定今晚不开这个面。

捕获版（S2 真正的写法）不变，仍停在 `split`（3.5 末尾那面墙）；也就是张量模型的 `vjp` 现在只差 `split` 这一处。

## 4. 复现

```
pwsh -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\s2 `
     -Plan test/plans/feat/showcase_s2.plan.toml -Tier fast -Jobs 4
D:\Projects\CamelTest\.venv\Scripts\python.exe D:\Temp\s2-first-step-torch.py
```