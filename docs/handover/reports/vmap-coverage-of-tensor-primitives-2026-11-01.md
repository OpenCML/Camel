# `tensor:*` 原语的 `vmap` 批规则覆盖表（2026-11-01，`lane-m5`）

**尺子**：每个原语一个程序，形状固定为

```cangg
import rulesets.vmap
import { <原语> } from tensor
import { vmap } from transforms

func f(x: float[2, 3]): <返回类型> {
    return <探测体>
}

func main(): int sync {
    let xs: float[2, 2, 3] = [[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]], [[1.0, 1.0, 1.0], [2.0, 2.0, 2.0]]]
    println(vmap(f)(xs))
    return 0
}
```

跑 `camel <case> std::nvm`，读两条里的一条：答了就是 `REINTERP vmap function=f graph=… asked=A found=F missing=M rules=… elements=…`，
没答就是 `REINTERP vmap function=f skip=body construct=- uri=tensor:X asked=1 found=0 missing=1`
加一句诊断原文。行/批的秩、元素类型按原语的声明调整（`f32` 行给 `softmax` 族、`int` 行给 `one_hot` 的标签），
每行的「探测体」就是那一次用的表达式，照抄可复现。**共 52 个程序，52 行读数，一个原语不漏**
（其中 4 个是整轴形态补上之后才量的：`max`、`min`、`argmax`、`argmin`）。

**读数形状**（全表同一把尺子）：`asked` 是走到的原语数，`found` 是被规则集答了的数，`missing` 是差；
`elements=` 是实例按哪个元素类型造的。

## 一、有批规则的原语（13 条）

| 原语 | 探测体 | 整轴形态 | 批规则 | 读数 |
|---|---|---|---|---|
| `tensor:sum` | `sum(x)` | **有**（`sum(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_sum elements=float` |
| `tensor:mean` | `mean(x)` | **有**（`mean(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_mean elements=float` |
| `tensor:max` | `max(x)` | **有**（`max(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_max elements=float` |
| `tensor:min` | `min(x)` | **有**（`min(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_min elements=float` |
| `tensor:argmax` | `argmax(x)` | **有**（`argmax(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_argmax elements=float` |
| `tensor:argmin` | `argmin(x)` | **有**（`argmin(t)`） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_argmin elements=float` |
| `tensor:sum_axis` | `sum_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_sum_axis elements=float` |
| `tensor:mean_axis` | `mean_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_mean_axis elements=float` |
| `tensor:max_axis` | `max_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_max_axis elements=float` |
| `tensor:min_axis` | `min_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_min_axis elements=float` |
| `tensor:argmax_axis` | `argmax_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_argmax_axis elements=float` |
| `tensor:argmin_axis` | `argmin_axis(x, 0, false)` | 无（沿轴） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_argmin_axis elements=float` |
| `tensor:matmul` | `matmul(x, w)`（`w: float[3, 2]`） | —（二元） | 有 | `asked=1 found=1 missing=0 rules=vmap_rule_matmul elements=float` |

## 二、没有批规则的原语（39 条）

这些行的读数**全部**是同一形状，只换 URI 与诊断原文：

```
REINTERP vmap function=f skip=body construct=- uri=tensor:<名字> asked=1 found=0 missing=1
transforms: vmap has no rule for `tensor:<名字>` (the operator has no batching rule key bound at all)
```

