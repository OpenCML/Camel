# 库层与引擎今天还剩哪几条不同答：11 项重算（2026-11-04 03:47–03:53，纯测量）

树 `lane-vjparity`，基线取实时 `origin/develop` **`c4391db57`**（本轮 `git fetch` 实测，不是写死的）。
上一张表是 `lane-libgrad` 在 `120bd8070` 上量的（1 错数 / 3 不可达 / 7 缺失）。本页逐条重算结果为
**2 已修 / 1 仍是错数 / 4 仍是拒绝 / 4 仍是缺失**——数字变了，墙也挪了。

**读法**：两条腿要跑同一个形状。除第 5 项外，库层那一腿都是类型期或运行期的拒绝，一个程序装不下两条腿，
所以它们各是**两个程序**（引擎腿 `import { grad, with_grad } from autodiff`，库层腿 `import { vjp } from vjp`）；
第 5 项两腿都能编译，于是**一个程序里同时跑两腿**并打印差的平方范数。每个数字都是
`camel <probe> std::nvm` 的原样输出，探针在 `D:\Temp\vjparity-probes\`（不入 git）。读数一律取自主机
`88zjwei` 上本树刚构建的 `out/latest`，带 load，未在本机跑。

## 合并之后：这张表上哪几行动了（2026-11-04 05:25，`integ2/rules-batch2`）

上表是它自己那棵树上的原样读数（`lane-vjparity`，基线 `c4391db57`），**不改写**。第二批合进来之后，
其中四行已经不是那个答案，动的是哪一支、新的读数钉在哪条用例上，见下：

| # | 现在 | 由谁动、钉在哪 |
|---|---|---|
| 1 | `with` 实参的切向**已修**：库层腿从 `pullback=(0.0)` 变成 **`3.0`**，与引擎腿 `grad=3.0 lambda=3.0 with_grad=(9.0, 3.0)` 逐位同 | `lane-withtan#2e14a30dc`（合并在 `713e1ff26`）；钉在 `test/cases/compile/transforms/library_grad_of_a_with_weight.cml`（`weighted engine=3.0 value=9.0 grad=3.0`、`both value=18.0 pullback=(6.0, 3.0)`） |
| 2 | 结构体 `with` **仍是拒绝**，句子与位置变了：``jvp does not reinterpret a `with` value of type `Point` yet (…)``，带源位置 | 同上；`lane-withtan` 量到的逐字句 |
| 3 | 张量 `with` **仍是拒绝**，句子变了：``jvp does not reinterpret a `with` value of type `float[..]` yet (…)`` | 同上 |
| 6 | `f32` 出口**已修**：`transpose` 不再因出口类型拒（`Tangent<f32>` 是 `f32`，余切按该宽度的加法相加） | `lane-f32trans#9f68dfdb2`（合并在 `78fa5c12a`）；读数与用例见 `test/plans/feat/transforms/compositions.plan.toml` 的 `known_fail_note` |

第 2、3 项为什么宁可拒绝：抬升结构体权重会让 `split` 在 `nodevm.cpp` 的 `idx < t->size()` 上崩
（实测 exit 134），而不崩的那条路是照旧写零、把错数留着。按“对 > 明确拒绝 > 错数 > 崩”，只能在抬升里
按名字拒。**同名判定（拒绝）不变，句子与位置变了。**

第 1 项那个错数为什么算今晚最值钱的一条：它不是“缺了切向”，是 `liftGraph` 用
`zeroConstantFor(...)` 把权重方向**主动填成了 `0.0`**——不报错、答案悄悄错。
## 11 项逐条

