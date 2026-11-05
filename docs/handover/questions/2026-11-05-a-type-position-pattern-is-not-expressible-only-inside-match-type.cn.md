# 包模式只在一个位置有读法：类型位置的 `match type` 的 case

**判据一句话**：`(W, ...Rest)` 今天**能写**，但只有一个位置能写——`match type` 的 case，且那个 `match type` 自己站在**类型**位置上；别的位置写同样这几个记号，要么只是一段元组类型，要么不绑定名字。

状态：待裁。**已按默认开工**（`FirstOf<P>` 这条既有通道）。基线 `origin/develop` `43b775401`，树 `D:\Projects\Camel-wt\lane-patq`。

## 要你裁的是什么

**这条缺口记成"类型位置缺模式语法"（读者会去要一个新特性），还是记成"包模式只在一个位置有读法"（要落的是值位置那半，模式语法本身已经在）？**

## 量出来的边界

| 位置 | 整包展开 `...P` | 拆包 `(W, ...Rest)` |
| --- | --- | --- |
| 类型位置，`match type` 的 case | ——（这里是拆，不是展开） | **能** |
| 类型位置，其它类型表达式（元组成员、`with` 列表、结果） | 能 | **不能**：三个记号只是一段元组类型，`W`/`Rest` 不是新名字 |
| 值位置，函数体里的 `match type` 的 case | —— | **不能**：同一个 case 在这里按身份匹配，不绑定名字 |
| 值位置，lambda 的形参表与 `with` 列表 | 能 | **不能** |

**能写的位置（两条，都是真在树里的）：**

1. `stdlib/types.cml#FirstOf` 的声明体 `match type P { case (W, ...Rest) => W }`——类型位置的 `match type`，标准库里，今天就在编译。
2. `test/cases/compile/type_packs/pack_taken_apart.cml#RestOf` 的 `type RestOf<P> = match type P { case (W, ...Rest) => Rest }`——用户程序自己写的、不要库里任何东西；同一文件的 `test/cases/compile/type_packs/pack_taken_apart.cml#FirstWithOf` 把 `Head<WithOf<F>>` 写进函数类型的结果，说明**头部与其余两半都能在用户程序里写出来**。

**写不出来的位置（两条真实被卡的）：**

1. `stdlib/types.cml#Gradient` 的声明：它要的顺序是"先按 `(W, ...Rest)` 拆开 `WithOf<F>`，再用 `W` 与 `Rest` 拼 with 列表与结果"，而声明自己的类型表达式不是 `match type`，拆不了。今天写成 `<...WithOf<F>> (...ParamsOf<F>) => Tangent<FirstOf<WithOf<F>>>`——**这是绕开，不是卡住**（同一类型另有写法，见下一节）。
2. `modules/transforms/jvp.cpp#withIsPromoted` 的注释段：它记下挡住 `grad` 组合的两条，第二条就是这个形状在**值位置**缺席——"a lambda's parameter list cannot write `<w, ...Rest>`"。同一段还给出对照：整包在 lambda 上**能**展开，`stdlib/vjp.cml#vjp` 就写着 `return <...ws> (...xs) => { … }`。所以缺的不是"lambda 收不了包"，是"包里拆不出第一个、其余还留在手上"。

**最要紧的第三处（本页的真正读数）**：`src/compile/gct/builder.cpp#Builder::visitMatchType`——值位置的 `match type` 把 case 的 pattern 走 `visitType` 当身份匹配，而绑定包模式名字的 `packPatternNamesOf` 只从 `Builder::visitMatchTypeType` 调用一次。于是"在函数体里写 `match type WithOf<F> { case (W, ...Rest) => <一个值> }`"写不出绑定。**类型位置拆包与值位置写值之间没有桥**——这一处才是 `grad` 组合真正缺的那一步。

### 任务书上那句描述错在哪

- "`(W, ...Rest)` 这种类型位置的模式今天表达不出来"——**今天能表达**，就是 `stdlib/types.cml#FirstOf`。那句话出自 `docs/handover/reviews/2026-11-05-does-a-type-position-need-pattern-syntax-or-is-firstof-the-channel.cn.md#2.4`，是在 `06cdadd45` 上量的；`513956ecb` 把 `FirstOf` 改成库里的 `match type` 之后它不再成立，`docs/handover/reviews/questions-2026-10-29.md#返工（2026-11-04 20:00）：FirstOf<P> 不做成 C++ 原语` 记的就是这次返工。
- 所以正确的一句是**形状已经有了，少的是一个位置**：类型位置的 `match type` 的 case 之外，同名记号不产生绑定。写成"缺类型层模式匹配"会让作者去要一个已经落地的特性。

