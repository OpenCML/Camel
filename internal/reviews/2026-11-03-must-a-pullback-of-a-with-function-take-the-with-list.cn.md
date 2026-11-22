# 带 `with` 参数的函数的 pullback，要不要把 `with` 列表写在类型上？

**问题**（给作者）：`vjp(f)` 的第二个分量（pullback）在 `f` 带 `with` 参数时，**取**`f` 的 `with` 列表，还是把它**闭包捕获**成常量、只取余切？

**这一轮我按"取"写了**（默认）：`Pullback<F>` 从
`(ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)` 改成
`<..WithOf<F>> (ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`，与 `Vjp<F>` 一致；对没有 `with` 参数的函数，包是空的，类型与原先逐字相同（`is_same::<Pullback<(x: float) => float>, (ct: float) => (float,)>()` 仍为 `true`）。

## 依据（不是偏好，是构建出来的类型）

1. `split(jvp(f))` 的线性那一半 `lin` 是一个**同样的** `with` 函数：`with` 值是整个变换的常量，不是前半产出的残差，`split` 没有把它剪成闭包捕获，所以 `lin` 仍取那份列表。
2. 因此 `transpose(lin)` 的类型是 `<float> (float) => (float,)` —— 这条是从编译器的诊断里**读出来**的，不是我推的：`vjp.cml` 写成 `(y, transpose(lin))` 时报
   `Return type '(float, <float> (float) => (float,))' does not match the function's declared return type '(float, (ct: float) => (float,))'`。
3. 裁定（2026-11-03 13:50）说 `transpose(lin)` 上那个 `with` "照它的类型定"。它的类型里**有**这份列表，于是列表留在 `lin` 的转置上；那么声明 `Pullback<F>` 必须承认它，否则 `vjp` 永远过不了类型检查（这与"声明即契约"（6eceaa89d）方向一致：契约要写成构建真做出来的那个类型）。
4. 早先"后半只靠 `lin` 闭包里的残差"的读法（裁定 11-02 11:30）不覆盖 `with` 值：残差是前半**产出**的值，`with` 值是调用方**给**的常量，两者不同一类。

## 如果答案是"闭包捕获"

那要动的是 `split`：把 `with` 值在切点当成残差捕获进 `lin` 的闭包，`Transpose<F>`/`Pullback<F>` 就都不带列表、我也能把 `Pullback<F>` 改回去。代价是 `split` 的切点规则要认 `with` 端口（`transforms/split` 19 条钉着的现有形状），而且 `lin` 的闭包要多一层。这一轮不这么做：它跨了 `split` 车道。

## 现在的表面代价（作者应知道）

`f` 带一个 `with` 值时，调用方要把那份值写两遍：
`let (y, pb) = vjp(scale)<2.0>(3.0); pb<2.0>(1.0)`。
若答案是"闭包捕获"，第二遍就没有了。`test/cases/compile/transforms/vjp_of_a_with_function.cml` 钉的是现在这条路（`pullback_a<2.0>(1.0).0` 等三行手算数）；答案若反过来，那三行要跟着改。