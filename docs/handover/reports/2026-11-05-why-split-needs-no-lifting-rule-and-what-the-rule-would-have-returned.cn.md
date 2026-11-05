# 为什么 `split` 不需要提升规则，以及那条规则会返回什么

**结论先说：给 `split` 的结果补两条提升规则（`jvp` 一条、`autodiff` 一条）不是一个「还没做」的修法，是一个会返回假数的修法。** 那 4 条 `known_fail`（`jvp_vjp_f.{nvm,fvm}`、`grad_vjp_g_zero.{nvm,fvm}`）维持原状，期望值一个字都不改。

**这份读数是为了一张任务书写的**——那张任务书写着「补两条规则，4 条就能绿」。**任务书错了，而写任务书的人是我。**

**读数树**：`D:\Projects\Camel-wt\lane-splitlift` = `origin/develop` `f56c65587`（本页所有读数都在这个基上取，宿主 checkout 名 `lane-splitlift`，二进制 built 2026-11-05T04:53:57-05:00，1 min load 15.4–15.9）。本页每一个数字都在 `nvm` 与 `fvm` 上各取一次，**两后端逐字一致**；负载不进入任何结论。

## 为什么不是补规则

两条最小程序，各 8 行，各读 `vjp` 的一半：

| 程序 | `nvm` | `fvm` |
|---|---|---|
| `jvp((x: f32) => vjp(sq)(x).0)(3.0, 1.0)` | exit 1，`transforms: jvp has no forward transform for this function` | 同左，逐字一致 |
| `jvp((x: f32) => vjp(sq)(x).1(1.0).0)(3.0, 1.0)` | exit 1，**同一句** | 同左，逐字一致 |

**两者撞同一句，所以这与「读哪一半」无关**：提升走的是整张体图，`split(jvp(f))<..ws>(..xs)` 那个返回值调用在两条路径上是**同一个节点**（`modules/transforms/split_operator.cpp#splitKernel` 造出的函数值的调用）。**结论是「卡在同一个节点」，不是「规则不够」。**

类型层还有更硬的一句，它先于任何图成立：

```
jvp(split(jvp(sq)))
  -> Invalid call to 'jvp' with argument types <> ((f32) => (f32, (f32) => f32 pure) pure):
     `jvp` takes a function whose result has a tangent space, and this one's result is a
     `(f32, (f32) => f32 pure)`, which has none
```

**`split` 的结果里有一个函数（`.1` 那半），函数没有切空间**，所以 `jvp` 的契约 `(值, 切向)` 对这个结果类型**没有定义**。门在 `modules/transforms/jvp_operator.cpp#jvpOperatorSignature` 的 `tangentSpaceOf(exitType).hasSpace()`；那句诊断由 `modules/transforms/jvp_operator.cpp#jvpKernel` 在 `built.graph == nullptr` 且没有具名出处时兜底抛出。

决定点则是 `modules/transforms/jvp.cpp#liftGraph` 的 `GCNodeKind::Call` 分支：**它对任何被调值一律写 `jvp(v)`，完全不按 uri 认算子**——所以「给 `split` 补一条 `jvp` 规则」在这条路上没有落点。

## 补出来的是 0.0，不是拒绝

不靠新规则，手工写一条「携带函数值、让外层 `jvp(pb)` 干活」的等价程序，两后端逐字一致：

```
lin=6.0 pb=6.0
naive value=6.0 tangent=0.0 want=2.0
```

`pull(x) = vjp(sq)(x).1(v).0 = 2*v*x`，`pull'(3) = 2`。**`want=2.0` 是正确答案，`tangent=0.0` 是这条规则给出的数。**

原因一句：**方向 `dx` 是 `x` 的，而 `x` 是 `lin` 的捕获；`modules/transforms/jvp.cpp#liftGraph` 的 `Call` 分支只把捕获的**值**交出去，切向按设计原则 9.9 取零**，所以 `pb` 里没有方向可流。

**所以这条规则不会让用例变红，会让它变绿——而数是假的。** 一旦 `0.0` 进了期望值，它会绿，而且再也没人去看它。这是「数字在动、东西没动」的实例：不是拒绝，是一个看起来正常的答案。

## `autodiff` 那一半与已有裁定相反

反向侧今天拒在 `modules/autodiff/pullback.cpp#Backward::refusalReason` 的那句 head 上，后半句由 `modules/autodiff/pullback.cpp#Backward::whyNotEvident` 的 `Oper` 分支给出，落到句尾：

```
autodiff: cannot differentiate through this call of a function value: which function it calls
is not evident where it is called: the function is computed at run time.
```

