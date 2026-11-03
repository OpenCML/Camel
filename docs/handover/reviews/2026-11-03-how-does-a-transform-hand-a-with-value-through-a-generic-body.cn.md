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

两条路合起来的意思是：名字不是丢在采纳处，是丢在**拼接处**——`Vjp<F>` 里 `..WithOf<F>` 只拼类型，不拼名字。于是有两条可选的路，代价不同：

### 选项一：lambda 也能写 `with <..ws>`（新增语法，归作者）

`(..xs) => …` 写成 `<..ws> (..xs) => …`，体里就有名字了，`vjp.cml` 只需把那一行改成 `split(jvp(f))<..ws>(..xs)`。

- 代价：语法、GCT、补全三处都要动；而且要定"lambda 写的 `with` 包和位置上的 `with` 列表是什么关系"——lambda 是一个**值**，它的签名不能留着"未知个数的包"，所以补全时这个包必须被**绑定到位置的那份具体列表**（元素类型和值都来自位置，包名 `ws` 来自 lambda）。
- 好处：**库体作者自己起名，不依赖调用方怎么写**。这一点是决定性的，见下面对照。

### 选项三：让拼接保留名字

`..WithOf<F>` 拼接时把 `F` 的 `with` 形参名字一起带过来，`Vjp<F>` 展开成 `<w: float> (…) => …` 而不是 `<float> (…) => …`，泛型体里就能直接写 `w`——**探针一已证"名字在就能用"**。

- 代价 ①：名字来自 `F` 的声明，而 `F` 是类型变量。拼出的签名于是依赖**实参函数怎么命名**：调用方写 `<w2: float> (x: float) => float`，库里那句 `w` 就 `UnresolvedReference`。对"库作者写一次、所有人读"的泛型体，这是**硬伤**，不是风格问题。名字本身不参与类型同一性（`FunctionType` 只把它当标注），所以它不违反"类型不看名字"，但它确实让**可写性**依赖命名——这条要作者判。
- 代价 ②：影响面用 `git grep` 数出来——`WithOf<` 全树 **19 个文件**有引用；真正"把 `with` 列表拼进函数类型"的代码站点今天只有 **3 处**：`stdlib/types.cml` 的 `Vjp<F>`、`Jvp<F>`（后者已在 `3a4e97200` 上），以及 `ParamsOf<F>` 之类的同族（`Transpose<F>` **没有**拼 `with`，`Pullback<F>`、`Tangent<F>` 也没有）。另有 2 处只在测试/文档里：`test/cases/compile/type_packs/function_pack_params.cml` 的 `Weighted<F>`、`docs/diagnostics.md` 的 `Mix<F>`。所以**改动面很小**，3 个代码站点。
- 代价 ③：与选项一的关系。选项一解决"**写得出来**"，选项三解决"**叫得出名字**"。两者**不能互相替代**：只做选项三，库体要写调用方的命名；只做选项一，单态位置上的名字仍然留着（探针一那一半今天就好用），库里也有名字。**我的判断是两者都要，但顺序上先做选项一**，因为它是让 `vjp.cml` 能写出来的那一条；选项三之后可以作为"拼接不留无名槽位"的独立改进。

### 建议（标出来）

**推荐选项一**，一句理由：库体必须写得与调用方的命名无关，而选项三给的名字正好依赖调用方怎么命名；选项一只多一条语法，却把"名字由谁起"这件事放回写库的人手里。选项三仍然值得做，但它是显示与单态位置上的改进，不是这条墙的钥匙。

## 三点五、机制通了之后要验的数（下一轮的验收清单）

| # | 程序 | 期望 |
| --- | --- | --- |
| 1 | `vjp(f)<2.0>(3.0)`，`f(x) = w*x` | value **6.0**、tangent（`split` 的 `lin`）**2.0** |
| 2 | `f(x) = w*x + v*x*x`，`<2.0, 3.0>` | value **9.75**、tangent **11.0** |
| 3 | 同上但交换成 `<3.0, 2.0>` | value **9.0**、tangent **9.0**（与第 2 行不同 ⇒ 两个 `with` **没有**各自落对位置时会被这条抓住） |
| 4 | `apply((x) => x * w)`，`g<2.0>(3.0)` | **6.0**（今天已经是这个数，回归用） |

第 2、3 行就是"两个 `with` 必须能区分"那条更强的证明：`vjp` 若取错其中一个，答案会是 9.75/11.0 之外的一个数。

## 四、这一页不是什么

不是"给 `vjp.cml` 的调用形补一个占位实参"。那会把"传不下去"变成"假装传了"，属于今晚点名的同一类错误：留一个不生效的东西让闸门变绿。若实现上面的默认时发现**机制上必须新增核心概念**（例如 `with` 值需要一条新的传递通道，而不是复用已有的包），**停手，只留这一页**。

**已经停手了一次，原因就是上面第三节之二的拼接**：给继承列表起名要么新增"lambda 的 `with`"语法（选项一），要么让类型函数拼接带名字（选项三），两者都归作者。`stdlib/vjp.cml` 一个字没改，也没有任何占位实参。

## 五、读数出处

- `Camel Invariant Violation: FunctionType::normTypeAt … index < normCount_`（改前，abort，exit 134）→ 同一程序的 `rest_hole 7.0 / named_hole 30.0 / two_with 6.0`（改后，提交 `a7430950d`）；
- 上面的 `ArgumentsMismatch` 原文是在提交 `a7430950d` 的树上跑的，探针 `vjp(f)<2.0>(3.0)`、`f` 带一个 `with` 参数；
- 库侧与引擎侧在标量模型上逐位相同（w = -0.75, b = -0.5, loss = 0.125，差 0.000e+00），用例 `test/cases/compile/transforms/library_grad_is_the_composition.cml`。