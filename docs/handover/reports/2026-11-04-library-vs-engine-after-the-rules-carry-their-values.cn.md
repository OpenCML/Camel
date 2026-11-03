# 做完 ①（规则交出原值）、②（被调函数的 `lin` 当外层残差）与 `with` 切向之后，库层与引擎还剩哪几条不同答

**第一行，回答作者那一句："`grad_first_with_parameter` 之外还剩几条不同答？"——11 条，其中只有 1 条是
"错数"，其余 10 条是"走不到那一步"或"根本写不出"。** 逐条：

- **错数 1 条**：`with` 实参的切向（`jvp` 不给 `with` 值写切线，`lin` 恒等于 0）。**已由 `lane-jvpwith`
  从 `db741501d` 起接手，我不动 `modules/transforms/jvp.cpp`。** 我在本轮树上的独立读数与 `lane-lincall`
  的读数**不是同一个症状**，见第 3 节第 0 行——这一点比数本身重要。
- **走不到那一步 3 条**：`with` 参数是 struct（lift 就 `skip=body`）、是 tensor（`split` 拒"端口不是图的参数"）、
  是第二个 `with`（`jvp` 直接拒，连 `skip=` 行都没有）。
- **写不出 / 缺规则 7 条**：`mean` 的负轴（前向规则把轴当下标）、`stop_gradient` 无规则、`^` 无 `pow_t`、
  `f32` 出口被 `transpose` 拒（四条组合里两条的墙）、库层 `grad` 的签名面（D2）、库层无 `with_grad` 导出、
  `has_aux` 四形式零覆盖，以及 ~38 个算子缺 `*_t`（D1，算一条）。

树 `lane-libgrad`，基线 `origin/develop` **`131676081`**（本轮 fetch 实测），本页读数在我自己的提交
`120bd8070` 上取。读数只走主机闸门。

## 1. 现状：四个算子走哪条路（`file#Symbol`）

| 角色 | 位置 | 一句话 |
|---|---|---|
| 四个算子的**类型** | `modules/autodiff/operators.cpp#GradResolver` | 唯一签名来源，`gradientSignature` 算 `Tangent<With 的第一个>` |
| 四个算子的**实现** | `modules/autodiff/operators.cpp#gradFunction` → `modules/autodiff/pullback.cpp#buildGradientGraph` | 运行期建图，四个 kernel 全走它，没有第二条路 |
| 建图的第二处 | `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee` | 嵌套 `grad` |
| 库层 | `stdlib/vjp.cml#vjp`、`#grad` | `vjp` = `transpose(split(jvp(f)))`；`grad` **只收 `(x: float) => float`**；**无 `with_grad`** |
| 规则 | `stdlib/rulesets/jvp.cml`（23 头）、`stdlib/rulesets/transpose.cml`（19 头） | D1 的缺口面 |

## 2. 用例面（主机读数，`camel-remote.ps1 -Tier fast -Jobs 4`）

| 计划 | 总数 | 绿 | 红 | known fail | 时刻 → load 1m（run 前 → 后） |
|---|---|---|---|---|---|
| `feat/modules/autodiff.plan.toml` | 263 | 263 | 0 | 0 | 02:35:03 → 16.39 → 35.13 |
| `feat/compile/transforms.plan.toml`，基线干净树 | 105 | 102 | 2 | 1 | 02:36:00 → 32.21 → 25.62 |
| 同上，我的提交 `120bd8070`（干净） | 107 | 104 | 2 | 1 | 02:43:35 → 15.78 → 17.26 |
| `feat/transforms`（`split` + `compositions`） | 29 | 21 | 0 | **8** | 02:41:43 → 14.66 → 14.95 |

基线就红的 2 条：`transpose_loop_escape.two_values_of_two_kinds`（`transpose has no transpose for this
function: the membe…`）、`.second_state_of_the_same_loop`（`split does not build this function (this loop is
on the chain the exit reads…)`）——**既不属 ① 也不属 ②**。8 个 known fail 全在 `compositions`：
`grad_vmap_f`、`vmap_grad_f`、`jvp_vjp_f`、`grad_vjp_g_zero`（各 2 后端）。

**① 到今天还没落**：它自己的判据之一 `vjp_loop.rounds_are_undone_in_reverse.nvm` 在 `cabe67e0e` 上仍是
`[XFAIL]`（17:35 那条要求它答 `(1.0, 1.0)` 并摘掉已知失败标记）。`split` 那 20 条其余全绿。

