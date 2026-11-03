# 参数表是"计算出的 pack"的函数值，要不要给一个拼写？

**状态**：待裁（2026-11-01，`c2181c95f`，原 `lane-m4-assembly`，该分支已删）

**问句**：`vjp` 要并成一条泛型 `vjp::<F>`（作者 14:30 裁定的方向），函数体得返回一个**参数表是计算出的 pack** 的函数值。今天这个形状没有拼写。要不要给一个？还是"两条按元数分写的声明"就是答案？

## 类型那半今天就能写，缺的只在体

```
type Vjp<F> = (..ParamsOf<F>) => (ResultOf<F>, (ct: float) => (..Tangent<ParamsOf<F>>))

is_same::<Vjp<(x: float) => float>,        (x: float) => (float, (ct: float) => (float,))>()        → true
is_same::<Vjp<(x: float, y: float) => float>, (x: float, y: float) => (float, (ct: float) => (float, float))>() → true
```

一元元组裁定（作者 14:30）之后，`grad` 的 `.0` 也正好对上。所以"一条声明"在**类型层面**已经不是问题。

问题在**体**：`vjp` 的体要 `return` 一个 `(..xs: ..ParamsOf<F>) => …` 那样的函数值。两条路都试过，都是语言拒绝，原文：

```
error[SyntaxDiag::UnexpectedToken]: Unexpected '..'; expected an expression.
 4 |     return (..xs: ..ParamsOf<F>) => 1
   |             ^^                                   （lambda 的形参表收不了 ..）

error[SemanticDiag::RestParameterTypeNotSupported]: The rest parameter '..xs' cannot write a type.
 8 |     func out(..xs: ..ParamsOf<F>): …
   |              ^^^^^^^^^^^^^^^^^^^
help: A rest parameter is untyped: the tuple of the arguments left over is made at the call, so its
      element types are decided there. Write '..xs'.
                                                      （改用嵌套具名函数绕也不行）
```

**一句话**：一个"参数表是计算出的 pack"的函数值**没有拼写**——`func` 的声明与定义可以写 `..xs: ..A`（`stdlib/prelude.cml#operator` 的 `zip`，用例 `firsts`/`both`），但那里的 pack 由**调用点**定（`help` 说得很清楚）；要让它由**外层泛型**定（`..ParamsOf<F>`），今天没有写法。

## 三条路

**① 让 lambda 的形参表收 `..`**
`(..xs: ..ParamsOf<F>) => …` 能解析、能定型。
- 好处：只动前端与 lambda 的定型，`stdlib` 一行不用改就能并成一条 `vjp::<F>`。
- 代价：lambda 的形参现在是"逐个具名"，带 `..` 之后调用的 spread（`forward(..xs)`）也得有拼写——两处前端一起动；而且它有和下面 ② 一样的类型问题（pack 由外层定）。

**② 允许 rest 形参标注一个计算出的 pack**
`func out(..xs: ..ParamsOf<F>)`。
- 好处：具名函数这条路已经能写体（`static for`、`xs.[j]` 都有），只差"标注"这一步。
- 代价：动的是设计 5.6 的"rest 形参无类型、成员由调用点定"这条规则——`help` 那句正是它。要在类型检查里允许"pack 由外层泛型定"，等于给 5.6 开第二种 pack 来源，规则要重写一段。

**③ 维持两条按元数分写的声明**（今天的事实）
```camel
export func vjp::<T>(f: (x: T) => float): (x: T) => (float, (ct: float) => (T,))
export func vjp::<T, U>(f: (x: T, y: U) => float): (x: T, y: U) => (float, (ct: float) => (T, U))
```
- 好处：零语言改动，今天四后端逐位在跑；作者 14:30 只裁了方向（"并成一条泛型"），没有裁机制。
- 代价：名字不是一处；元数 3 及以上要再加一叶（算子的实现本身不限于元数——`jvp`/`split`/`transpose` 都按图走）。

## 默认建议：③ 维持两条声明

理由只有一条：作者裁定的是**方向**（"并成一条泛型"），没有裁定**机制**；而机制这一层今天要动的是设计 5.6（rest 形参的 pack 由谁定），那是语言的事，不是 `vjp` 的事。等 ①/② 任一条落下，再并成一条是纯机械的改动。

## 今天堵着谁

- 堵着"`vjp` 合并成一条声明"这一笔（M4 迁移表判据②之后的那一步），不堵别的：两条声明的落地四后端逐位在跑（`27.0/27.0`、`18/12/9`、`grad=27.0`）。