# 类型层的包怎么拆：`match type` 的元组模式，还是声明处的拆包？

**状态**：待裁；我方默认见第 5 节（**甲：`match type` 的元组模式**，外加一个 `GradientOf<T>` 原语）。

写这页的原因：作者 2026-11-04 04:30 的第 ① 步原文写着"**具体语法由施工方提一页**"（`docs/handover/reviews/questions-2026-10-29.md` 的 `## 负责人定（2026-11-04 04:30）：类型层的包能拆`），规范性那一句在 `docs/design-principles.cn.md` §5。本页只提写法、**不改语言语义**；凡涉及语义的，都在第 6 节列成"需作者确认的一行"。

## 1. 裁定给的约束，与今天的拼法

两条读法上的硬约束：**与值层 `<w, ..rest>` 读起来一致**（§5.6 的值层规则：`<w, ..rest>` 拆出第一个）；**空包拆不出第一个成员，在实例化处按语言规则报错**。

今天树上已有的拼法（都不够用，列出来是为了说清新形式差在哪）：

- 整包拼进 `with` 列表、参数列表：`<..WithOf<F>> (..ParamsOf<F>) => …`（`stdlib/types.cml#Vjp`）
- 整包在类型位置按成员展开：`(..ElementOf<A>)[]`（`stdlib/prelude.cml#zip`）
- `when` 里整包谓词：`when ..is_array::<A>()`（`stdlib/prelude.cml#zip`）
- 不带右边的类型函数原语：`ParamsOf<F>`、`WithOf<F>`、`ResultOf<F>`、`ElementOf<T>`（`export type X<…>` 无体，编译器提供）

**没有一个**把包拆成"第一个 + 其余"，也没有"**具名成员 + 包尾**"的拼法（`<W, ..Rest>`）：今天 `..` 后面只能跟整包或一个类型表达式。三种写法都要用到这个新形式（见第 2 节的对照表），它不是选写法的依据，是本页要提的那件事本身。

## 2. 三种写法，与各自对既有规则的影响

### 甲：`match type` 的元组模式（默认）

```cml
export type Gradient<F> = match type WithOf<F> {
    (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => GradientOf<W>
}
```

- **空包报错位置**：`match type` 今天就是"在绑定 `T` 的那个实例被做出来时求值"（`stdlib/types.cml` 模块说明）。空包没有成员可配 `W`，天然落在**实例化处**。
- **与 `(..P) => R` 拼回**：`<W, ..Rest> (..ParamsOf<F>) => GradientOf<W>` 与今天的 `<..WithOf<F>> (..ParamsOf<F>) => …` 是同一个形状，只是左边由"整包"换成"具名头 + 包尾"。
- **`when` 里的 `..pred::<A>()`**：`W`、`..Rest` 只在类型表达式**体内**可见，声明处的 `when` 看不见它们，问不到 `W`。
- **要的新东西**：`match type` 的 scrutinee 从"一个类型"扩到"**一个类型包**"。这是本页唯一要作者点头的语义扩展（第 6 节第 1 条）。

### 乙：声明处的拆包子句（不引入包 scrutinee）

```cml
export type Gradient<F> unpacks <W, ..Rest> = WithOf<F> =
    <W, ..Rest> (..ParamsOf<F>) => GradientOf<W>
```

`unpacks` 是提议的关键字，位置在类型参数表之后、`=` 之前。

- **空包报错位置**：同样在实例化处，但报错能**直接挂在绑定处**——这条绑定写的就是"取第一个成员"，空包就是这条绑定失败，措辞天然是第 4 节那一句，不必借"没有分支匹配"去表达。
- **与 `(..P) => R` 拼回**：与甲逐字相同，因为拼回那一半不是三种写法的差别。
- **`when` 里的 `..pred::<A>()`**：`W`、`..Rest` 在声明处就绑了，**声明处的 `when` 能问它们**（例如 `when is_same::<W, float>()`）。三种写法里只有乙做得到。
- **代价**：类型函数多一个声明部位，而这门语言写明"类型函数的函数体是一个类型表达式，不是语句块"（§5.6）；在 `=` 之前插绑定是**第二处**声明语法。

### 丙：不加语法，加两个无体原语 `HeadOf<P>` / `RestOf<P>`

```cml
export type Gradient<F> =
    <HeadOf<WithOf<F>>, ..RestOf<WithOf<F>>> (..ParamsOf<F>) => GradientOf<HeadOf<WithOf<F>>>
```

照 `ParamsOf<F>` / `ElementOf<T>` 的既有约定：`export type HeadOf<P>` 无体，编译器提供。

