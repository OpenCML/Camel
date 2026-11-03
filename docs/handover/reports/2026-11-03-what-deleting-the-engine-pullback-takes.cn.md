# 删引擎 `pullback.cpp` / `buildGradientGraph` 要满足什么（2026-11-03）

作者"下一批"第 7 条："对齐后删引擎 `pullback.cpp` 与 `buildGradientGraph`"。"对齐"是形容词，
这份把它变成可逐条打勾的表。**只读审计，未改代码**；所有数字取自当前树（`fdd46fdb6`）的静态清点。

## 1. 它今天还在被走吗：在，而且是 `grad` 的**唯一**路径

| 事实 | 位置 | 原文一行 |
|---|---|---|
| 唯一入口是两个算子 kernel | `modules/autodiff/operators.cpp#gradKernel`、`#valueAndGradKernel` | `const auto gradient = buildGradientGraph(ctx.shared_from_this(), function->program(), function->graph(), withValue, hasAux);` |
| 第二个调用点是引擎自己处理**嵌套 grad** | `modules/autodiff/pullback.cpp#ForwardRewrite::rewriteClosureCall` | `const GradientGraph gradient = buildGradientGraph(engine_.context(), …);` |
| 它返回什么、谁用 | `modules/autodiff/pullback.h#GradientGraph` | `struct GradientGraph { const Program *program; GCGraph *graph; }`，两个调用点都拿它做 `::Function::create(...)` 并把 `f` 的 closure 抄过去 |
| 没有回落到库层的路 | `stdlib/vjp.cml#vjp` | `let (y, lin) = split(jvp(f))(..xs)` —— 库层只用 `jvp`/`split`/`transpose`，从不问引擎 |
| 也没有从库层回落到引擎的路 | `modules/autodiff/pullback.cpp#buildGradientGraph` 的调用点全集 | 只有上面两处；`DerivativeRegistry::findRule` 的唯一调用点也在 `pullback.cpp` |

结论：**两套并行实现，谁都不回落谁**。`import { grad } from autodiff` 全形状走引擎；
`import { grad } from vjp` 只有 `(x: float) => float` 一条（`stdlib/vjp.cml#grad`，注释自己说
"读作 `vjp` 的一种读法，不是第二个 transform"）。所以"对齐"不是"补齐回落"，是**换掉入口实现**。

## 2. 库层已经能覆盖哪些算子：14 个规则头，对引擎 61 个名字

引擎侧清点：`modules/tensor/ops/vjp.cpp#setVjp` 的注册表（`modules/tensor/ops/registry.cpp#OpRegistry::add`
把它们挂进 `DerivativeRegistry`）+ `modules/math/module.cpp#MathModule::load` 的 `sqrt`/`erf`/`erfc`
+ `modules/autodiff/operators.cpp` 的 `autodiff:stop_gradient`：**61 个张量名字**（50 个真规则、
11 个 `noGradient`：`zeros_like`、`ones_like`、`numel`、`dim`、`shape`、`argmax`、`argmin`、
`argmax_axis`、`argmin_axis`、`one_hot`、`lt/le/gt/ge/strict_eq/strict_neq`），**外加 3 个数学**。

库侧清点：`stdlib/rulesets/transpose.cml` **14 个头**（`dtod_t`、`add_tensor_t`、`mul_tensor_t`、
`subtract_t`、`matmul_t`、`sum_t`、`idx_arr_t`、`take_t`、`mean_axis_t`，加标量写法
`add_t`/`sub_t`/`mul_t`/`div_t`/`neg_t`）；`stdlib/rulesets/jvp.cml` **23 个头**（多 `jvp_pow`、
`jvp_reshape`、`jvp_softmax` 与 8 条转换规则）。

