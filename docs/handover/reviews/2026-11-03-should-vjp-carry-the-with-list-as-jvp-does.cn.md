# `Vjp<F>` 该不该和 `Jvp<F>` 同形地带 `WithOf<F>`？

**问句**：`Vjp<F>`（`stdlib/types.cml`）今天只写 `ParamsOf<F>`，而 `Jvp<F>` 的声明与实现都已经带上 `WithOf<F>` 那一半；这两者应当同形吗——即 `Vjp<F>` 是否应当声明成
`<..WithOf<F>> (..ParamsOf<F>) => (ResultOf<F>, Pullback<F>)`？

## 让它是问句的读数

1. `Jvp<F>` 侧已经成立。`jvpSignature`（`modules/transforms/jvp.cpp`）现在把 `f` 的 `with` 列表原样写进抬升后的类型，`split`/`transpose` 两半也都声明同一个列表。实测（`with <m: float> func block(x) = m * x`）：
   - `jvp(block)<2.0>(3.0, 1.0)` = `(6.0, 2.0)`
   - `split(jvp(block))<2.0>(3.0)` → `y = 6.0`，`lin<2.0>(1.0) = 2.0`
   - `transpose(lin)<2.0>(1.0)` = `(2.0)` —— 手算 `d/dx (m x) = m = 2`
   - `g(x) = m x^2` 同一条链给 `(18.0, (12.0))`，手算 `2 m x = 12`
2. 只有 `vjp` 的组合层卡住，卡在一次调用上（`stdlib/vjp.cml:73:37`）：
   「Function of type `<float> (float) => (float, <float> (float) => float pure) pure` cannot be called with argument types: `<> (float)`」——
   前一半声明了一个权重端口，而 `vjp` 返回的那个 lambda 没有 `with` 列表可以填它。
3. 为什么 lambda 填不了：**带体的函数不能声明 `with` 参数包**。实测
   `let g = <..ws: float> (x: float): float => x` 得到
   `error[SyntaxDiag::UnexpectedToken]: Unexpected '..'; expected a name`（`test/cases/.../with_pack_with_body.cml` 正是这条规矩的用例）。所以把 `Vjp<F>` 改成带 `WithOf<F>`，`vjp` 的函数体立刻无法写出同类型的返回值——声明改了、实现写不出来。

## 三条路，各带代价

- **A 先改声明，再补语言**（默认建议）：`Vjp<F>` 写成带 `WithOf<F>`，同时让"带体的函数可以声明 `with` 参数包"。代价：要动语法与实例化两处，且要决定包在体内的名字（`..ws` 如何被读、如何转交给内层调用）。
- **B 声明不动，`vjp` 换写法**：不让 `vjp` 返回 lambda，而是让 `vjp` 本身成为一个 `with` 包声明的算子（像 `format` 那样只有声明、无体）。代价：反变换的实现从库挪进核心算子，与"变换是库"（9.9 第一条）相反。
- **C 声明与实现都不动**：`vjp` 只接受无 `with` 的函数，模型上的梯度继续只走 `autodiff` 模块那条已经在跑的路（`grad_first_with_parameter` 两组用例今天都绿）。代价：`vjp = split(reinterp<jvp>(f))` 这条构造对权重永远不成立，9.9 的第 670 条只能由另一条实现兑现。

## 默认建议（写死）

**按 A 走**：`Vjp<F>` 应当与 `Jvp<F>` 同形地带 `WithOf<F>`。理由两条——`Jvp<F>` 已经这么做，两边不对称会持续产生上面第 2 条那句错误；而且 `split`、`transpose` 两半的端口已经就位，缺的只是这一处声明与它所需要的语法。落地顺序：先补"带体的 `with` 参数包"，再改 `Vjp<F>`，最后让 `vjp` 的 lambda 把包转发给 `split(jvp(f))`。

## 顺带一条读数（不是问句）

权重本身**不会是**这条链给出的梯度：前向把 `with` 值当常量、切向量为零（作者 A2「`with` 参数照闭包规则处理」），所以 `transpose(lin)` 只答数据（`x`）的余切，答案元里没有权重的位置。`vjp(block)<m>(x)` 即便打通，给出的也是 `d/dx`，不是 `d/dm`。
## 待办（2026-11-03 18:20 记）

**用户面今天有两个 `grad` 并存**：`stdlib/vjp.cml#grad`（只收 `(x: float) => float`，实现是 `vjp(f)` 读种子 1 取 `.0`）与 `stdlib/signatures/autodiff.cml#grad`、`:66#with_grad`（实现在 `modules/autodiff/operators.cpp#gradFunction`，C++ 直接 pullback，对第一个 `with` 参数求导）。

作者 2026-11-03 10:00 的第 3 条要的是：库层那个写成**一次组合**——`<w, ..rest> (..xs) => …`，体里对 `(w) => f<w, ..rest>(..xs)` 做 `vjp`；`stdlib/vjp.cml` 里只收单个 `float` 的那个删掉，用户面只剩一个。这条排在增补 1（带体的 `with` 参数包，`src/compile/gct/builder.cpp#Builder::visitFuncData` 的守卫与体里的绑定）之后——今天拿掉守卫会得到 `camel: internal error: Unknown GCT NodeType`。**在那之前不要删引擎的 `pullback.cpp`。**