- **空包报错位置**：也在实例化处，但失败的是 `HeadOf` 这个原语，**不是这个函数**。诊断会变成"`HeadOf` 收到了空包"，指向一个用户从没写过的名字；裁定要的那句话说的是"这个函数的 `with` 列表是空的"。
- **与 `(..P) => R` 拼回**：**仍然需要** `<具名成员, ..包尾>`，因为 `<HeadOf<…>, ..RestOf<…>>` 正是"一个具名成员 + 一个包尾"。丙省不掉这半，只是把绑定换成了投影，还把 `HeadOf<WithOf<F>>` 在答案与 `with` 列表里各写一遍（两处必须相等，靠人看）。
- **`when` 里的 `..pred::<A>()`**：投影是纯类型表达式，能出现在 `when` 里，但每次都要重写；且**空包时 `HeadOf` 先炸**，轮不到 `when`。

### 三处对照

| | 甲 `match type` 元组模式 | 乙 声明处拆包 | 丙 两个原语 |
| --- | --- | --- | --- |
| 空包报错位置 | 实例化处（实例建立时求值） | 实例化处（绑定处） | 实例化处（`HeadOf` 内） |
| 能否说"这个函数的 `with` 列表是空的" | 要特判模式不匹配，否则只能说"没有分支匹配" | 能，绑定本身就是"取第一个" | 不能，说的是 `HeadOf` |
| 与 `(..P) => R` 拼回 | 需要新增 `<W, ..Rest>` | 同甲 | 仍需要 `<HeadOf<…>, ..RestOf<…>>`，即同一个新形式 |
| `when` 能否问 `W` | 不能 | **能** | 只有重写投影时能，且空包先炸 |
| 新增语法 | 包模式 1 处（+ scrutinee 可为一包） | 关键字 1 个 + 声明部位 1 处 | 语法 0 处，原语 2 个 |

## 3. 四个声明（按甲写，可直接照抄施工）

放 `stdlib/types.cml`，紧接 `stdlib/types.cml#WithOf` 那一段之后：

```cml
/// `grad(f)` 的类型：`f` 的 `with` 列表照旧，`f` 的普通参数照旧，答案是第一个 `with` 参数的梯度类型。
/// @example is_same::<Gradient<<w: float> (x: float) => float>, <w: float> (x: float) => float>()    // true
export type Gradient<F> = match type WithOf<F> {
    (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => GradientOf<W>
}

/// `with_grad(f)` 的类型：同一个调用形状，答案是 `(f 的结果, 梯度)`。
/// @example is_same::<ValueAndGradient<<w: float> (x: float) => float>, <w: float> (x: float) => (float, float)>()    // true
export type ValueAndGradient<F> = match type WithOf<F> {
    (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => (ResultOf<F>, GradientOf<W>)
}

/// `f` 的结果是 `(value, aux)` 时，`has_aux` 拆出来的第二个成员。
export type AuxOf<F> = match type ResultOf<F> {
    (V, Aux) => Aux
}

/// `grad(f, has_aux: true)`：答案是 `(梯度, aux)`，JAX 的顺序。
export type GradientAndAux<F> = match type WithOf<F> {
    (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => (GradientOf<W>, AuxOf<F>)
}

/// `with_grad(f, has_aux: true)`：答案是 `((值, aux), 梯度)`，即 `(ResultOf<F>, 梯度)`。
export type ValueAndGradientAndAux<F> = match type WithOf<F> {
    (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => (ResultOf<F>, GradientOf<W>)
}
```

**用法（只给语法不算交）**

1. `stdlib/signatures/autodiff.cml#grad` 的签名一个字不改：`export func grad::<F>(f: F): Gradient<F> when is_function::<F>()`。今天 `Gradient<F>` 在标准库里**没有声明**，它是 `modules/autodiff/pullback.cpp#gradientSignature` 在 C++ 里算出来的；这四个声明落完，它才第一次有声明可对，`stdlib/signatures/autodiff.cml` 也就不必再用"arbitrary-function-type gap"解释自己。
2. 第 ③ 步的组合照这些声明写：`<W, ..Rest> (..xs) => vjp((w) => f<w, ..Rest>(..xs)).1(1.0)`，答案类型正好是 `GradientOf<W>`；`with_grad` 多带结果分量，正好是 `(ResultOf<F>, GradientOf<W>)`。
3. 自检：`stdlib/types.cml` 里 23 条 `@example is_same::<…>()` 就是钉声明的既有办法（`#Vjp`、`#Jvp`、`#Transpose` 都这么钉）；四个新声明各一条，`feat/docs_links` 的 `docs.snippets_compile` 会跑。

**多出来的两个名字，裁定没点**（`#AuxOf`、`GradientOf<T>`），理由：