| 类 | 原语 | 探测体 | 整轴形态 |
|---|---|---|---|
| 创建 | `tensor:zeros` | `zeros::<float>([2, 2])` | — |
| 创建 | `tensor:ones` | `ones::<float>([2, 2])` | — |
| 创建 | `tensor:full` | `full([2, 2], 1.0)` | — |
| 创建 | `tensor:eye` | `eye::<float>(2)` | — |
| 二元 | `tensor:maximum` | `maximum(x, 1.0)` | — |
| 二元 | `tensor:minimum` | `minimum(x, 1.0)` | — |
| 选择 | `tensor:where` | `where(c, x, 0.0)`（`c: bool[2, 3]` 常量） | — |
| 一元 | `tensor:abs` | `abs(x)` | — |
| 一元 | `tensor:exp` | `exp(x)` | — |
| 一元 | `tensor:log` | `log(x)` | — |
| 一元 | `tensor:sqrt` | `sqrt(x)` | — |
| 一元 | `tensor:rsqrt` | `rsqrt(x)` | — |
| 一元 | `tensor:sigmoid` | `sigmoid(x)` | — |
| 一元 | `tensor:tanh` | `tanh(x)` | — |
| 一元 | `tensor:relu` | `relu(x)` | — |
| 一元 | `tensor:gelu` | `gelu(x)` | — |
| 一元 | `tensor:erf` | `erf(x)` | — |
| 一元 | `tensor:erfc` | `erfc(x)` | — |
| 布局 | `tensor:reshape` | `reshape<[6]>(x)` | — |
| 布局 | `tensor:unsqueeze` | `unsqueeze<0>(x)` | — |
| 布局 | `tensor:transpose` | `transpose(x)` | — |
| 布局 | `tensor:permute` | `permute<[1, 0]>(x)` | — |
| 布局 | `tensor:concat` | `concat<0>(x, x)` | — |
| 布局 | `tensor:take` | `take(x, [0, 1])` | — |
| 布局 | `tensor:one_hot` | `one_hot(x, 3)`（`x: int[2]`） | — |
| like | `tensor:zeros_like` | `zeros_like(x)` | — |
| like | `tensor:ones_like` | `ones_like(x)` | — |
| nn | `tensor:softmax` | `softmax<0>(x)`（`x: f32[2, 3]`） | — |
| nn | `tensor:log_softmax` | `log_softmax<0>(x)`（`x: f32[2, 3]`） | — |
| nn | `tensor:softmax_grad` | `softmax_grad(x, x, 0)`（`x: f32[2, 3]`） | — |
| nn | `tensor:layer_norm` | `layer_norm(x, x, x)`（`x: f32[2, 3]`） | — |
| nn | `tensor:linear` | `linear(x, w)`（`x: f32[2, 3]`，`w: f32[3, 2]`） | — |
| 折类型 | `tensor:shape` | `float(len(shape(x)))` | — |
| 折类型 | `tensor:numel` | `float(numel(x))` | — |
| 折类型 | `tensor:dim` | `float(dim<0>(x))` | — |
| 字节 | `tensor:to_bytes` | `to_bytes(x)` | — |

**不在这张表里的四个名字**：`show::<A>(t: A[..]): void`、`num_threads(): int`、`set_num_threads(n: int): void`、
`gemm_backend(): string`（`stdlib/tensor.cml:102/109/114/120`）。它们不是「把一个张量变成另一个张量」的原语
（一个打印、两个是模块设置、一个回后端名），`vmap` 对它们的翻法不是「批」，所以没量；谁要批 `show`，那是另一句话
（打印一次还是每行一次），不是规则缺。

**三条不是这个形状，单独列出**：

| 原语 | 探测体 | 实测原文 |
|---|---|---|
| `tensor:save_npy` | `save_npy(x, "coverage.npy")` | "vmap has no rule for `a constant` (a constant of a type this transform has no batch for)" —— 卡在**路径字符串常量**上，还没走到算子 |
| `tensor:load_npy_*` | `load_npy::<float>("coverage.npy")` | 同上，同一句：先说常量不能上批 |
| `tensor:from_bytes_*` | `from_bytes::<float>(to_bytes(x), [2, 3])` | `uri=tensor:to_bytes asked=1 found=0 missing=1` 加 "no batching rule key bound at all" —— 它先卡在自己的实参上 |

**上表里 `tensor:one_hot` 那一行曾经写不出来**：第一版探测时它在批里**编译不过**，原文是
`Return type 'f32[2, 3]' does not match the function's declared return type 'int[..]'`——那不是批的问题，
是**声明与算子自己不一致**：`stdlib/signatures/tensor.cml` 说回 `int64[..]`，而算子那侧
（`modules/tensor/ops/layout_ops.cpp:443` 的注释、`oneHotKernel`、`oneHotInfer`、OpDef 的 `resultDoc = "float[..]"`、
`setVjp(defs, "one_hot", &noGradient)` 上那句 "the float one-hot rows"）全都说 **f32**。
照现有用例判，f32 那一侧是多数也是语义：`test/plans/feat/modules/tensor.plan.toml` 的 `take_one_hot` 钉着
`one_hot=[[0.0, 1.0, 0.0], [1.0, 0.0, 0.0]]`，`discrete_zero` 钉着 `one_hot=(-2.0,0.0,2.0)`，
`softmax_labels` 把它与 `f32` logits 一起用。所以改的是**声明**（`f32[..]`，正文与 `@example` 一起），
算子一个字节没动；改完这一行就是上表的 `uri=tensor:one_hot asked=1 found=0 missing=1`：
`one_hot` 也是「键没绑」，不是「规则缺」。

**顺带记一句**：这条错声明在树上活了很久，因为**没有任何检查看声明的结果类型**——
`registry.signatures_match`（`stdlib-reference.mjs --verify-ops`）比的是名字、参数个数、参数名与纯度，
结果类型只由生成页带出去。要防它再来一次，得给 `--verify-ops` 加一条「声明的结果类型与算子的 `resultDoc` 一致」。

**曾经单独量的两条：`argmax` / `argmin`（已补规则，见下面那段）**

