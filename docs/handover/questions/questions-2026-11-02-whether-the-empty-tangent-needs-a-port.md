# 待裁：`()` 在端口层面怎么表示——沿用"没有槽"，还是要一个真正的空元组端口？

> **一句问句**：一棵提升过的图里，一个**没有切空间**的形参（`int`、`bool`、`string`）在端口上是"少一个端口"，还是"有一个类型为 `()` 的端口"？
>
> **默认建议：沿用"没有槽"**，与 M3 已落地的 `Tangent<bool> = ()` 同一条规则。若这条规则在走图层沿用不了，就**保持在签名层拒绝**，并在页里写明"端口层面 `()` 还没解决"——**两条都可以，不许发明一种新的端口**。

## 今天堵着谁

M4 的数组三步。第 3 步（签名层）已经把 `jvpSignature` / `transposeSignature` 改成问 `tangentSpaceOf`：

```
jvpSignature        (float[], float[]) -> (float, (float[], float[]))
transposeSignature  ((), float[])      -> (knowns, float[])
```

`float[]` 的切空间是它自己，所以它有端口；但一个参数**没有**切空间时，签名层今天的选择是**直接拒绝**：

```
jvpSignature( (xs: float[], n: int) => float )  ->  nullptr        // 没有切空间的那个形参
```

理由写在代码里：把 `()` 当端口塞进运行时图，需要一种空元组端口，而签名层不该发明它。

## 已有的两条读数

1. **M3 的做法是"没有槽"，不是"空端口"**：`Tangent<bool> = ()` 落地时，`contribution=0` 那条读数就是它——一个不产生余切的槽，不是一个装着零的端口。
2. **走图层已经在用同一条规则**（M4 第 4 步这一笔）：提升一条数组函数时，`xs[i]` 的 `i` 没有切空间，规则拿到的是"有切空间的那几个操作数的切向，按它们本来的顺序"，`i` **不占一个槽**：

```
REINTERP jvp function=f graph=64 liftedFrom=55 type=(float[], float[]) => (float, float) pure nodes=6 kinds=6/3 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=jvp_idx_arr,jvp_mul
```

也就是：**走图层已经答了"没有槽"**，签名层还答"拒绝"。两处答案不一致，这就是这一页要一个裁定把它合起来的地方。

## 三个选项（都不发明新端口）

- **甲（默认建议）**：签名层也照走图层——没有切空间的形参**不给切向端口**，函数照常提升。`(xs: float[], n: int) => float` 的提升签名是 `(float[], float[])`（`n` 只有一个值的端口），规则拿到的切向数与"有切空间的形参"对齐。
- **乙**：签名层保持拒绝，直到有谁来定"空元组端口"。代价是 `f(xs, n)` 这类函数今天不能求导，而走图层其实已经能走。
- **丙**：造一个真正的 `()` 端口。**这一条违反"不许发明新端口"**，列在这里只是为了把它排除。

## 需要作者答的一句话

选甲还是乙。选甲，我就把签名层与走图层对齐（两边都是"没有槽"）；选乙，我就把走图层那条"没有槽"的分支改成"整条拒绝"，并在这页记下差距。
## 2026-11-02 补一句：这一页的结论可能随复核改变

作者 17:30 把"变换挪进前端实例化"这一线**暂停**了（`c8bc3d87f`、`c6b7104ae`、`split_instantiate.cpp`、`transpose` 同类，以及把"实参必须编译期已知"的拒绝推广到更多入口；**已进树的不撤**），理由是 `#macro` + `std::macro` 本身就是"动态构造静态图"的机制，9.1 写明宏阶段做还是运行期做是**实现的选择**。

**17:50 又叠了一层**：负责人否决了"前端变换表 + 调用处拒绝"整套（`fsh-dev` 的 `a5377eebe`），`compile_time.*`、`jvp_instantiate.cpp`、`split_instantiate.cpp`、`staticFunctionArg`、`lifted-at=`/`halves-at=` 正在被撤。所以今天有三件事要分清，别拧成一件：`staticFunctionArg`（被撤）与"这个类型没有切空间"（仍在，见下）是两件事；"实例化那一线被否决"也不等于"这一页的结论已被裁"。**这一页的两种可能都还开着**：签名层与走图层要不要对齐、对齐之后 `()` 走哪条路，都等复核。

**还要分清的一件（别让下一个人读错）**：签名层今天仍然会拒绝一个**没有切空间**的类型（`int`、`string`）——那是"这个类型不能求导"，与**被撤的**那套"实参必须编译期已知"（`staticFunctionArg`、调用处拒绝）**不是一件事**。签名层并没有放行一切：放行的只有"有切空间的类型，切向按 `tangentSpaceOf` 给"。

## 一条今天量出来的边界：库面声明的形状由算子注册决定，不由调用处决定

想给 `:tangent/scatter_add_arr` 一个库面名字 `scatter_add::<T>(xs: T[], i: int, delta: T): T[]` 时，**两处同时红**：

```
调用处  error[SemanticDiag::ImportNameNotExported]: Reference 'scatter_add' is declared as imported but is not exported by its module.
registry [FAIL] registry.signatures_match / registry.result_types_match / registry.parameter_types_match / registry.docs_complete
```

