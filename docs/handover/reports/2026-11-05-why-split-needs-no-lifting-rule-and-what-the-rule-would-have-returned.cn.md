# 为什么 `split` 不需要提升规则，以及那条规则会返回什么

**结论先说：给 `split` 的结果补两条提升规则（`jvp` 一条、`autodiff` 一条）不是一个「还没做」的修法，是一个会返回假数的修法。** 那 4 条 `known_fail`（`jvp_vjp_f.{nvm,fvm}`、`grad_vjp_g_zero.{nvm,fvm}`）维持原状，期望值一个字都不改。

**这份读数是为了一张任务书写的**——那张任务书写着「补两条规则，4 条就能绿」。**任务书错了，而写任务书的人是我。**

## 为什么不是补规则

两条最小程序，各 8 行，`f56c65587` 上两后端一致：

| 程序 | 诊断 |
|---|---|
| `jvp((x) => vjp(sq)(x).0)` | `transforms: jvp has no forward transform for this function` |
| `jvp((x) => vjp(sq)(x).1(1.0).0)` | 同上 |

**`.0` 与 `.1` 撞的是同一句，所以这与「哪一半」无关。**

类型层更硬的一句，直接说清了为什么：

```
jvp(split(jvp(sq)))
  -> Invalid call to 'jvp' ... this one's result is a (f32, (f32) => f32 pure), which has none
```

**`split` 的结果没有切空间。** 所以门不在认不认 `split` 这个算子，在 `modules/transforms/jvp_operator.cpp#jvpOperatorSignature` 的 `tangentSpaceOf(exitType).hasSpace()`。

决定点则是 `modules/transforms/jvp.cpp#liftGraph` 的 `GCNodeKind::Call` 分支：**它对任何被调值一律写 `jvp(v)`，完全不按 uri 认算子。**

## 补出来的是 0.0，不是拒绝

不靠新规则、手工写一条「携带函数值、让外层 `jvp(pb)` 干活」的等价程序，读数是：

```
naive value=6.0  tangent=0.0  want=2.0
```

**`want=2.0` 是正确答案，`tangent=0.0` 是规则给出的数。**

原因一句：**方向 `dx` 是 `x` 的，而 `x` 是 `lin` 的捕获；`Call` 分支只把捕获的值交出去（`carries`），切向按设计原则 9.9 取零。**

**所以这条规则不会让用例变绿，会让它变绿**并且是错的**。** 这是今天反复出现的那一类的反面实例：**前几次是「读数在、意义错了」，这一次是「规则在跑、数字是假的」。** 一旦 `0.0` 进了期望值，它会绿，而且再也没人去看它。

## `autodiff` 那一半与已有裁定相反

`split` 旁边还站着一个同类节点，实测 `grad` 穿它报同一句：

```
with <x: float> func slope() { let lin = (d) => x * d; transpose(lin)(1.0).0 }
  -> autodiff: cannot differentiate through this call of a function value
```

**而作者已经裁过这一类：引擎不许按出处认算子，这些形状由库层 `grad` 覆盖。** 出处是 `docs/handover/reports/2026-11-04-the-author-answered-forty-eight-pages.cn.md` 的 `can-a-pass-build-the-graph-a-transform-hands-out` 一节（结论「让库层 `grad` 覆盖这些形状」，仍待做）。代码侧的同一句话写在 `modules/autodiff/pullback.cpp#Backward::whyNotEvident` 的 `Oper` 分支注释里。

**所以在 `autodiff` 里认 `transforms:split` 不是待办，是被禁止的。**

## `split` 不是孤例

`split` 与 `transpose` 撞同一句诊断，说明这一类的名字不是「`split`」，是**「穿过一个携带的函数值」**。

**真正的修法在 `modules/transforms/jvp.cpp#liftGraph` 的 `Call` 分支：捕获要像 `Func` 那样被提升（`liftSignature(promoteWiths=true)` 那条路）。而运行时 `jvp(v)` 的 kernel 不提升捕获，所以这等于一个新算子加新语义——正是 `docs/technical/` 里设计 4.4 自述「未实现」的那一段。**

**这句话是给下一个做这件事的人的入口：不是「补两条规则」，是「设计 4.4 落地」。**

## 三条可以立刻改对的写法

比提升规则值钱，因为**这三条是能做到的**：

1. **`vjp(f)(x, ct)` → `vjp(f)(x).1(ct).0`。** `stdlib/vjp.cml#vjp` 的签名是 `func vjp::<F>(f: F): Vjp<F>`，`vjp(f)(x)` 给 `(value, pullback)`，余切是 pullback 的参数。**这条已由 `lane-vjpcases` 在 `f6a8281c9` 落地。**
2. **`vmap` 只收一元函数。** `vmap(jvp(pull))` 从来不被接受（`jvp(f)` 是 `(x, dx) => (value, tangent)`）；`jvp(x => vmap(pull)(x))` 又撞 `vmap has no rule for 'a constant'`（即 `vmap_grad_f` 那条墙）。
3. **`grad` 求第一个 `with` 参数。** `grad((w: f32[4]) => ...)` 报 `it has no 'with' parameter`；要写成 `with <w: f32[4]> func loss()`。

## 那 4 条的期望值是对的

现成绿用例 `compositions.pullback_of_a_sum_of_squares` 印 `ct=1 sum -> 20.0`，而 `2v = [2,4,6,8]` 求和正是 20。

**所以 `F(w) = sum(2w)`、`∇F = (2,2,2,2)`，而用例里那四个零属于二阶导（`grad` 再 `grad`），这条程序没写。**

**结论：那 4 条的期望值不许改成 2。** 它们今天停在 `vmap takes a function of exactly one parameter` 与 `cannot be called with argument types`——**是写法问题，不是数值问题。** 写法改对之后才走到 `stdlib/vjp.cml#vjp` 里那道墙。