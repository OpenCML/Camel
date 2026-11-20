# `split` 割「结果含函数」的图时，前向半要不要收切向端口

**问句（一句）**：`split` 割一个**结果含函数**的提升图（`jvp` 之后，`Lift<T>` 已按 2026-11-20 16:00 裁定落地）时，前向半遇到一条**必须**拿到切向端口才能算完的调用，是继续拒绝（「值半不拿切向」），还是允许它拿**它需要的那一个**？

## 默认建议（写死：本轮按此继续）

**允许值半收切向端口，但只在被割的图自身需要它时。**

理由：`split` 那句「值半不拿切向」是在「切向只作**输出**」的时代写的；二阶让切向变成**输入**，于是这条不变式要松成「**值半不拿它不需要的切向**」。

**它不是特判**：特判是「若二阶则放行」；这里判的是「这条调用**要**这个端口吗」——与「若成员是函数则跳过」不是一类。

**代价写死**：这个松法让 `split` 的拒绝少一类，所以必须同时有一个**阴性臂**：**一条值半不需要切向的图，改后仍必须拒**（证明不是把这条拒绝整个删了）。

### 它落在哪一行，以及为什么它不只是 `split.cpp` 的一行

松法的落点只有一个：前向半的**参数表**。今天它等于「值的那半」（`splitHalvesWith` 里 `forwardParams` 只取 `0..n`）；松法之后它等于「值的那半 + **这条体真的读到的**那些切向端口」。

于是问题不在 `split.cpp`，在类型的**可算性**：

```
`split` 算子的答案类型由解析器算，解析器**只拿得到类型**（`residuals == nullptr` 那一支）
⇒ 「这条体读到哪些切向端口」是**图的属性**，类型里没有
⇒ 解析器给调用点的答案类型（值的那半）与割真正建出来的前向半（值 + 若干切向）**对不上**
⇒ 调用点按解析器的答案配实参，运行期就是 norm-arity mismatch
```

这不是新问题，`split` 的残差位早就撞过一次同样的形状，并且已经用「**在手里有图的地方填洞**」解决了（`split.h` 开头那段：`Split<F>` 的残差位是编译器在图在手时填的洞，解析器只给一个替身）。松法要的是**同一个洞的第二个字段**：前向半的切向端口也是图属性，也得由「图在手」的那一步填，而替身该怎么拼是**类型层的决定**。

**⇒ 所以这一格不只是割的法则，还是 `Split<F>` 那个替身的类型怎么写。** 我不想在用类型回答不了的地方加一个「若读到了就补一个端口」的图侧特判——那正是「在两个死点各加一处」的形状。

## 一、读数一：类型那道墙已经没了，紧随其后的是这一道

本轮把「结果类型按类型提升」落地为**一处**计算（`modules/transforms/lift.h#LiftedShape`，定义在 `jvp.cpp`）：

```
Lift<T>       = (T, Tangent<T>)        T 有切平面（与改前逐字一致）
Lift<A => B>  = Lift<A> => Lift<B>     函数提升为提升后的函数（`liftSignature` 同一条法则下一层）
Lift<(T0..)> = (Lift<T0>, ...)         结果含函数 ⇒ 逐分量提升
```

`split` 按裁定「同样认提升后的函数分量」读同一条法则（`split.cpp#splitHalvesWith`，一处）。于是 `feat/transforms/nested` 上：