## 今天怎么绕开的

`FirstOf<P>` 这条既有通道。`stdlib/types.cml#FirstOf` 的声明原文，逐字：

```camel
export type FirstOf<P> = match type P { case (W, ...Rest) => W }
```

它自己的文档注释（同一处 `stdlib/types.cml#FirstOf`）写着它不是原语："This is not a primitive: it is the language, written here because a function type computed from `F` needs it and a program that writes the pattern itself gets the same answer."`test/cases/compile/type_packs/pack_taken_apart.cml` 的文件头把这条钉成判据：那个文件不 import 任何拆包的东西，自己写 `type Head<P>`，库里的 `FirstOf<P>` 只是同一个 case。

## 绕不过去的地方

卡在 `stdlib/vjp.cml#has_aux` 这一处（以及它缺的那个兄弟 `with_grad`）：

- **它想写**：一个形状一条声明的替代品——`<...ws> (w, ...xs) => …`，其中 `w` 是被微分的那个 `with` 参数、`...xs` 是其余参数；也就是说 `with` 列表要拆成 `(w, ...Rest)`，拆出来的 `w` 再作为一个**值**参数用。这一步同时要"类型层拆包"和"值位置写值"，而这两者在今天的位置表里是分开的两格（`src/compile/gct/builder.cpp#Builder::visitMatchType`）。
- **现在只能写成**：一个形状一条声明——`export func has_aux(f: (w: float) => (float, float)): (w: float) => ((float, float), float)`（`stdlib/vjp.cml#has_aux`），`grad` 同样是 `export func grad(f: (x: float) => float): (x: float) => float`（`stdlib/vjp.cml#grad`）。
- **`with_grad` 本身不在库里**：作者的裁定 `080778522` 删掉了没有体的占位，所以 `test/plans/feat/compile/transforms.plan.toml#transforms.with_grad_from_the_library` 挂 `known_fail`；那一条的 `known_fail_note` 说它等的正是"composition + 声明 `FirstOf<WithOf<F>>`"。
- **卡点原文**在 `modules/transforms/jvp.cpp#withIsPromoted`：两条缺口是"`vjp` 的 lambda 体不能调用函数值 `f`"与"lambda 的形参表写不出 `<w, ...Rest>`"，两条到位后 promotion 那座桥一起拆。**两条都在值层**，没有一条要求类型层的新语法。

## 默认

**先用 `FirstOf<P>` 这条既有通道，等有模式语法再换写。**

这条默认**已经开始执行**，在用它的地方：

- `stdlib/types.cml#Gradient`：结果那一格写 `Tangent<FirstOf<WithOf<F>>>`，一条声明覆盖每个形状。
- `test/cases/compile/type_packs/gradient_type.cml#Gradient`：用例把 `FirstOf<WithOf<Loss>>` 与 `f64[2]` 判成同一个类型，也就是拿 `FirstOf` 当正写钉住。
- `test/cases/compile/type_packs/pack_taken_apart.cml#RestOf`：另一半（"其余"）用的仍是同一个 case 形状，只是结果换成 `Rest`。
- `test/plans/feat/compile/transforms.plan.toml#transforms.with_grad_from_the_library`：`known_fail` 的注记把 `FirstOf<WithOf<F>>` 写成组合将来要交的那条声明。

本页量出的边界让"等有模式语法"更窄一档：**类型位置这一半不用等**（`match type` 的 case 已经能写，`FirstOf` 就是它）；要等的是值位置那半——`<w, ...Rest>` 出现在 lambda 的形参表上（`modules/transforms/jvp.cpp#withIsPromoted`）。在它落地之前，`ValueAndGradient<F>` 一类按 `FirstOf<WithOf<F>>` 写，不拿两次 `FirstOf` 去硬凑"其余"。

## 判据

1. 同一个 `F`，模式拆出来的 `W` 与 `FirstOf<WithOf<F>>` 必须是 `is_same` 的 `true`（今天已是，`test/cases/compile/type_packs/gradient_type.cml` 与 `test/cases/compile/type_packs/pack_taken_apart.cml` 一起钉住）。
2. 值位置的 `<w, ...Rest>` 落地那天，`stdlib/vjp.cml#has_aux` 与 `stdlib/vjp.cml#grad` 的 per-shape 声明换成泛型组合，`test/plans/feat/compile/transforms.plan.toml#transforms.with_grad_from_the_library` 由 `known_fail` 翻红，四个 `library=` 字段从 `RuntimeError` 变成元组。