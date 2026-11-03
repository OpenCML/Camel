# 做完 ①（规则交出原值）与 ②（被调函数的 `lin` 当外层残差）之后，库层与引擎还差哪几条

**第一行，作者要的判据：①② 不够。** ② 的判据原文是"库层 `with_grad` 在 `grad_first_with_parameter`
上与引擎同答"，但**库层没有 `with_grad` 这个可被调用的东西**：`stdlib/vjp.cml` 只导出 `vjp` 和一个只收
`(x: float) => float` 的 `grad`，`stdlib/signatures/autodiff.cml#with_grad` 是不编译的参考声明。② 落地
后能测的只有把组合内联写出来的 `test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml`。
要按原文那一句测，② 之后还得补一笔"库层给一个 `with_grad` 导出"，或把判据改写成"内联组合同答"。
下面第 3 节第 5 行是这条，验法也写在那里。

树 `lane-libgrad`，基线 `origin/develop` **`131676081`**（本轮 fetch 实测）。读数只取主机闸门。

## 1. 现状：四个算子走哪条路（`file#Symbol`）

| 角色 | 位置 | 一句话 |
|---|---|---|
| 四个算子的**类型** | `modules/autodiff/operators.cpp#GradResolver` | 唯一签名来源，`gradientSignature` 算 `Tangent<With 的第一个>` |
| 四个算子注册 | `modules/autodiff/operators.cpp#operatorGroups` | `grad`/`with_grad` 各两条规则（plain 与 `_aux`） |
| 四个算子的**实现** | `modules/autodiff/operators.cpp#gradFunction` → `modules/autodiff/pullback.cpp#buildGradientGraph` | 运行期建图；四个 kernel 全走它，**没有第二条路** |
| 建图的第二处 | `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee` | 嵌套 `grad` |
| 库层 reverse 组合 | `stdlib/vjp.cml#vjp` | `transpose(split(jvp(f)))` |
| 库层 `grad` | `stdlib/vjp.cml#grad` | **只收 `(x: float) => float`** |
| 库层 `with_grad` | 无 | 导出不存在 |
| 库层规则 | `stdlib/rulesets/jvp.cml`（23 头）、`stdlib/rulesets/transpose.cml`（19 头） | D1 的缺口面 |
| 库层切线/余切线 | `stdlib/autodiff.cml#add_tangent` … `#div_cotangent_c` | `^` 没有 helper |

## 2. 用例面（主机读数，`camel-remote.ps1`）

| 计划 | 总数 | 绿 | 红 | known fail | 时刻 / load 1m·5m·15m（run 前 → 后） |
|---|---|---|---|---|---|
| `test/plans/feat/modules/autodiff.plan.toml` | 263 | 263 | 0 | 0 | 02:35:03 → 1m 16.39 → 35.13 |
| `test/plans/feat/compile/transforms.plan.toml`（基线，干净树） | 105 | 102 | 2 | 1 | 02:36:00 → 1m 32.21 → 25.62 |
| 同上（加我这笔之后） | 107 | 104 | 2 | 1 | 02:41:26 → 1m 15.48 → 14.95 |
| `test/plans/feat/transforms`（目录：`split` + `compositions`） | 29 | 21 | 0 | **8** | 02:41:43 → 1m 14.66 → 14.95 |

那 8 个 known fail 全在 `compositions`，四条组合各两个后端：`grad_vmap_f`、`vmap_grad_f`、`jvp_vjp_f`、
`grad_vjp_g_zero`。`split` 那 20 条（含 `split_call_one`、`split_loop_*`）全绿。

基线就红的两个：`transpose_loop_escape.two_values_of_two_kinds.nvm`（`transpose has no transpose for
this function: the membe…`，循环带出两个不同类型的值）、`transpose_loop_escape.second_state_of_the_same_loop.nvm`
（`split does not build this function (this loop is on the chain the exit reads…)`）。**它们既不在 ① 名下
也不在 ② 名下**，动它们的人要先看清 `split` 的 loop forward 与 `transpose` 的 escape 两支。

## 3. 差分项：①② 之后仍差的，逐条可验

先记**已经同答**的四条（同一台机、同一棵树量到，说明差集不是"整个库层"）：`x³` 引擎 27.0 = 库层 27.0
（`stdlib/vjp.cml#grad`）；`x / y` 的 `d/dx` 0.25 两侧逐位相同且库层还答 `d/dy = -0.375`；`tanh` 在
`[0.5, -1.0]` 上 `[0.7864477329659274, 0.41997434161402614]` **二进制相同**；`take(x, [0,0,2])`（重复下标）
两侧 `[2.0, 0.0, 1.0]`。