```
改前：`jvp has no forward transform for this function`（六条 XFAIL 逐字都是这一句）
改①后：`split does not build this function (a tangent of a call): the value half computes the value
        of the call, and the tangent of a call is computed after the cut -- its component 1 here is
        the linear half, which is a function and not a tangent`
        raise 点 modules/transforms/split.cpp 的 Accs/`pairTangentReadOf` 那一支
改②后（split 接了法则）：`split does not build this function (a tangent port): the value half reads
        the tangent of parameter 0, which the forward half does not take`
        raise 点 modules/transforms/split.cpp 参数端口那一支
        （`refuseAt(refusal, source, ref, "a tangent port", "the value half reads the tangent of
          parameter " + …)`）
```

## 二、读数二：值半**确实**需要那个切向——这不是照抄照错了

被割的图是 `G = jvp(first_with_as_param(grad(quartic)))`。`G` 的体里有一条**对被提升过的闭包 `forward` 的调用**：

- 源图里它是 `forward(w)`，答案是 `(float, (float) => float)`；
- 提升后参数是 `(w, dw)`，答案按新法则变成 `((float, float), Lift<F'>)`。

割 `G` 时 `answersAPair` 按法则答 **false**（正确：它不是 `(result, tangent)` 那对），于是值半照抄这条调用；照抄就得给 `(w, dw)` 两个实参 ⇒ 值半要读 `dw`（port 1，切向端口）。

**而 `Lift<F'>` 这个提升闭包确实依赖 `dw`**：二阶的方向。所以值半想答出「`G` 在 `w` 处的值」这条链，就真的需要那个方向。**问题不是照抄照错了，是割的法则要不要松。**

## 三、读数三：数字（坐标）

| 读数 | 改前 | 改后 |
|---|---|---|
| `feat/transforms/nested` | `pass 4, fail 0, known 6` | `pass 3, fail 3, known 4` |
| 其中 `nest_quartic / nest_square / nest_cube` | 一条负用例 + 两条 XFAIL（钉 `jvp has no forward transform`） | `fail`，runner 逐字 `known failure changed its mind: expected "jvp has no forward transform for this function", got "expected exit 0, got exit 1"` |
| `nest_named / nest_lambda / hessian / confusion` | XFAIL（同上那句） | **XFAIL，逐字仍是同一句**（本页第七节） |

**这不是回归**，两条坐标：

1. `feat/transforms/split` 改后 **`total: 30, pass: 30, fail: 0`，逐字等于基线** —— 法则动了 `liftSignature`，但今天对的那条 `(result, tangent)` 路径一条没坏。
2. `feat/compile/transforms` 改后 `total: 214, pass: 201, fail: 0, known failures: 13` —— `fail 0`、`known 13` 与基线同（`total 214` 是该计划后来加了条目，基线记录的是 197）。

`fail 3` 的形状是「**拒绝没了、用例还钉着拒绝**」：三条都必须等这一格裁完才有新读数，现在改计划就是拿愿望钉读数。

## 四、一处实现的证据（自检）

法则只有一处（`lift.h` + `jvp.cpp` 的定义），三个读者：`jvp` 的签名（`liftSignature`）、行走里每一个被写的节点形状（调用答案、投影、元组、出口，全部只调 `liftedShapeOf`）、`split` 的「能不能割」。

**没有任何调用点自己判「这里是不是函数」**：唯一的形状判断在法则内部（三条法则的那三条分支）。`jvp.cpp` 的 Fill 分支另有一个「这是不是 tuple fill」的判断，问的是**填充节点自己的形态**（闭包 fill 的成员是捕获，不是待提升的分量），不是成员的类型。

## 五、阴性臂（松法必须带的）

一条**值半不需要切向**的图改后仍必须拒：`split(jvp(f))`，`f` 的体里读一个切向端口而值半用不到它（今天的拒绝句不变）。落地时把它写成计划里的一条 `negative`，与松法同一次提交出现。

## 六、`Lift<(T0..Tn)>` 的弃答边界（真缺口，先报）

法则现在写的是：**任一成员无空间且不是函数 ⇒ 整条 tuple 不答**（不造带洞的 tuple）。所以 `bool`/`int` 与函数同处一个 tuple 的结果仍然没有提升形状。这不是特判能补的——要补的是「一个既有无空间成员、又有函数成员的结果，它的 `Lift` 是什么」。本轮未动，等裁定。

## 七、四条仍未动的 XFAIL（分类：**另一道墙**，且坐标已在仓里）

`nest_named / nest_lambda / hessian / confusion` 改后**仍逐字**是 `jvp has no forward transform for this function`，`stdlib/vjp.cml:190:25`。

**它们归「另一道墙」，不是「`Lift<T>` 没覆盖」**，依据三条，都在仓里：

1. 那条拒绝的成因是 `refusedOrigin == kInvalidOriginId`（**没有原因、没有位置**），而不是 `liftSignature` 那一支：计划自己对这三条的理由逐字写着「the outer `jvp`'s lift refuses a call **whose callee's code is not readable**, and the callee here is what `split` handed out … `refusedOrigin == kInvalidOriginId` (no cause, no place)」（`test/plans/feat/transforms/nested.plan.toml:65`）。
2. 同一条理由也写在 `nest_square` / `nest_cube` 上（同文件 `:29`、`:45`），而**这三条这次动了**（拒绝句换成了第一节那两句）⇒ 那条理由对它们已经过期、对这四条仍然成立 ⇒ 差别在**写法**，不在法则：这四条的写法是让内层变换出现在**被外层微分的那段体里**（`grad(cube)<x>()`、lambda 体里的 `grad(cube)<v>()`、`grad(f)<x>()` 之后再取分量），于是外层提升要走一条**穿过函数值的调用**，而那个被调者的图在提升时读不到。
3. 计划自己的实测记录同意这条：「the outer `jvp` prints nothing at all」（`nested.plan.toml:67`，2026-11-20 `--trace transforms` 实测）——**提升连一步都没走进去**，这与走进去之后死在割上（第一节那两句）是不同的两道墙。

**⇒ 所以这一栏是计划乙第 1 项（提升走进 `split` 交出的那段代码），与本页第一格的割法则并列，不是它的前置。**

> 顺带更正一处工具认知：`-AstOnly` 不是类型打印器，它只选 `-UpdateAstHashes` 要重写哪些计划行（单独给它会直接 `exit 2`），拿不到这四条的类型。要拿到，得用 `--trace transforms` 的 `REINTERP jvp … skip=<cause>` 行——而第 3 条已经证明这四条**连 `REINTERP` 行都没有**，所以那一步拿不到更多东西。

---

**这一格要作者裁的是第一节最后那句**：`split` 的前向半在「被割的图自身需要」时能不能收切向端口。**类型法则已经把「墙在类型」这一项做完了**；剩下的是割的法则，`Lift<T>` 答不了它。