| # | 差什么 | 今天的引擎腿（逐字） | 今天的库层腿（逐字） | 归类 | 证据 | 下一笔动哪个文件 |
|---|---|---|---|---|---|---|
| 1 | `with` 实参的切向 | `grad=3.0 lambda=3.0 with_grad=(9.0, 3.0)` | `value=9.0 pullback=(0.0)` | **仍是错数**，但墙换了：`lane-lincall` 的合并（`db741501d`）拆掉了“转置一个调用”那面墙，零切向**第一次成为可观测的数**（libgrad 量到的是 `this call's callee has no transpose of its own`） | `modules/transforms/jvp.cpp#jvpSignature`：一个 `with` 形参只有一个值口，“the pair is not written for a `with` parameter at all” | `modules/transforms/jvp.cpp`；修在未合并的 `lane-jvpwith#bf9775359`，落它之前别动 `stdlib/vjp.cml` |
| 2 | 结构体 `with` | `grad={ a: 4.0, b: 1.0 } with_grad=(11.0, { a: 4.0, b: 1.0 })` | `REINTERP jvp function=point_loss skip=body` → `transforms: jvp has no forward transform for this function.` | 仍是拒绝（原样） | `modules/transforms/jvp.cpp#jvpSignature`；拒句抛在 `stdlib/vjp.cml#vjp` | `modules/transforms/jvp.cpp` |
| 3 | 张量 `with` | `grad=[4.0, 5.0, 6.0] with_grad=(32.0, [4.0, 5.0, 6.0])` | lift 过（`rules=jvp_mul_tensor,jvp_sum,jvp_float_from_float`）→ `split` 拒：`skip=forward construct=a port uri=- why=the port is not a parameter of the graph, so neither half takes it` | 仍是拒绝（原样，与 ② 的判据用例同一面） | `modules/transforms/split.cpp#Half::makeCopy`（`withPortOf` 里没有这个端口，`portIndex` 里也没有） | `modules/transforms/split.cpp` |
| 4 | 第二个 `with` | `first=6.0 second=4.0` | `transforms: jvp has no forward transform for this function.`，且**没有 `skip=` 行** | 仍是拒绝（原样） | `modules/transforms/jvp.cpp#buildRuleTangent` | `modules/transforms/jvp.cpp` |
| 5 | `mean` 负轴 | `value=7.0 grad=[[0.3333333333333333 ×3]×2]`；`axis -2`：`value=10.5 grad=[[0.5 ×3]×2]` | 同一程序两腿：`value_engine=7.00000000000000000 value_library=7.00000000000000000 squared_difference=0.00000000000000000e+00`，两个梯度逐字同 6 个元素；`rhs=dtod_t,sum_stated_t,mean_axis_stated_t`；`axis -2` 同 | **已修**，且不是 `lane-meanneg` 修的 | `stdlib/rulesets/jvp.cml#jvp_mean_axis` 把轴与 extents 一起交给 `mean_axis_stated`；`stdlib/rulesets/transpose.cml#mean_axis_cotangent` 只读 `at[2:]` 的 extents 与 `numel(t)/numel(ct)`，**轴不进任何索引**——落点 `3b4bcacff`（合并在 `6b94e6db4`） | 无；要钉住就改 `test/plans/feat/compile/transforms.plan.toml` |
| 6 | `f32` 出口 | `grad=4.0 with_grad=(4.0, 4.0)` | ``Invalid call to 'transpose' with argument types <> ((f32) => f32 pure): `transpose` takes a function that returns a `float`: the cotangent of the result is what goes in.`` | 仍是拒绝（原样） | `modules/transforms/transpose_operator.cpp#explainRejection`：`exitType()` 必须是 `Float64` | `modules/transforms/transpose_operator.cpp#transposeSignature` |
| 7 | `stop_gradient` | `grad=4.0 with_grad=(24.0, 4.0)` | `skip=rule uri=autodiff:stop_gradient reason=the operator has no forward rule bound at all` | 仍是缺失（两半都没有） | `stdlib/rulesets/jvp.cml` 无 `jvp_stop_gradient`、`stdlib/rulesets/transpose.cml` 无 `stop_gradient_t` | `stdlib/rulesets/jvp.cml` + `transpose.cml`；两条在未合并的 `lane-missinglaws#d599d9179` |
| 8 | `^` 的 `pow_t` | `^`：`caret_grad=12.0 caret_with_grad=(8.0, 12.0)`；`math.pow`：`autodiff: no derivative rule for operator 'math:pow_d'` | 前向过（`rules=jvp_pow`）→ `TRANSPOSE function=rulesets.jvp::pow_tangent<float> skip=condition`、`skip=call-no-transpose node=6 callee=rulesets.jvp::pow_tangent<float>` | 仍是缺失，但**只差后一半**：墙从“前向无律”移到“`pow_tangent` 没有转置” | `stdlib/rulesets/jvp.cml#jvp_pow`（`09042c4fe` 已落地）；`stdlib/rulesets/transpose.cml` 无 `pow_t` | `stdlib/rulesets/transpose.cml` |
| 9 | 库层 `grad` 的签名面 | `grad=3.0 with_grad=(10.0, 3.0)` | `No matching function found … Available overloads: vjp::grad: (f: (x: float) => float) => ((x: float) => float) pure` | 仍是缺失（原样） | `stdlib/vjp.cml#grad` | `stdlib/vjp.cml` |
| 10 | 库层 `with_grad` | `with_grad=(9.0, 3.0)` | `SemanticDiag::ImportNameNotExported: Reference 'with_grad' is declared as imported but is not exported by its module.` | 仍是缺失（原样） | `stdlib/vjp.cml` 无导出；`stdlib/signatures/autodiff.cml#with_grad` 是不参与编译的参考声明 | `stdlib/vjp.cml`（或合并 `lane-withgradexport#0b3303101`） |
| 11 | `has_aux` | `grad_aux=(4.0, 6.0) with_grad_aux=((4.0, 6.0), 4.0) counted=(4.0, 7)`，五个后端一致 | `vjp(loss, has_aux: true)` → `SemanticDiag::ParamArgumentUnknown: 'has_aux' is not a parameter of '(f: F) => …'`（按名拒绝）；把对写在写的地方就得到同一批数：`value=4.0 pullback=(4.0) aux=6.0` | **已修 / 不是缺陷** | 覆盖**不在** `autodiff.plan.toml`，而在 `test/plans/feat/modules/jax_alignment.plan.toml#derivatives.has_aux`（期望串 `jax_has_aux\n(4.0, 6.0)\n((4.0, 6.0), 4.0)\n(4.0, 7)\n`，五个后端 + 后端互比），另有 `#refused.has_aux_missing`、`#refused.has_aux_false` | 无；要形参那是 `lane-hasaux#17df40332` 那页的裁定 |