- `#AuxOf`：`(value, aux)` 的第二个成员今天没有名字可用，`Tangent`/`ElementOf` 都不回答"对的第 2 个成员"。
- `GradientOf<T>`：`gradientTypeOf` 回答的是"**面向用户**的梯度类型"（`include/camel/core/derivative.h`：数与布尔是它们自己，`string` 与函数没有，结构体/元组逐成员），与 `Tangent<T>`（设计 9.9 的切空间：`int`、`bool`、`string`、函数都答 `()`）**不是同一个答案**。写 `W` 只在 `W` 是数字标量时对；写 `Tangent<W>` 会把"`string` 的 `with` 参数"从拒绝变成答 `()`，那是语义变化（第 6 节第 3 条）。所以默认写 `GradientOf<W>`，并在 `stdlib/types.cml` 加一条无体原语 `export type GradientOf<T>`，与 `Tangent<T>` 并列。

## 4. 空包在哪报错、报什么话

**"实例化处" = 这个类型函数被一个具体的 `F` 实例化的那一刻**：不是声明处（声明里 `F` 还是变量），也不必等到调用——类型函数"在绑定 `T` 的那个实例被做出来时求值"（`stdlib/types.cml` 模块说明）。三处都算：`grad(f)` 的声明结果类型、类型位置里显式写的 `Gradient<(x: float) => float>`、`is_same::<Gradient<…>, …>()` 的实参。程序一个 `grad` 都不调用，`Gradient<(x: float) => float>` 也会被拒——与 `ElementOf<int>` 被拒是同一档。

**措辞草案**（甲乙同用；丙换成 `HeadOf` 的名字就指错了人）：

```
grad 的答案是 Gradient<F>，而 `(x: float) => float` 的 `with` 列表是空的：拆不出第一个 `with` 参数的类型。
给这个函数加一个 `with` 参数（设计原则 9.9 对第一个 `with` 参数求导），或者用 `vjp`，它的答案按普通参数排。
```

英文同一句：`` `Gradient<F>` takes the type of the first `with` parameter of `(x: float) => float`, and that `with` list is empty. Give the function a `with` parameter (design principles 9.9 differentiates the first one), or use `vjp`, whose answer is laid out by ordinary parameter. ``

## 5. 默认建议（未回复即按此施工）

**甲 + `GradientOf<T>` 原语。** 理由：

1. 甲的求值时机与裁定要的"实例化处"是同一件事：`match type` 今天就在实例建出来时求值，不必新造一条"何时报错"的规则。
2. 甲的体仍是一个类型表达式，符合"类型函数的函数体是一个类型表达式，不是语句块"（§5.6）；乙要在 `=` 前再加一个声明部位。
3. 甲是作者自己的第一建议，与值层读法一致（`(W, ..Rest)` ↔ `<w, ..rest>`）。
4. 丙省不掉任何语法（仍需 `<具名成员, ..包尾>`），却把诊断指向用户没写过的名字，不取。

## 6. 需作者确认的一行

1. **`match type` 的 scrutinee 能否是一个类型包**（甲的前提）。今天它只匹配类型，包不是类型；若不扩，甲只有写法没有语义，得退到乙。
2. **`<W, ..Rest>`（具名成员 + 包尾）是否合法**：今天 `<…>` 里只出现过整包 `..P` 与具名参数（`<w: float>`）。三种写法都要它，所以它是本页要提的新形式本身，不是选写法的依据。
3. **`GradientOf<T>` 还是 `Tangent<T>`**：前者照 `gradientTypeOf`，后者是设计 9.9 的切空间，两者对 `int`/`bool`（前者答自己、后者答 `()`）、`string`/函数（前者没有、后者答 `()`）回答不同。取 `Tangent<T>` 会改变今天的读数。
4. **`ValueAndGradient<F>` 与 `ValueAndGradientAndAux<F>` 的类型表达式相同**（都是 `(ResultOf<F>, 梯度)`），差别只在 `F` 的结果是不是一对。裁定说写两个，我就写两个；若只要一个名字，第 ② 步少一行。
5. **`var` 参数**：`gradientSignature` 保留 `with`/普通参数的 `var`（`include/camel/core/type/composite/func.h#FunctionType::withIsVarAt`），类型层今天写不出 `var`。四个声明因此对 `var` 参数"不多说"——这是第 ② 步的已知缺口，不是本页能补的。

## 7. 这一步不做什么

**不写实现**：只提写法。不改 parser / 类型检查器，不碰 `src/compile/gct/builder.cpp#Builder::visitFuncData`（值层 `<w, ..rest>` 的那个守卫，是第 ③ 步的前提，另一件）。

**不从第 ④ 步开始**：删 `modules/autodiff/operators.cpp#GradResolver`、`modules/autodiff/pullback.cpp#gradientSignature`、`pullback.cpp` 与 `buildGradientGraph` 是第 ④ 步，在 ①②③ 落地之后才做；四个声明还没进树、组合还没同答之前先删任何一个，都是把唯一能算这个类型的实现先拆掉。

**不动别的裁定**：`stdlib/vjp.cml#with_grad` 那份永远拒绝的导出要删，是 04:20 第五节的事，不在本页。