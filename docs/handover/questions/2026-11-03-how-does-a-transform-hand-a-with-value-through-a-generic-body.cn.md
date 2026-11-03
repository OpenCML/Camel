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

## 三之二、两次探针把"还差什么"量清楚了

**已经能用的那一半（不需要新概念）：** 位置的 `with` 参数带名字时，lambda 的体**已经可以写这个名字**——

```cml
func apply(g: <w: float> (x: float) => float): float { return g<2.0>(3.0) }
println(apply((x) => x * w))     // 6.0 = 3.0 * 2.0
```

`completeLambdaSignature` 采纳位置的 `with` 列表时是**连名字一起**采纳的（`addWithArg(name, type, …)`），所以单态位置上的 lambda 体拿得到 `w`。这条今天就能用，不需要任何新东西。

**还差的那一半：** 泛型体里这个办法不成立，两条路都断在同一处——

1. **lambda 写不出 `with` 列表。** `<..ws> (x: float) => …` 在解析阶段就被拒：
   ```
   error[SyntaxDiag::UnexpectedToken]: Unexpected '..'; expected a name.
     --> 3:14   let f = <..ws> (x: float): float => x * ws.[0]
   ```
   只有 `func` 声明能写 `with`。所以 lambda **无法自己给这个包起名**。
2. **类型函数拼接不带名字。** `Vjp<F>` 展开后的签名打印出来是 `<float> (float) => (float, …)`——`with` 槽位**没有名字**（`..WithOf<F>` 拼的是类型），所以泛型库里也借不到名字。

两条路合起来的意思是：**给继承来的 `with` 列表一个名字，今天没有任何语法或类型层面的写法**。最小的机制是让 lambda 也能写 `with <..ws>`（`with` 列表进 lambda 的语法），那就是**新增语法**——语法是作者的，不是我的。

所以这一页要问的是：**lambda 能不能写 `with <..ws>`？** 若可以，剩下的都是今晚已经打通的东西（包里存值、`<..ws>` 转发、`..ws` 展开成 `with` 实参），`vjp.cml` 只需把 `(..xs) => …` 写成 `<..ws> (..xs) => …` 并把那一行改成 `split(jvp(f))<..ws>(..xs)`。若不可以，就得另定一条"继承列表叫什么"的规则，同样要作者开口。

## 四、这一页不是什么

不是"给 `vjp.cml` 的调用形补一个占位实参"。那会把"传不下去"变成"假装传了"，属于今晚点名的同一类错误：留一个不生效的东西让闸门变绿。若实现上面的默认时发现**机制上必须新增核心概念**（例如 `with` 值需要一条新的传递通道，而不是复用已有的包），**停手，只留这一页**。

**已经停手了一次，原因就是上面第三节之二**：给继承列表起名需要"lambda 能写 `with`"这条语法，它是新增语法，不属于我可以顺手加的改动。`stdlib/vjp.cml` 一个字没改，也没有任何占位实参。

## 五、读数出处

- `Camel Invariant Violation: FunctionType::normTypeAt … index < normCount_`（改前，abort，exit 134）→ 同一程序的 `rest_hole 7.0 / named_hole 30.0 / two_with 6.0`（改后，提交 `a7430950d`）；
- 上面的 `ArgumentsMismatch` 原文是在提交 `a7430950d` 的树上跑的，探针 `vjp(f)<2.0>(3.0)`、`f` 带一个 `with` 参数；
- 库侧与引擎侧在标量模型上逐位相同（w = -0.75, b = -0.5, loss = 0.125，差 0.000e+00），用例 `test/cases/compile/transforms/library_grad_is_the_composition.cml`。