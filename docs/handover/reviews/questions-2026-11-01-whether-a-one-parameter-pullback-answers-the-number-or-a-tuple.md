# 已裁（作者 2026-11-01 14:30，`235987b44`）：一元函数的拉回也是元组

**状态**：已裁已落（2026-11-01，`lane-m4-assembly`）

**裁定**：**拉回一律是元组，每个参数一项**，一元时就是 `(float,)`；不加“只有一项时取元素”的类型函数，也不为一元开例外。`grad(f)(x) = vjp(f)(x).1(1.0).0`（作者自己 06:50 那条式子补上 `.0`）。同一条裁定还说：第二步不是新增 `vjp:vjp` 算子，`vjp` 继续是库里的组合 `transpose(split(jvp(f)))`，第二步改走“三个算子编译期实例化”。

**落地读数**（四后端 nvm/fvm/jit/prl，逐位）：

```
one value=27.0 dx=(27.0)        vjp(cube)(3.0) 的拉回，ct = 1.0
f=18.0 dx=12.0 dy=9.0           vjp(f)(3.0, 2.0)
grad=27.0                       grad(cube)(3.0) = vjp(cube)(3.0).1(1.0).0
```

打印出来的形态是 `(27.0)`：一元元组的逗号在打印时省掉，类型仍是 `(T,)`。

---

以下是立页当日的记录，保留作对照。其中“名字背后是算子 `vjp:vjp`”那一段已被同一条裁定否掉，算子那两笔也已撤回（库体恢复成组合）。

## 今天的事实：落法是 (c)，两条按元数分写的声明

`stdlib/vjp.cml` 现在是：

```camel
export func vjp::<T>(f: (x: T) => float): (x: T) => (float, (ct: float) => T)

export func vjp::<T, U>(f: (x: T, y: U) => float): (x: T, y: U) => (float, (ct: float) => (T, U))
```

两条都没有函数体，名字背后是算子 `vjp:vjp`（`modules/transforms/vjp.cpp`）：它在 C++ 里做
`jvp` → `split` → `transpose` 三步，再用两张图与一个闭包包成
`(xs) => (forward(xs).0, (ct) => transpose(linear)(res, ct))`。`grad` 的函数体一个字没动。

读数是逐位不变的（nvm/fvm/jit/prl 四后端）：

```
one value=27.0 dx=27.0        vjp(cube)(3.0)、pullback(1.0)，cube(x)=x*x*x
f=18.0 dx=12.0 dy=9.0        vjp(f)(3.0, 2.0)，f(x,y)=x*x*y
grad=27.0                    grad(cube)(3.0)
```

**所以 (c) 不是建议，是当前事实**——这一问问的是要不要换掉它。

## 为什么不能一处声明：`(..Tangent<ParamsOf<F>>)` 对一元给出 `(float,)`

按设计 6.1 写成一处的形状是：

```camel
type Vjp<F> = (..ParamsOf<F>) => (ResultOf<F>, (ct: float) => (..Tangent<ParamsOf<F>>))

export func vjp::<F>(f: F): Vjp<F> when is_function::<F>()
```

它落进库里，库自己先编不过（2026-11-01 实测，`CAMEL_STD_LIB` 指向一份打过补丁的 stdlib 副本）：

```
error[SemanticDiag::ReturnTypeMismatch]: Return type '(x: float) => (float,)' does not match the
function's declared return type '(x: float) => float'.
   --> vjp.cml:57:5
```

那一行是 `grad` 的函数体：`pullback(1.0)` 被算成 `(float,)`，而 `grad` 声明的是 `(x: float) => float`。

原因只有一条：`..Tangent<ParamsOf<F>>` 对**一个**参数展开成**一元元组** `(float,)`，不是那个数。
作者 2026-11-01 的裁定是"一个参数的拉回给出那个数"（`grad(f)(x) = vjp(f)(x).1(1.0)`），所以声明
与裁定在元数 1 上不一致。

`stdlib/types.cml` 今天有 9 个类型函数，**没有一个能说"只有一个时取元素"**：

```
ElementOf<T>  PayloadOf<T>  WithElement<A, E>  RowsOf<T>  KeyOf<T>
ValueOf<T>    ParamsOf<F>   WithOf<F>          ResultOf<F>  Tangent<T>
```

（第 10 个是 `Tangent`，上面列了；合计 9 个面向类型推导的入口，外加 `Tangent` 本身不是推导器。）
所以"一处声明 + 一元给一个数"今天写不出来。

## 三条路与代价

**（a）拉回一律给元组**：一元也回 `(27.0,)`，`grad` 里改成 `pullback(1.0).0`。
- 好处：声明可以只留一处（`Vjp<F>` 原样）。
- 代价：**改的是作者已经写明的对外读数**——`vjp(f)(x).1(1.0)` 从 `27.0` 变 `(27.0,)`，所有按裁定
  写的用例、文档、迁移表都要跟着改；本车道实测的对照表也随之失效。

**（b）加一个类型函数**（建议）：`stdlib/types.cml` 里加一条能表达"只有一个时取元素"的入口，
例如 `SingleOrTuple<P>`，写进 `Vjp<F>` 的拉回位。
- 好处：**同时满足"一个名"与"一元给一个数"**，读数一个不变。
- 代价：语言扩展一道（类型函数是宿主实现的，`stdlib/types.cml` 声明、C++ 实现），要动的是
  "类型函数能表达什么"这一层，不是 M4 的车道。

**（c）保留按元数分写的声明**（今天的选择）：一元、二元各一条，体走算子。
- 好处：数与裁定都不动，零风险。
- 代价：名字不是一处；元数 3 及以上要再加声明（算子的实现本身已经是任意元数的）。

## 建议：(b)

只有 (b) 同时满足作者已经写下的两条——"一个名"与"一元拉回给一个数"。在 (b) 落下之前，
(c) 是唯一不动数与裁定的路，所以它现在是事实，不是候选。

## 今天堵着谁

- 堵着 `vjp` 的"一处声明"那一笔（M4 迁移表里的判据②），不堵其它任何东西：算子在跑，读数在。
- 元数 ≥3 的函数今天落到 `NoMatchingFunction`（没有声明可匹配），不是具名拒绝——这是 (c) 的
  已知代价，不是缺陷。