| 算子 | 引擎规则 | 库 `transpose` | 库 `jvp` |
|---|---|---|---|
| `add`（数组） | ✓ | ✓ `add_tensor_t` | —（标量 `jvp_add` 走 `+`） |
| `multiply`（数组） | ✓ | ✓ `mul_tensor_t` | ✓ `jvp_mul_tensor` |
| `subtract` | ✓ | ✓ | ✓ |
| `matmul` | ✓ | ✓ | ✓ |
| `sum` | ✓ | ✓ | ✓ |
| `mean_axis` | ✓ | ✓ | ✓ |
| `take` / `idx_take` | ✓ | ✓ `take_t` / `idx_arr_t` | ✓ |
| 标量 `+ - * /`、一元负、`float()` | 核里另有 | ✓ | ✓ |
| `pow`（`^`） | ✓ | **✗**（实测 `skip=rule uri=math:pow_d reason=unbound`） | ✓ `jvp_pow` |
| `relu` | ✓ | **✓** `relu_t`（2026-11-03 补齐，见下） | **✓** `jvp_relu` |
| `exp` | ✓ | **✓** `exp_t`（2026-11-03 补齐，见下） | **✓** `jvp_exp` |
| `reshape` | ✓ | **✗** | ✓ |
| `softmax` | ✓ | **✗**（不是差 `softmax_t`：库层在 `split` 就停，走查未开始；见下） | ✓ |
| 其余 ~41 个张量算子 | ✓ | **✗** | **✗** |

**差集进度**：`relu` 已从"只能走引擎"移到"两侧都有"（引擎 `reluVjp` 与库层
`jvp_relu` + `relu_t` 在 `[-2.0, 0.0, 3.0, -0.5]` 上逐元素同答 `[0.0, 0.0, 1.0, 0.0]`；
用例 `test/cases/compile/transforms/relu_gradient_through_{the_engine,the_library}.cml`）。
`exp` 也已移过去（`lane-exp` `ab1e7113c`）：`jvp_exp` = `exp(x) * dx`、`exp_t` = `ct * exp(x)`，
在 `[1.0, 2.0]` 上两侧逐元素同答 `[2.718281828459045, 7.38905609893065]`。
`^`（`math:pow_d`）仍在"只有引擎"。

**下一个候选**：**`sigmoid` / `tanh`（最贴训练语料）**——一元，切线 `y*(1-y)`，两条律即可，
且它们的切线**只重算 `x` 上的值**，与 `exp` 同形，不会再碰 cut 那道墙；`softmax` 与 `log_softmax`
仍是"缺一次 cut 的裁定"（见下），不是缺律。

**两个被实测改口的结论**（2026-11-03 深夜）：

1. "`softmax` 只差一条律"错了（`lane-softmax` `f5dbca977`）：补 `softmax_t` 没有用，库层在**走查
   之前**就停——`REINTERP split function=jvp_of_total skip=backward construct=a node whose input was
   not copied uri=- why=an operand of the node is not reachable from the half's exit`，整份输出里
   没有一行 `TRANSPOSE`。引擎侧同一条损失答得出（`[[-0.17007706463866937, -0.37517307550291684,
   -0.5460605174868518, 1.0913106576284373], [0.10848803305563078, -0.046500634627606695,
   -0.22389029087827908, 0.16190289245025496]]`，与手算到 1e-16 一致）。
2. "cut 会拦一切重算"也错了（`lane-exp` `ab1e7113c`）：`exp` 的切线在**后半边重算 `exp(x)`**，
   而 cut 把它当残差带过去了——`REINTERP split … residuals=2`、
   `TRANSPOSE … rhs=sum_t,mul_tensor_t,exp_t`，库层因此过。所以 `relu`（掩码只依赖 `x`）与 `exp`
   都过，被拦的是**重算的调用带着出口不可达的参数**（`softmax<1>` 的轴、`sum<1,true>` 的
   `keepdims`、`vjp_loop` 里 `index_into` 的数组），这正是 `mean_axis` 那条 `skip=arity` 的另一面。
   收窄后的问题与两条路的代价写在
   `docs/handover/questions/2026-11-03-when-the-tangent-recomputes-a-value-what-does-split-carry.cn.md`。

**缺口（= "对齐"的度量）**：引擎 61 个张量名字里，库 `transpose` 覆盖 8–9 个，
**约 41 个只能走引擎**（`gelu`、`sigmoid`、`tanh`、`exp`、`log`、`sqrt`、`rsqrt`、`abs`、
`erf`、`erfc`、`maximum`、`minimum`、`where`、`log_softmax`、`layer_norm`、`mean`、`max`、`min`、
`sum_axis`、`max_axis`、`min_axis`、`sum_to`、`broadcast_like`、`expand_axis`、`cast_like`、
`permute`、`permute_inverse`、`transpose`、`unsqueeze`、`concat`、`shape`、`linear`…），
再加 3 个数学、1 个 `autodiff:stop_gradient`。（`relu` 与 `exp` 已补齐，故本行按补齐后的口径数。）