**已经同答的**（同机同树，说明差集不是"整个库层"）：`x³` 27.0 = 27.0；`x/y` 的 `d/dx` 0.25 逐位同，库层还答
`d/dy = -0.375`；`x - y` 引擎 1.0 = 库层 1.0 且 `d/dy = -1.0`；`tanh` `[0.7864477329659274,
0.41997434161402614]` 二进制同；`take(x,[0,0,2])` 两侧 `[2.0, 0.0, 1.0]`。

## 3. 差分项，逐条带验法与拒绝原句

| # | 差什么 | 我量到的 | 定位 | 验法 |
|---|---|---|---|---|
| 0 | **`with` 实参没有切向**（唯一一条错数） | 我的树 `120bd8070` 上，同一个形状**没走到 0.0 那个数**：`vjp((ww) => weighted<ww>(2.0))(3.0)` 在 `transpose` 就拒——`this call's callee has no transpose of its own, so the call cannot be turned around`（`stdlib/vjp.cml:88`），前置行是 `REINTERP jvp function=weighted … rules=jvp_mul,jvp_add`。也就是说：**零切向这个症状要在"转置一个调用"能过之后才看得见**，`lane-lincall` 在 `db741501d` 上量到 `grad=(0.0)` 是它的树比我多走了一步 | `modules/transforms/jvp.cpp`（`with` 实参写的是 Data 常量而不是切向口） | 引擎 `grad(weighted)<3.0>(2.0)` = `3.0`；库层腿必须答同一个 `3.0`。`lane-jvpwith` 接手，我不重复修 |
| 1 | **`with` 参数是 struct** | `REINTERP jvp function=point_loss skip=body` → `transforms: jvp has no forward transform for this function`（`stdlib/vjp.cml:87`） | lift 不认带 struct `with` 参数的函数体 | 引擎 `pg={ a: 4.0, b: 1.0 }` / `pvg=(11.0, { a: 4.0, b: 1.0 })`（`grad_first_with_parameter`） |
| 2 | **`with` 参数是 tensor** | lift 成（`rules=jvp_mul_tensor,jvp_sum,jvp_float_from_float`）→ `split` 拒：`the port is not a parameter of the graph, so neither half takes it` | `split` 的 forward 仍不把闭包端口当残差；与 `with_grad_composition_meets_the_split_wall.cml` 同一面，② 的判据用例就是它 | 同上用例的 `wg=[4.0, 5.0, 6.0]` / `wvg=(32.0, [4.0, 5.0, 6.0])` |
| 3 | **第二个 `with` 参数（当常量）** | `transforms: jvp has no forward transform for this function`，且**没有 `skip=` 行** | lift 对"两个 `with` 参数"直接拒 | `sk=6.0 sx=4.0` |
| 4 | **`mean` 的负轴** | 引擎 `[[1/3 ×3]×2]`；库层运行期 `Index -1 is out of range for an array of length 2`，而 `split`/`transpose` 都成功（`residuals=2`、`rhs=sum_t,mul_tensor_t,dtod_t,div_t`） | `stdlib/rulesets/jvp.cml#jvp_mean_axis` 的 `T(1.0 / float(shape(xs)[axis]))`：轴原样当下标。正轴 `mean<1,true>` 同一条路是过的 | **本轮新加两条**：`#mean_axis_negative_axis.engine`、`#mean_axis_negative_axis.refused_by_the_library` |
| 5 | **`f32` 出口** | `transpose` 在类型层就拒：``Invalid call to 'transpose' … `transpose` takes a function that returns a `float` ``（`vjp<(x: f32) => f32 pure>`） | `modules/transforms/transpose_operator.cpp` 的出口类型；与 `compositions.jvp_vjp_f`/`grad_vjp_g_zero` 两个 known fail 同一堵墙 | 那两条 `known_fail_msg`；摘标记要有数 |
| 6 | **`stop_gradient` 无库层规则** | 引擎 4.0；库层 `skip=rule uri=autodiff:stop_gradient reason=the operator has no forward rule bound at all` | 缺 `jvp_stop_gradient` 与 `stop_gradient_t` | 引擎腿 `4.0`，库层腿换 `vjp` 必须同答 |
| 7 | **`^`（`math:pow_d`）无转置律** | `skip=rule uri=math:pow_d reason=unbound` → `skip=call-no-transpose` | 缺 `pow_t` | 已有对：`pow_gradient_through_the_engine` / `…_refused_by_the_library` |
| 8 | **库层 `grad` 的签名面** | 两参数：`No matching function found for argument types: <> ((x: float, y: float) => float pure)` | `stdlib/vjp.cml#grad`；根在 D2，等类型层裁定 | `grad_first_with_parameter` 的五组数就是它将来要逐字答出的 |
| 9 | **库层无 `with_grad`** | 导出不存在；`stdlib/signatures/autodiff.cml#with_grad` 是不编译的参考声明 | ② 的判据"库层 `with_grad` 与引擎同答"**今天没有可测对象** | `with_grad_composition_meets_the_split_wall.cml` 转正并答 `value=0.125 w=-0.75` |
| 10 | **`has_aux` 四形式** | 库层零覆盖 | 没有设计也没有律 | 引擎侧 `autodiff_value_with.plan.toml`；库层腿先按名拒，词不许与别的合并 |
| 11 | **~38 个张量算子缺 `*_t`** | 引擎 61 个名字，库 `transpose` 19 头覆盖 9 个左右 | D1 | `setVjp` 名字集合 − 规则头集合 = 空 |

