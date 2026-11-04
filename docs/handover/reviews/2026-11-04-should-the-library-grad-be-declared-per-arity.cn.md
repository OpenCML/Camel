# 库层 `grad` 要不要按 arity 铺开一族声明，还是等一个"pack 的第一个成员"类型函数？

状态：待裁。**本轮不动**（默认建议），只把读数记下来。发现时辰 2026-11-04 02:56，树
`D:\Projects\Camel-wt\withgradexport`，基线 `db741501d`。

## 一、让这是问句的读数

两边的声明逐字（引擎那份在 `modules/autodiff/operators.cpp#GradResolver::signature`，库那份在
`stdlib/vjp.cml#grad`）：

```
引擎： (f: <W...>(N...) => float)                       => <W...>(N...) => gradient of W0
库层： (f: (x: float) => float) => ((x: float) => float) pure
```

引擎那份收**任意** arity 的 `with` 列表与任意个普通形参（`grad_first_with_parameter.cml` 的四组里，
`scaled` 是 2 个 `with`、0 个普通形参，`weighted` 是 1 个 `with`、1 个普通形参，`dot` 的 `with` 是张量，
`point_loss` 的 `with` 是结构体）；库层那份只收**一个 float 普通形参、没有 `with` 列表**的函数。

库层那份在四个分组上的实测（每条形一个程序，因为类型拒绝会让整程序不编译）：

```
grad(weighted)  -> NoMatchingFunction: no matching function for <x: float> (y: float) => float pure
grad(scaled)    -> NoMatchingFunction: no matching function for <k: float, x: float> () => float pure
grad(dot)       -> NoMatchingFunction: no matching function for <w: float[..]> (x: float[..]) => float pure
grad((x: float) => x * x)(3.0) -> 6.0        <- 唯一走得通的那种形状
```

四条 `NoMatchingFunction` 都把**唯一可用的那条重载**原样印出来，所以是明确拒绝、不是错数：
`vjp::grad: (f: (x: float) => float) => ((x: float) => float) pure`。

**为什么窄。** 窄的不是函数体：`grad` 的体读 `cotangents.0`，对任意 arity 都对。窄的是**声明**——
`grad(f)` 答的是第一个 cotangent，它的类型是 `..ParamsOf<F>` 的第一个成员，而
`stdlib/types.cml` 写了 `ParamsOf<F>`、`WithOf<F>`、`ResultOf<F>`、`Tangent<T>`，**没有**任何类型函数
能命名"pack 的第一个成员"。同一块缺失也让 `ValueAndGradient<F>`（引擎 `with_grad` 的答型）写不出来，
见同目录 `2026-11-04-should-the-library-with-grad-be-an-export-that-refuses.cn.md`。

## 二、三条路

- **甲（默认建议）：不动。** 库层 `grad` 继续只收一种形状；权重走引擎的 `grad`（`autodiff` 模块），
  数据走 `vjp`。代价：判据"库层 grader 覆盖权重"这句仍然不成立；好处：零风险，且没有一族声明要维护。
- **乙：按 arity 铺开一族声明**（`(x: float) => float`、`(x: float, y: float) => float`、`() => float`…）。
  代价：每个 arity 一条声明、一条文档、一条用例；仍然覆盖不了 `dot`（张量形参）和 `point_loss`
  （结构体 `with`，且根本不在普通形参上），而这正是四组里两组的形状；族随形状增长。
  这一条与 `stdlib/vjp.cml` 的注释里既有的说法（`vjp`"每 arity 声明一次"）同源，所以它不是新做法。
- **丙：等一个 `FirstOf<..>`（或 `AtIndexOf<A, 0>`）类型函数。** 一条声明覆盖全部形状，`ValueAndGradient<F>`
  也一起解锁。代价：那是类型层的能力，不在本车道能改的文件里；而且要作者先答一个设计问题——
  空 pack 的第一个成员有没有类型（`() => float` 那组恰好是空 `ParamsOf`）。

## 三、代价与哨兵

- 本轮**没有**改 `stdlib/vjp.cml#grad` 一个字符；`feat/compile/transforms` 与
  `feat/transforms/split` 的读数不受影响。
- 若将来走乙，哨兵是四组各一条 `expect_exit = 1` 的拒绝用例（今天的四条 `NoMatchingFunction` 原文即可钉）。
- 若将来走丙，哨兵是同一组用例**翻成 `expect_exit = 0` 且钉数**，与 `grad_first_with_parameter.cml`
  的四行逐字相同。