| # | 差什么 | 现在读到的（引擎 vs 库层） | 定位 | 验法（用例 + 期望） |
|---|---|---|---|---|
| 1 | **`mean` 的负轴反向** | 引擎 `[[1/3 ×3]×2]`；库层运行期 `Index -1 is out of range for an array of length 2`，且 `split`/`transpose` **都成功了**（`residuals=2`、`rhs=sum_t,mul_tensor_t,dtod_t,div_t`） | `stdlib/rulesets/jvp.cml#jvp_mean_axis` 的 `T(1.0 / float(shape(xs)[axis]))`：轴原样当下标用，`-1` 不是任何 extent 数组的下标；正轴 `mean<1,true>` 同一条路是过的 | **本轮新加两条**：`#mean_axis_negative_axis.engine`（断言上面那个数组 + 两个拼法同值）、`#mean_axis_negative_axis.refused_by_the_library`（钉 `Index -1 …` + `rules=jvp_mean_axis,jvp_sum`） |
| 2 | **`stop_gradient` 没有库层规则** | 引擎 4.0；库层在**前半边就拒**：`skip=rule uri=autodiff:stop_gradient reason=the operator has no forward rule bound at all` | 缺 `jvp_stop_gradient` 与 `stop_gradient_t`，两条都不在 | 引擎腿 `grad(<w*w + stop_gradient(w*10)>)<2.0>()` = `4.0`；库层腿同一程序换 `vjp` 必须答同一个 `4.0`，今天以那条 `skip=rule` 钉住 |
| 3 | **`^`（`math:pow_d`）没有转置律** | 引擎 `d(w²)/dw = 6.0`；库层 `skip=rule uri=math:pow_d reason=unbound` → `skip=call-no-transpose` | 缺 `pow_t`；`jvp_pow` 已有 | 已有用例对：`pow_gradient_through_the_engine` / `pow_gradient_refused_by_the_library` |
| 4 | **库层 `grad` 只到 `(x: float) => float`** | 两参数函数：引擎答；库层 `No matching function found for argument types: <> ((x: float, y: float) => float pure)` | `stdlib/vjp.cml#grad` 的声明面；根在 D2（`Gradient<F>` 写不出，等类型层裁定） | `test/cases/modules/autodiff/grad_first_with_parameter.cml` 的五组数（`gx=3.0`、`pg={ a: 4.0, b: 1.0 }`、`wg=[4.0, 5.0, 6.0]`、`sk=6.0 sx=4.0`）就是库层 `grad` 将来要逐字答出的 |
| 5 | **库层没有 `with_grad`** | 今天只有一个内联组合的负例，停在 `split` 的 forward：`skip=forward construct=a port uri=- why=the port is not a parameter of the graph` | ② 的机制本身 + 一个不存在的导出 | ② 落地后：`with_grad_composition_meets_the_split_wall.cml` 必须转正并答 `value=0.125 w=-0.75`（`library_grad_is_the_composition.cml` 的引擎腿已钉这三个数） |
| 6 | **`has_aux` 四种形式库层零覆盖** | `grad(f, has_aux: true)` → `(gradient, aux)`；`with_grad(…, has_aux: true)` → `((value, aux), gradient)`。库层没有任何一条 | 没有设计，也没有律 | 引擎侧已有用例（`autodiff_value_with.plan.toml`）；库层腿只能先写成"按名拒"，不许与别的词合并 |
| 7 | **其余张量算子（~38 个）** | 引擎 61 个名字，库 `transpose` 19 个头覆盖 9 个左右 | D1，一条一条写 `*_t` | 一个 diff：`setVjp` 名字集合 − 规则头集合 = 空 |
| 8 | **四条 transform 组合还到不了** | `feat/transforms` 29 = 21 绿 / 8 known fail。`grad_vmap_f` 的墙是"被调函数的值"（`cannot differentiate through this call of a function value`）；`vmap_grad_f` 是 `vmap` 的常量规则；`jvp_vjp_f` / `grad_vjp_g_zero` 是 `transpose` 拒非 `Float64` 出口（`takes a function that returns a` 加反引号包着的 `float`） | 前一条 ② 的库层版本就是它的答案（"转置一个已知的线性函数值 = 调 `transpose(v)`"）；后两条在 `transpose` 的出口类型上，与 `stdlib/types.cml#Transpose` 的声明面同一处 | `test/plans/feat/transforms/compositions.plan.toml` 的四个 `known_fail_msg`，摘标记的前提是各自 `verify_script` 的数（`vmap_grad_f` 是 `[2, 4, 6]`） |

## 4. ①② 会吃掉、别重复派的

- **被捕获的值在 `split` 的 forward 里不是残差**（`with_grad_composition_meets_the_split_wall.cml` 那面墙）：
  ② 的"`lin_g` 照 15:15 成为外层残差、进 `lin` 的闭包"就是它的通用版。判据即第 3 节第 5 行。
- **`mean_axis_t` / `take_t` 读线性操作数**（17:35 点名）：① 落地后 `mean_axis_t` 只读已知实参。
  `mean_axis1_keepdims_gradient_reads_the_row_length` 与 `rules_angle_args.mean_axis_transpose.nvm` 是判据。
- **注意**：① 改的是**转置**规则怎么拿形状，**不改** `jvp_mean_axis` 怎么读轴——第 3 节第 1 行那条
  `-1` 的账落在**前向**规则上，① 与 ② 都不会顺手修掉它。

## 5. 本轮落的码与没碰的

新增 `test/cases/compile/transforms/mean_axis_negative_axis_through_the_engine.cml`、
`…_refused_by_the_library.cml` 与 `test/plans/feat/compile/transforms.plan.toml` 的两条目（第 3 节第 1 行）。
未改 `stdlib/`、`modules/`、`src/` 一行；未碰 `stdlib/rulesets/{jvp,transpose}.cml` 与
`modules/transforms/{split,split.h,transpose}.cpp`。`GradResolver` 未动：它的类型那一半（`Gradient<F>` 的
结果类型）等类型层拆包的裁定，边界在 `docs/handover/reviews/2026-11-03-can-gradient-of-a-function-be-a-library-type-function.cn.md`。