认不认一个算子，由 `modules/autodiff/pullback.cpp#ForwardRewrite::evidentFunction` 决定：它只认 `Data` 上的静态 `::Function*` 与单输入的 `Fill(FunctionClosure)`，`Oper`/`Port`/`Join`/`Accs`/`Func`/`Call` 一律不认。唯一**按 id 认算子**的是 `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee`，而它只认本模块自己的四个宏（`autodiff:grad`、`autodiff:with_grad`、`autodiff:grad_aux`、`autodiff:with_grad_aux`）。

**「在 `autodiff` 里认 `transforms:split`」不是待办，是被禁止的**：作者 2026-11-04 04:20 的裁定是引擎不许按出处判断一个函数值是谁算出来的（按出处的那句已经删掉），这些形状由**库层 `grad`** 覆盖——`docs/handover/reports/2026-11-04-the-author-answered-forty-eight-pages.cn.md` 的 `can-a-pass-build-the-graph-a-transform-hands-out` 一节（落点「让库层 `grad` 覆盖这些形状」，仍待做）。代码侧同一句话写在 `modules/autodiff/pullback.cpp#Backward::whyNotEvident` 的 `Oper` 分支注释里。

## `split` 不是孤例

同一个 `Oper` 判据、同一句话，`grad` 穿 `transpose` 的结果时也撞上（`nvm` 与 `fvm` 逐字一致）：

```
with <x: float> func slope(): float {
    let lin = (d: float) => x * d
    return transpose(lin)(1.0).0
}
grad(slope)<2.0>()
  -> autodiff: cannot differentiate through this call of a function value: which function it
     calls is not evident where it is called: the function is computed at run time.
```

所以这一类的名字不是「`split`」，是**「穿过一个携带的函数值」**——`transforms:jvp`、`transforms:split`、`transforms:transpose` 都造这种值，一条 `split` 规则只会把墙往后挪一个节点（挪到 `transpose` 上，读数就在上面）。

**真正的修法**：让**调用一个值**时的捕获像**直接调用一个具名函数**那样被提升（`modules/transforms/jvp.cpp#liftSignature` 的 `promoteWiths` 那条路），运行时 `jvp(v)` 的内核也要这么做（它今天不提升）。这不是一条规则，**是设计 4.4 里自述「未实现」的那一段**（函数类型的值按导数类型包装、调用点传 `D[f_value]`）。**下一个做这件事的人从这里进去：入口是「设计 4.4 落地」，不是「补两条规则」。**

## 三条可以立刻改对的写法

这三条与上面的墙无关，是那条用例**自己的正文**：改对了才走到墙上，不改则永远钉的是自己的写法。

1. **`vjp(f)(x, ct)` → `vjp(f)(x).1(ct)`**（读成一个数再加 `.0`，因为一参函数的 pullback 答 `(ct) => (f32,)`）。`stdlib/vjp.cml#vjp` 的面是 `(..params) => (value, pullback)`，余切是 pullback 的参数——作者 2026-11-01 的裁定。
2. **`vmap` 只收一元函数。** `vmap(jvp(pull))` 从来不被接受（`jvp(f)` 是 `(x, dx) => (value, tangent)`）；`jvp(x => vmap(pull)(x))` 又撞 `vmap has no rule for 'a constant'`（即 `vmap_grad_f` 那条墙）。今天没有把批轴放进来的写法。
3. **`grad` 求第一个 `with` 参数。** `grad((w: f32[4]) => ...)` 报 `it has no 'with' parameter`；要写成 `with <w: f32[4]> func loss(): f32 { ... }` 再 `grad(loss)<vs>()`。

**第 1、2、3 条都已由 `lane-vjpcases` 在 `f6a8281c9` 落地（该提交在 `develop` 上）。** 它落地的正是三件事：调用文本换成 `vjp(sq)(x).1(v).0`、批轴改成三个点各一次 `jvp`、`grad` 换成 `with` 参数——**没有动一个期望数字**。

## 那 4 条的期望值是对的，不许改成 2

现成绿用例 `compositions.pullback_of_a_sum_of_squares` 印 `pullback f32 ct=1 sum -> 20.0`，而 `2v = [2,4,6,8]` 求和正是 20。

**所以 `F(w) = sum(2w)`、`∇F = (2,2,2,2)`——这个数由一条现成的绿用例交叉证实，不是推的。而用例里那四个零属于二阶导（`grad` 再 `grad`），这条程序没写。**

**结论：那 4 条的期望值不许改成 2。** `0 0 0 0` 与 `2 2 2 2` 属于两个不同的程序，改期望就是把问题换成另一个，而且换成 2 也不会绿——墙还在。两个数都不许动，直到有人真的把二阶做出来。

**它们今天停在哪，取决于读哪棵树**：在本页的基 `f56c65587` 上停在正文自己（`vmap takes a function of exactly one parameter`、`cannot be called with argument types`）；在 `f6a8281c9` 之后的 `develop` 上正文已经改对，停的是本页量到的那两道变换墙。**两种情况都不是数值问题。**