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

**给排期的一条**：`stdlib/rulesets/transpose.cml` 里连 `matmul_t` 都没有（只有 `add_t`/`sub_t`/`mul_t`/`div_t`/`neg_t`/`dtod_t`/`sum_t`/`mean_axis_t`/`idx_arr_t`/`take_t`），所以 MLP 的 `vjp` 会先卡在**乘积的转置**，比第 2 节第 2 行的 `mul_t` 早一个算子。

## 4. 复现

```
pwsh -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\s2 `
     -Plan test/plans/feat/showcase_s2.plan.toml -Tier fast -Jobs 4
D:\Projects\CamelTest\.venv\Scripts\python.exe D:\Temp\s2-first-step-torch.py
```