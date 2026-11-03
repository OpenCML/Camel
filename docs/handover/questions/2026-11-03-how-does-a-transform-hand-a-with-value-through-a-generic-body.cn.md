# 变换在泛型库体里怎么把 `with` 值传下去：lambda 采纳了位置的 `with` 列表，体里却没有这些值的名字

## 一、问题

作者 2026-11-03 09:50 的裁定说，`Vjp<F>` 与 `Jvp<F>` 同形地带上 `WithOf<F>`（`<..WithOf<F>> (..ParamsOf<F>) => ...`），并且**变换只对普通参数求导，`with` 值原样传下去**。

前半句今晚已经能建起来了：`vjp` 里那个 `(..xs) => …` 的 lambda 站在 `Vjp<F>` 的位置上，补全后得到 `<float> (float) => (float, <float> (float) => float)` —— `with` 前缀在（`test/cases/compile/lambda_inference/with_list_from_the_position.cml` 钉住的是同一件事的普通写法）。

**后半句没有路。** lambda 采纳了位置的 `with` 列表，但它的体里**没有这些值的名字**：lambda 没写 `with`，体里就写不出任何指向它们的表达式。于是 `stdlib/vjp.cml` 自己的那一行少给一个实参：

```
error[SemanticDiag::ArgumentsMismatch]: Function of type
'<float> (float) => (float, <float> (float) => float pure) pure' cannot be called with argument types: <> (float).
   --> stdlib/vjp.cml:73:37
    |
 73 |         let (y, lin) = split(jvp(f))(..xs)
    |                                     ^^^^^^
note: in the instance 'vjp<<w: float> (x: float) => float pure>' of the generic function 'vjp'
      with F = <w: float> (x: float) => float pure, instantiated by the call
```

`split(jvp(f))` 自己也是一个带 `with` 前缀的函数（`jvp` 抬升结果带上 `f` 的 `with` 列表），所以调用它必须给一个 `with` 实参；lambda 有那个端口，却没有名字可以用，**给不出来**。

## 二、这条墙此前没人撞到的原因

`origin/develop` 上没有任何用例对带 `with` 参数的函数调用 `vjp`：

- `first_step_library.cml` 的库腿写的是 `loss_plain(w, b)`（全普通参数），引擎腿才用 `with_grad(loss_with)`；
- 其余 `vjp(...)` 的用例，被变换的函数都没有 `with` 参数。

也就是说，"声明与图一致"在 develop 上只是**没有用例走过**，不是"走过且一致"。本条是第一个走到这里的程序。

## 三、该怎么定（默认，已按此实现）

**继承来的 `with` 列表在 lambda 体里可以被命名并转发。**

理由一句话：09:50 说"`with` 值原样传下去"，那么"能传"是这句话的**必要条件**；不能命名就等于传不下去。

机制上不需要新概念：今晚落地的包已经能做这件事 ——

```cml
with <..ws>
func forward(x: float): float {
    return add3<..ws>(x, 1.0, 2.0)     // 转发一个包给另一个函数的包
}
```

于是 lambda 的体应该拿得到一个包名（它就是它从位置采纳来的那个 `with` 列表），`split(jvp(f))<..that>(..xs)` 这种写法就成立。

## 四、这一页不是什么

不是"给 `vjp.cml` 的调用形补一个占位实参"。那会把"传不下去"变成"假装传了"，属于今晚点名的同一类错误：留一个不生效的东西让闸门变绿。若实现上面的默认时发现**机制上必须新增核心概念**（例如 `with` 值需要一条新的传递通道，而不是复用已有的包），**停手，只留这一页**。

## 五、读数出处

- `Camel Invariant Violation: FunctionType::normTypeAt … index < normCount_`（改前，abort，exit 134）→ 同一程序的 `rest_hole 7.0 / named_hole 30.0 / two_with 6.0`（改后，提交 `a7430950d`）；
- 上面的 `ArgumentsMismatch` 原文是在提交 `a7430950d` 的树上跑的，探针 `vjp(f)<2.0>(3.0)`、`f` 带一个 `with` 参数；
- 库侧与引擎侧在标量模型上逐位相同（w = -0.75, b = -0.5, loss = 0.125，差 0.000e+00），用例 `test/cases/compile/transforms/library_grad_is_the_composition.cml`。