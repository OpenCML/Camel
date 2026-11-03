# 直线路径要不要像分支臂那样，把抬升调用穿过原函数转置

**一句话**：库层把 `with_grad` 写成组合之后，后一半里留下一个**抬升调用的直呼**（`callee=jvp_of_weighted`），转置走到直线路径时按名拒绝；而**同一份 `transpose.cpp` 的分支臂路径已经有一条规则**处理同一件事。**两条路径对同一件事给出不同答案，这本身就是缺陷。**

## 实测（`lane-withgrad`，`036960b18`，基 `ce2691f54` + `66a496134`）

前一步把 `split` 那面墙拆了（后一半读到的参数现在必然进残差清单，残差收 `p0`，前一半给调用放 2 个实参：普通 1、上下文 1），于是墙挪到了 `transpose`：

```
transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around
TRANSPOSE function=jvp_of_weighted skip=signature
TRANSPOSE function=split_of_jvp_of__g_247__backward skip=call-no-transpose node=5 callee=jvp_of_weighted
```

而分支臂路径上已有的规则，注释原话是：

> A lifted call is transposed **through the graph it was lifted from**

机制：`callee->liftedFrom()`。直线路径没有用它，仍然要求被调方自己有转置；`jvp_of_weighted` 的签名（返回对）没有转置，于是拒绝。

## 要裁的那一句

**直线路径要不要补上分支臂已有的那条规则？还是库层的组合不该让后一半留下一个抬升调用的直呼？**

- **甲：直线路径补同一条规则。** 根据是"同一份代码里两条路径对同一件事给出不同答案"——分支臂能做到的，直线路径没有理由做不到。
- **乙：库层的组合不该产生这个直呼**（后一半不该直接调抬升函数）。这会改库层组合的形状，比甲深。

## 默认建议（施工方默认）

**甲**，但带一条硬约束：**补规则后必须先确认它没有把"明确拒绝"降级成"静默错数"。** 判据就用现成的那两条哨兵——`transpose_loop_escape.two_values_of_two_kinds` 与 `.second_state_of_the_same_loop`，**它们现在是红线（`same=no` / `Runtime indirect call norm-arity mismatch`）**；补规则后若它们变成"给出一个数但不报错"，那就是更坏，立刻回滚。

**代价（写清）**：甲会放宽"被调方签名里没有转置"这一类拒绝，拒绝面变小，因此必须有新用例钉住"真正该拒的形状仍然被拒"——否则我们是用一次放宽换了将来的静默错数。

## 另一条同车道的未决项（一并提请）

`:501`（`split.cpp#makeCopy` 里"后一半读了参数原值、而切点没带走它"的按名拒绝）在拆掉那面墙之后，**本车道构造不出触发的形状**，树上也没有用例钉着它。**没有删除**，理由按严重性次序（对 > 明确拒绝 > 错数 > 崩）：换成断言会把"明确拒绝"降级成"崩"。

三条读数分开记，不要合成一句"不可达"：

1. 本车道改后**构造不出**触发形状；
2. 树上**没有任何用例**钉着它（`git grep` 只在问题页里出现）；
3. 另一条车道在别的形状上插桩，**零进入**。

**"零命中"和"不可达"是两个结论。** 建议：给它加一行注释说明它陈述的不变量（今天由"后一半读到的参数必然进残差清单"保证），并写明"若将来确认对任何输入都不可达，删掉它并把不变量降级为注释"，**但不要在无替代拒绝的情况下删**。