根在这一句（算子自己写的）：`src/builtin/operators/derivative.cpp:386` ——
`Derivative of '{}' expects 3 or 4 inputs (base, index, delta, axis)`：**这个算子取三个或四个操作数，`axis` 可选**，三参声明与注册形状对不上。

**所以"想怎么用"不能决定声明怎么写**：声明必须与注册的真实形状一致，四红就是那张注册表在说"你写错了"。同一件事的另一个读数：**规则右侧能名的只有函数**（`expandRuleRightSide` 的 `Func` 分支），所以在规则集里写 `:tangent/scatter_add_arr` 也得先有一个能名的函数——"不新造库名"并不等于"不用声明"，只是把声明挪进规则集、不放进公共库面。

## 规则集能做什么、不能做什么（三条今天量出来的）

| 想做的事 | 能不能 | 证据 |
|---|---|---|
| 规则右侧直接写一个算子（`=> (ct) => :tangent/scatter_add_arr(...)`） | **不能** | 展开只走 `Func` 分支（按 `calleeIdOf` 取被调图），`Oper` 分支只处理**函数体里已有**的算子节点：规则右侧只能名一个函数 |
| 规则集里写一条**不 `export`** 的函数，用 `#op(...)` 绑内建算子 | **能**，且**不复现** `registry` 四红 | 四红只在把公共名加进 `stdlib/signatures/autodiff.cml` 时出现；同一条声明搬进 `stdlib/rulesets/transpose.cml` 且不 `export`，`transforms 46/46` 绿 |
| `#op` 的写法 | `#op('tangent:scatter_add_arr')` **不认**；`#op(':tangent/scatter_add_arr')`（**带前导冒号**）才认 | `NativeDeclarationNotLoaded: The declaration 'scatter_into' names the operator 'tangent:scatter_add_arr', which no loaded module implements` |

第一条与 `derivative.cpp:386` 的"3 或 4 个操作数（`base, index, delta, axis`，`axis` 可选）"是并列的两条：**一个说右侧只能名函数，一个说声明要按注册的真实形状写**。两个都是"注册表说了算"，不是调用处想怎么写就怎么写。

## 三条红的归属（`transposeSignature` 那一支）

放开 `transposeSignature` 的多参数分支后红了三条：`transpose_many_parameters`、`transpose_many_parameters.is_linear_in_the_cotangent`、`transpose_unread_parameters`。**逐条查过它们的签名，形参全是 `float`**：

```
transpose_many_parameters   func mt(x: float, w: float, dx: float, dw: float): float
transpose_unread_parameters func at(x: float, w: float, dx: float, dw: float): float
```

**每个形参都有切空间**，所以这三条红**不是"期望该改"，是我那次改动自己删了一个种子参数**（把返回签名少了一个位置）。按"全部参数都有切空间 → 红说明改动错了"这条判据，这一支到此为止：**要重做的话，正确改法是保留原有的种子参数、只放开循环里的类型判断**，而不是改期望。

（`transpose_unread_parameters` 尤其说明既有不变量：**不被读的参数仍然要有一份余切**——这与"没有切空间的形参不产出余切"是两件事，别混。）
## 三个 vjp 用例的对照表（引用读数必须带用例名）

| 用例 | 程序 | 期望数 | 今天状态 |
|---|---|---|---|
| `vjp_api.analytic_gradient`（`test/plans/feat/compile/transforms.plan.toml`，`vjp_analytic_gradient.cml`） | `f(x,y) = x*x*y` 在 `(3,2)` | `18.0 / 12.0 / 9.0`，四后端 `nvm/fvm/jit/prl` 一致（`backends_agree = ["exit_code", "output_normalized"]`） | **绿** |
| `vjp_api.gradient_when_the_steps_land`（同计划，`vjp_gradient.cml`） | `f(x) = x*x*x` 在 `x=3` | `vjp(f)(3.0)` 读 `(27.0, 27.0)`、`grad(f)(3.0)` 读 `27.0`；计划里 `backends = ["nvm"]`，`expect_stdout_contains = ["27.0"]`（**只有 nvm 一遍**，没有 `skip_unless`） | **绿** |
| `probe_library_vjp`（`test/tools/have-library-vjp.mjs`） | 同 `f(x,y)=x*x*y` | **无期望数**，只证"装得起来" | **绿** |

**为什么列这张表**：今天有人（我）引用过两组没有用例名的数（`4.0 / 4.0`、`-6.0 / 3.0`，来自 `stdlib/vjp.cml` 落笔时的对照），它们**查不到出处**，所以不算证据。**引用任何读数必须带用例名或计划名；没带名字的数就是假数**——它看起来像证据，但和"跨空间比较"是同一类错。

**关于 `mt`/`at` 的归属，原样记一句**：`transpose_many_parameters` 与 `transpose_unread_parameters` 的形参全是 `float`、都有切空间，所以那三条红**错在我的改动（删了一个种子参数），不是期望该改**；`unread_parameters` 钉住的是"**不被读的参数仍要有一份余切**"，它与"**没有切空间的形参不产出余切**"是两件事。