这一版先量到的是「有整轴形态、没有批规则」：`uri=tensor:argmax asked=1 found=0 missing=1` 加
`the operator has no batching rule key bound at all`。当时的判断是「写不出规则」，理由是**没试过**就推出来的：
页里只量了「形状写在圆括号里会被拒」，据此推「形状必须是编译期常量」，**这一步没有试**。作者在 `f5a450815` 里
实测推翻了它，并给了现成的写法（见第一节那两行与第三节）。方法上的教训记在这里：**凡是说「写不出来」，
先把想到的写法都实测一遍，贴上原文**。

## 三、整轴形态：四个名字都补上了（`5fd6fd902` 的裁定之后）

最初量这一列时，`sum` 与 `mean` 有一实参的整轴声明，`max` / `min` / `argmax` / `argmin` **没有**：
`stdlib/signatures/tensor.cml` 里它们只写了带 `axis` 的那一条，`modules/tensor/ops/reduction_ops.cpp` 里也只有
`reduceAxisDef<Max>("max_axis", "max")` 这样的沿轴注册，写 `max(x)` 的原文是：

```
No matching function found for argument types: <> (float[2, 3]).
```

作者按 `docs/design-principles.cn.md:548`（"整体归约与沿轴归约同名……沿一个轴的形式带 `with <axis, keepdims>`，
在同一个名字之下"）裁定：这四条**本来就该能写**，今天写不出来是实现落后于原则，不是语言里没有这个东西；
于是四种整轴形态补齐（`OpDef` + kernel + 类型推导 + 签名 + `vjp` + 文档），实测读数：

| 原语 | 读数 | 语义读数 |
|---|---|---|
| `max` | `max([[1,2,3],[4,5,6],[-1,0,1]] 行)` = `3.0` 每行 | NaN 传播、`-0 < +0`（IEEE 754-2019 `maximum`） |
| `min` | 同形 = `1.0` 每行 | 同一条 `minimum` |
| `argmax` | `argmax([[4,1,6],[3,2,5]])` = `2` | 行优先展开的位置；并列取最小；有 NaN 取第一个 NaN 的位置 |
| `argmin` | 同形 = `4` | 同形 |

四条在空张量上**都是故障**，不是拒绝编译，也不是答案（`design-principles.cn.md:575`：`mean`、`min`、`max`、
`argmax`、`argmin` 没有单位元），种类 `InvalidArgument`，原文（`attempt` 隔离后打印，四个名字各一句）：

```
error InvalidArgument: Invalid argument of 'max': a tensor with no element has no maximum.
error InvalidArgument: Invalid argument of 'min': a tensor with no element has no minimum.
error InvalidArgument: Invalid argument of 'argmax': a tensor with no element has no maximum.
error InvalidArgument: Invalid argument of 'argmin': a tensor with no element has no minimum.
```

批规则与整轴形态是两件事：`max` / `min` 的整轴规则折掉行后面的每个轴；`argmax` / `argmin` 的答案是**行优先
位置**，折轴给不出它，所以它们的规则是**把批读成一条线**——`reshape<[shape(xs)[0], -1]>(xs)` 把一行的元素放到
最后一个轴上，再 `argmax_axis` 沿那个轴数（作者在 `f5a450815` 里给的写法，与本车道最初那页的结论相反；
那一页的前提没试过，见第二节末尾）。读数 `asked=1 found=1 missing=0 rules=vmap_rule_argmax elements=float`
（`argmin` 同形），`float[3]` 行 `[1,2,3] / [4,9,6] / [7,8,5]` 答 `[2, 1, 1]` 与 `[0, 0, 2]`，与逐行 `array.vmap`
逐位相等。补形态的原始提议页（`docs/handover/reviews/questions-2026-11-01-whether-a-reduction-can-be-written-without-an-axis.md`）
里那条"默认不补"的建议被作者否掉了，那一页留着作记录。

顺带把 `reshape` 的写法量了一遍（作者提醒之后补的，原文各一行）：`reshape<[n, -1]>(b)` 与
`reshape<[shape(b)[0], -1]>(b)` 都得 `[2, 3]`，`reshape<[-1]>(b)` 得 `[6]`，`reshape<[n, shape(b)[1]]>(b)` 得
`[2, 3]`——**尖括号里可以写值，`-1` 是「值自己说的长度」**。

## 四、这张表回答了什么

- **M5 覆盖完了没有**：`tensor:*` 里**只有 13 条**有批规则（`sum`、`mean`、`max`、`min`、`argmax`、`argmin`、
  四条沿轴归约、`matmul`），其余 39 条一条都没有，而且是**键都没绑**（不是"绑了键、规则集里缺一条"）——
  诊断原文分得清：没键是 `the operator has no batching rule key bound at all`，有键缺规则才是
  `the ruleset holds no rule for this operator and operand count`。今天 `tensor:*` 上**没有一条**落进后者。
- 顺带的一条：整轴归约这一组现在**六条同形**，四折两reshape——`argmax`/`argmin` 的规则不是折，是「批读成一条线」。