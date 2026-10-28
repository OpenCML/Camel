# 副本体内的递归调用不能改指副本自身

## 我量到的

探针：`std::opt::specialize std::jgir` 对 `test/cases/opt/generic/shape_unroll.cml`；树 `D:\Projects\Camel-wt\h-bisect`，分支 `specialize-self-call`，提交 `89200b09e`，二进制是 `camel-remote.ps1` 在 Linux 主机上从该提交构建的 `out/latest/bin/camel`（Release）。原文的端口按 `parameter.index`、实参按 FUNC 节点的 normal 边槽位读，不靠标签：

```
run        g2  ports 0:t 1:steps 2:x 3:h    FUNC run       -> g2    args(0:t+1 1:steps 2:x 3:h_new)   4 口 4 入
forward     g3  ports 0:x                   FUNC run$spec0 -> g4    args(0:shape(x)[1] 1:x 2:zeros)     3 口 3 入
run$spec0   g4  ports (steps, x, h)         FUNC run       -> g2    args(0:t+1 1:steps 2:x 3:h_new)   3 口 4 入
```

`run$spec0` 少的那一个是 `t`：`forward` 按 `run(0, shape(x)[1], x, zeros(…))` 调用，`t` 被按值绑成 `0`、形参从副本里删掉，副本只剩 3 个端口；可副本体内那条递归调用仍照基函数的样子传 **4 个**实参，其中第一个就是被绑掉的那一位的下一层取值 `t+1`。

所以"副本体里的递归调用指向基函数"这条观察成立（`run$spec0 -> run`），但它在端口上还带着被 `eraseFormalFromDraft` 删掉的那一位。

## 按裁定只改 callee，两处硬报错

在提交（`draft.encode`）之前把该副本里所有 `callee == baseGraph` 的 FUNC 节点改成 `draft.setCalleeGraph(id, draft.id())`，构建通过，跑探针即报第一处：

```
():?:?: [Error]: Pass 'std::opt::specialize' failed with an internal error:
  FUNC node 23 calls a graph that is not among the graphs this graph calls (its reference table).
```

自引用的图 id 得先进图自己的引用表（基函数 `run` 表里就有 `g2`，因为它本来就调用自己）。补上 `draft.addDependency(draft.id())` 之后立刻撞第二处，也就是运行时自己的校验：

```
():?:?: [Error]: Pass 'std::opt::specialize' failed with an internal error:
  Runtime graph 'run$spec0' breaks its edge invariants (1 violation(s)):
  graph 'run$spec0' node 23 (10): passes a int as ordinary argument 1 of 'run$spec0', which takes a f32[..]
```

这条来自 `src/runtime/graph.cpp#validateRuntimeGraphCalls`：FUNC 节点传的实参数必须等于被调图的形参端口数。4 入 3 口，它是"副本指向自己"这一步的直接后果，不是补引用表能绕过的。

## 为什么不能"连实参一起改"

把被绑定那一位置的实参也删掉，调用就变成 `run$spec0(steps, x, h_new)`——端口对得上了，但副本会以它绑定的 `t = 0` 再进一次，而它该进的是 `t = 1` 的那一层：`run$spec0` 会把同一层的 `h + slice(x, 1, 0, 1)` 反复加下去，永远到不了 `t >= steps`。**这是错语义，不是外观**。

而且副本一旦带上自调用就落进了环：第 1 笔把"环里的 callee 不复制"作为门，内联器就会拒绝把 `run$spec0` 内联进 `forward`。于是 `shape_unroll.simplify.rgir` 的"`forward` 只调一个 `run$` 实例"会变绿，而程序的值是错的——绿在断言上、错在语义上，正是不能要的那种绿。

## 那条红其实来自内联，不是副本的 callee

单跑 `std::opt::specialize` 时，`forward` 的 FUNC 已经是 `run$spec0`（3 口 3 入，与副本端口一致），本来就满足"`forward` 调的是递归的一个实例"。整轮 `std::opt` 把 `run$spec0` 内联进 `forward` 之后，`forward` 里剩下的才是副本体内那条去往通用 `run` 的调用（4 入 4 口，落在 `forward` 里），断言就是在这一步红的。

也就是说：**让副本的 callee 自引用**与**让内联不要把调用目标退化成通用函数**，是两件事；这一处的症状由后者产生。

## 要问的一句

三选一：

- **(a)** 只在"调用实参与副本自己的形参完全对得上"（端口数相等、类型可接受，即同一个 key、没有形参被按值绑掉）时把 callee 改指向副本，其余保持指向基函数。安全，实现有先例（同文件里直接 FUNC 特化那一支就是这么判的），但 `shape_unroll` 这条不绿。
- **(b)** 连实参一起改写，删掉被绑定位。需先确认语义：本页量到的这个副本会因此以 `t = 0` 重入。
- **(c)** 改内联侧：拼接副本时保留副本体内的调用目标，别把它退化成通用 `run`。

三条都是语义决定，我不动；代码停在第 1 笔 `89200b09e`，全量读数 7476 / `fail 7`（与基线同名）/ `snapshot differs from golden` 0 条 / `-Check all` 8 项 0 失败。