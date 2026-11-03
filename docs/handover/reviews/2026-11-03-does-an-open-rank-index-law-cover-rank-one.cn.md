# 下标的律：秩一与开秩要的答案类型不同，一条写在 `T[..]` 上的律覆盖不了两者

## 问什么

`:op/idx_arr` 是一个算子，而设计原则 5.4 让它的操作数有三种写法：`f32[]`、`f32[3]`、`f32[..]`。今天是
两条律（`jvp.cml#jvp_idx_arr`、`transpose.cml#idx_arr_t`）都写在 `T[]` 上，`ArrayType::assignableFrom`
也只允许 `T[..]` 接住 `T[]`（不放行反方向）。于是开秩实参被按名拒绝：

```
skip=rule uri=:op/idx_arr reason=the rule takes (float[], int), and the operator has (float[..], int) (of which 2 is a data operand)
```

要裁定的是：在"一个键一条规则"（`rulesets_duplicate_head_refused` 那道闸按 `when` 文本判重）下，
怎样让同一个算子既答秩一又答开秩，而**不改** `assignableFrom` 的方向。

## 已量到的：把形参从 `T[]` 写成 `T[..]` 不成立（2026-11-03，实测）

试过的写法（两条律各改一处，外加右边形参与一处私有声明）：

```camel
func index_into::<T>(x: T[..], i: int): T[..] { return x[i] }
export rule jvp_idx_arr::<T>(xs: T[..], i: int) {
    index_into(xs, i) => (dxs: T[..]) => index_into(dxs, i)
}
```

**秩一被弄坏了**。同一个程序 `pick1(t: float[]): float { return t[0] }`，`t=[1,2,3]`、`dt=[6,7,8]`：

| 律的形参 | 读数 |
| --- | --- |
| `T[]`（改前） | `OPENRANK b_rank1 pick1 value=1.0 tangent=6.0` |
| `T[..]`（改后） | `OPENRANK b_rank1 pick1 value=1.0 tangent=6.9084636812749e-310` |

`tangent` 那一位变成了一个数组槽的位被当 `float` 读出来。原因是律的形参类型同时决定了左边式的**结果
类型**：`index_into` 在 `T[..]` 上答 `T[..]`，`T` 绑成 `float` 时切线就成了 `float[..]`，而秩一节点的
切空间是 `float`。5.4 的读数正是这个差别：`t: f32[]` 的 `t[0]` 是 `f32`，`t: f32[..]` 的 `t[0]` 是
`f32[..]`。**两种形状要的不是同一条律的参数，是不同结果的类型。**

**开秩也没通**，只是换了个拒绝的理由（`patch=值` 拦在建图之后）：

```
REINTERP jvp function=pick skip=value
error[RuntimeDiag::RuntimeError]: transforms: jvp has no forward transform for this function.
```

`transpose` 一侧同样两条都坏，读数在下面"读数"。另外开秩经由 `vjp`/`transpose` 时根本走不到规则：

```
error[SemanticDiag::ArgumentsRejected]: Invalid call to 'transpose' with argument types <> ((float[..]) => float[..] pure):
`transpose` takes a function that returns a `float`: the cotangent of the result is what goes in.
```

## 不知道的

- 规则的返回类型能不能按**操作数的秩**特化（秩多态：`ElementOf` 给了元素，5.6 没有给"少一维"）。
- `portsTakeOperands`（`modules/transforms/jvp.cpp:131-186`）能不能对切线那一位改按**节点自己的切空间
  类型**去比，而不是按律里写下的类型。
- `vmap` 一族**不需要**同样的改动，且这一点已经量到（下节 d）：它按**元素**类型实例化规则
  （`modules/transforms/vmap.cpp:325` 的 `ruleElementTypeOf(operandType)`），所以 `T[]` 在那里是"元素的
  数组"、正好是那个批的拼法；把 `vmap_rule_idx` 也写成 `T[..]` 会把批从 `Lift<T[]>` 改成 `Lift<T[..]>`，
  秩一批的读数就不再是今天这个值 —— 那是把一个能用的拼法换掉，不是放宽。

## 默认（按此往下做）

1. **两条秩一律保持原样**（`T[]`），不为了过一条探针把秩一弄成错数 —— "拒绝比错数好"。
2. 缺口留在本页；下一次要动的是**路 1**：给规则加秩多态（返回类型按操作数秩特化），那要动
   5.4/5.5/9.11 与匹配层，是一条语言变更，不是一处拼写。**路 2**（切线形参按节点切空间类型比）
   更小，但它改的是 `portsTakeOperands` 与规则绑定的契约，要作者先裁定"律的形参类型是声明还是槽"。
3. 在路 1 或路 2 落下之前，开秩下标仍然按名拒绝；这条拒绝是对的，不是缺陷。

## 缺口（今天红在哪）

- 开秩参数的求导：`f(t: float[..]) = t[0]`（本页探针 `PROBE-openrank-a`）。
- 开秩的转置：`vjp`/`transpose` 在**入口**就拒绝返回开秩的函数，早于规则。

## 读数（本机不构建，全部走主机 88zjwei，树 `~/camel-work/openidx`）

探针在 `D:\Temp\PROBE-openrank\`，主机上 `/tmp/PROBE-openrank/`；"改前"用 `CAMEL_STD_LIB=/tmp/stdc-before`
（同一二进制，只换回两条律的原文）量，所以两组读数是同一个可执行文件给的：

| 探针 | 改前（`T[]`） | 改后（`T[..]`） |
| --- | --- | --- |
| a 开秩 jvp `t[0]` | `skip=rule uri=:op/idx_arr reason=the rule takes (float[], int), and the operator has (float[..], int)` | `skip=value` → `jvp has no forward transform` |
| b 秩一 jvp `t[0]` | `value=1.0 tangent=6.0` | `value=1.0 tangent=6.9084636812749e-310` |
| c 秩一转置 `xs[1]` | `vjp value=2.0`，切线图 `split function=jvp_of_g` 建好 | `TRANSPOSE function=rulesets.jvp::index_into<float> skip=signature` → `transpose has no transpose for this function` |
| e 开秩转置 | 入口拒绝（`transpose takes a function that returns a float`） | 同上 |
| d 开秩 vmap 批 | `asked=3 found=3 missing=0 rules=vmap_rule_idx,vmap_rule_idx,vmap_rule_add`，值 `[3.0, 9.0]`（与逐行 `1+2`、`4+5` 相同） | **未改**：`vmap_rule_idx` 按元素实例化，已经接得住，见上节 |

d 的入口还多一个读数：`float[..]` 进 `vmap` 的 lift 要在调用处显式转换（
`argument 1 ('o') has type 'float[..]', which is less exact than 'float[?, ..]'`），转换之后三个规则全找到。