## 4. ①② 会吃掉、别重复派的

- **`split` 的 forward 不把闭包端口当残差**（第 2 行那面墙）：② 的"`lin_g` 照 15:15 成为外层残差"就是它的
  通用版，判据是 `with_grad_composition_meets_the_split_wall.cml` 转正。
- **`mean_axis_t` / `take_t` 读线性操作数**（17:35 点名）：① 落地后 `mean_axis_t` 只读已知实参；判据
  `mean_axis1_keepdims_gradient_reads_the_row_length` 与 `rules_angle_args.mean_axis_transpose.nvm`。
- **注意**：① 改**转置**规则怎么拿形状，不改 `jvp_mean_axis` 怎么读轴——第 4 行那条 `-1` 的账落在**前向**
  规则上，① 与 ② 都不会顺手修掉它。

## 5. 本轮落的码与没碰的

`120bd8070`：新增 `test/cases/compile/transforms/mean_axis_negative_axis_{through_the_engine,refused_by_the_library}.cml`
与 `test/plans/feat/compile/transforms.plan.toml` 两条目（第 4 行）。未改 `stdlib/`、`modules/`、`src/` 一行；
未碰 `stdlib/rulesets/{jvp,transpose}.cml`、`modules/transforms/{split,split.h,transpose,jvp}.cpp`。
`GradResolver` 未动：类型那一半的边界在
`docs/handover/reviews/2026-11-03-can-gradient-of-a-function-be-a-library-type-function.cn.md`。
## 6. 复现（第 0–3 行的四条，探针已删，正文照抄即可）

每条都是"引擎腿 + 库层腿"一个程序；引擎腿的数取自 `grad_first_with_parameter` 已钉的期望，库层腿今天停在下表那句：

```cml
// 第 0 行：with 参数的切向
import { grad, with_grad } from autodiff
import { vjp } from vjp
with <x: float>
func weighted(y: float): float { return x * y + x }
func main(): int sync {
    let e = grad(weighted)<3.0>(2.0)                                  // 3.0
    let (v, pullback) = vjp((ww: float) => weighted<ww>(2.0))(3.0)     // 拒：this call's callee has no transpose of its own
    return 0
}
```

```cml
// 第 1 行：struct 的 with 参数          -> REINTERP jvp function=point_loss skip=body
with <p: Point>
func point_loss(y: float): float { return p.a * y + p.b }
let (v, pullback) = vjp((pp: Point) => point_loss<pp>(4.0))({ a: 2.0, b: 3.0 })
// 第 2 行：tensor 的 with 参数          -> split: the port is not a parameter of the graph
let (v, pullback) = vjp((ww: f64[..]) => dot<ww>(x))(w)
// 第 3 行：第二个 with 参数当常量        -> jvp has no forward transform（无 skip= 行）
let (v, pullback) = vjp((kk: float) => scaled<kk, 3.0>())(2.0)
```

同一台机上的读数（`120bd8070`，load 15.77/15.96，`D:\Temp\libgrad-probe-with.log`、`…-probe-w1b.log`）；
四条都被 `-Plan` 单目标跑过，未入 git。