## 3. 删它需要哪几件事同时成立（每条可判定）

| # | 条件 | 今天怎么勾 |
|---|---|---|
| D1 | 引擎有真规则的每个张量算子，`stdlib/rulesets/transpose.cml` 里都有它的头 | 一个 diff 工具：`setVjp` 名字集合 − 规则头集合 = 空（今天差 ~42） |
| D2 | `grad`/`with_grad`/`_aux` 有**库层声明**：`Gradient<F>`、`ValueAndGradient<F>`、`…AndAux<F>` 成为类型函数。**已单独开页**：`docs/handover/questions/2026-11-03-can-gradient-of-a-function-be-a-library-type-function.cn.md`（默认 A：记为永久例外；要达成须先动那条否决） | **未达成，且要作者裁决**：`stdlib/signatures/autodiff.cml` 自己写着 "`grad` has no declaration in standard library types: its result type is computed here"，今天由 C++ `GradResolver`（`gradientSignature`）算；要写成 `Tangent<With 的第一个>`，正撞上作者 2026-11-01 对"一长元组取元素"的否决 |
| D3 | 每个算子的数字对齐：库层 `grad`/`vjp` 与今天引擎读数 `max_abs_diff = 0` | 语料可数：`test/cases` 里 **239 个** case `import {…} from autodiff`、**125 个**提到 `with_grad`；对齐后这些数一个不动 |
| D4 | 引擎今天**拒**的，库层按名拒、词不合并 | 非标量结果、无 `with`、首参 `var`（`differentiationRefusal`）、未知被调（`call-no-transpose`）、loop（`skip=state`）逐个有 case |
| D5 | **嵌套 `grad`**（被微分的体里再调 `grad`）有库层答案 | 今天是引擎在 `ForwardRewrite::rewriteClosureCall` 里就地建图；作者的句子没提这条，但删了就没人答 |
| D6 | 收尾一起死的东西点名 | `DerivativeRegistry::setRule` 三处（`OpRegistry::add`、`MathModule::load`、`operators.cpp`）+ `VjpRule`/`findRule`（唯一调用点 `pullback.cpp`）+ `modules/tensor/ops/vjp.cpp` 整文件；`pullback.h` 只有 `operators.cpp` 与 `pullback.cpp` 两个包含者 |

D2 是硬前置：**类型算不出来，库层 `grad` 就写不出来**，于是 `GradResolver` 得留，
`pullback.cpp` 再删也只删掉"建图"那半边，`grad` 仍是 C++ 算子——这不是作者第 7 条想要的结果。
D1 才是主体工作量（~42 个算子，一个一个写 `*_t`）。

## 4. 最小可验证的前置（今天就能勾，已钉成用例）

**`^`（两个 float 的乘方）**，两条相邻用例：

- 引擎回答：`test/cases/compile/transforms/pow_gradient_through_the_engine.cml` —— `grad` 给出
  `d(w^2)/dw = 6.0`（`w = 3`），退出 0；
- 库层按名拒：`test/cases/compile/transforms/pow_gradient_refused_by_the_library.cml` ——
  `REINTERP jvp function=p … rules=jvp_pow`（前半边跑通）之后
  `TRANSPOSE function=rulesets.jvp::pow_tangent<float> skip=rule uri=math:pow_d reason=unbound`，
  再 `skip=call-no-transpose … callee=rulesets.jvp::pow_tangent<float>`，程序句是
  "this call's callee has no transpose of its own"。

实测要点：库层拒的 key 是 **`math:pow_d`**（`^` 落进图的标量拼写），不是 `tensor:pow`；`unbound`
是规则表自己的词（这个族没有这个算子的规则）。两条读数不能同属一个程序——拒绝发生在建图处，
程序不会跑到打印。

**`relu` 是同一形状、更贴训练语料的锚**（静态清点，未跑）：引擎有
`setVjp(defs, "relu", &reluVjp)`；库层无 `jvp_relu`、无 `relu_t`，所以 `vjp` 会在**前半边**（lift）
就按名拒，而不像 `^` 那样走到转置才拒。要把它也钉成读数得再加一个用例，本轮只报不改。

## 5. 一句话

"对齐" = **D1（~42 个算子的库规则）+ D2（`Gradient<F>` 变成能写的类型，要作者裁决）**；
D3–D6 是删之后的验收。今天能立刻验证的只有第 4 节的 `pow`/`relu` 反例。