## 总数

**11 = 已修 2（第 5、11 项）+ 仍是错数 1（第 1 项）+ 仍是拒绝 4（第 2、3、4、6 项）+ 仍是缺失 4（第 7、8、9、10 项）。**
旧表是 1 / 3 / 7：**第 5 项真的修了**（修在 extents 那条线上，不是 `mean_neg` 那条），**第 11 项从来不是缺陷**，
**第 1 项从“走不到”变成“答错”**，**第 8 项修掉前一半、后一半移到 `pow_tangent` 上**，其余 7 项一字未动。
能逐位比数的只有第 5 项（`squared_difference=0.00000000000000000e+00`）与第 11 项（库层用两个投影到同样三个数）；
其余九项的库层腿给的是拒绝句子，**没有数可比**，这也是它们不能归进“错数”的原因。

## 两处闸门（同一棵树，主机 `88zjwei`，`-Tier fast -Jobs 4`，一次一个目标）

| 目标 | total | pass | fail | known | 时刻 | load 1m 前→后 |
|---|---|---|---|---|---|---|
| `test/plans/feat/compile`（目录） | 2243 | 2230 | 2 | 11 | 03:47:28 → 03:50:37 | 18.36 → 18.92 |
| `test/plans/feat/compile/transforms.plan.toml` | 105 | 98 | 2 | 5 | 03:50:51 → 03:51:00 | 18.72 → 18.31 |
| `test/plans/feat/modules/jax_alignment.plan.toml`（为第 11 项作证） | 34 | 34 | 0 | 0 | 03:51:26 → 03:52:51 | 17.74 → 18.16 |

两条红两次都是同两条：`transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`
（`transpose has no transpose for this function: the membe…` / `split does not build this function (this loop is
on the chain the exit reads…)`）——它们是基线，`lane-libgrad` 在 `131676081` 一带量到的也是这两条。本树
不改一行代码，所以这就是基线读数。

## 本轮的改动面

只新增本页。`git diff -w` 里没有第二样东西；`README` 索引那一半不存在——`docs/handover/reports/` 在任何
文件里都没有索引（拿最近一页的文件名全仓搜，零命中），所以没有可加的索引行。