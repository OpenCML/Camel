# 同一个形状拒绝，在 `jvp` 和 `transpose` 里该不该有同一个名字？

## 现象（实测，2026-11-03，树 `dedupe` 合并 `a17769388` 之后）

一个秩二的索引 `m[0]`（`m: float[2, 2]`），两条路对**同一个形状拒绝**给出**同一句话**，却写着**两个分类名**：

```
jvp:        skip=rule       reason=the rule takes (float[], int), and the operator has (float[2, 2], int)
transpose:  skip=rule-shape reason=the rule takes (float[], int), and the operator has (float[2, 2], int)
```

第一行是**实测**：`D:\Temp\rule_shape_probe\p1.cml`（`sum(m[0])`，`m: float[2, 2]`）交给 `vjp` 跑出来的。
第二行是 **`transpose` 那段代码会写的话**，不是今天的读数——它由
`modules/transforms/transpose.cpp` 里 `RuleRefusal::Shape` 的分支组装（`"the rule takes " + 律的参数表 + ", and the operator has " + 操作数表`），
曾经由 `showcase_s2` 的 `matmul_t.vector_operand_is_refused_by_shape` 钉住；那一版用例在 `s2wall` 的树上被删了（向量×矩阵现在能算），
今天 `transpose` 侧这个分类**没有再被任何用例执行到**（见下节）。两台机器上今天都拿不到第二行的读数，所以这一页不假装它是实测。

两条路的判据是**同一个**：律的参数表能不能接住这个节点的操作数（`Type::assignableFrom`，按 5.4 秩零就是元素本身，所以
`T[..]` 收 `float`）。判据相同、句子相同、**分类名不同**。

## 为什么今天会变成一个问题

`rule` 和 `rule-shape` 是夜里这一笔分开的两个类（`test/plans/feat/showcase_s2.plan.toml`，
`matmul_t.vector_operand_is_refused_by_shape` 钉过它；分类名不能合并的理由是"责任在哪一层"）。分开之后，`transpose` 侧的这个类
**在整棵树上没有可达的用例**：模式里还写着形状的律（`:op/idx_arr` 的 `xs: T[]`、`take` 的 `indices: int[]`）在
`jvp.cml` 里有镜像，而 `jvp` 先问，于是形状拒绝总是先落在 `jvp` 上，`transpose` 根本走不到那个节点。也就是说：**同一机制在两个
family 里叫两个名字，而先被问到的那个用的是老名字**。

## 默认建议：统一成"模式自己拒了就是 `rule-shape`"

**默认：分类命名的是机制，不是谁先问。模式（律的参数表）接不住操作数，就叫 `rule-shape`；键下没有律，才叫 `rule`。`jvp`、
`vmap`、`transpose` 三家同答。**

理由一，今天已经用过一次的：**分类名不是措辞，合两堵墙不等于少一堵**（`423c2bce0` 的提交正文）。反过来也成立——**同一个机制不该有
两个名字**。分类是给下一个读者看"责任在哪一层"的，读者拿到 `skip=rule` 时该判断的是"这里没有律"，不是"哪家先问"。

理由二，可达性：`transpose` 的 `rule-shape` 今天不可达，唯一让它保持可达的办法是 `jvp` 也承认这个词——否则它是一条**无人认领
的分类**，留着就等于留一个不生效的守卫（"留一个不生效的守卫比不留更糟"）。

## 代价（要写清，所以它是一笔独立改动）

1. **一批 golden 会变。** `jvp` 现在对形状拒绝写 `skip=rule`，改用 `rule-shape` 之后，凡是钉了那一行 `skip=rule` 的用例都要改
   读数；`feat/compile` 里已知会动的至少是索引、`take` 与 `mean_axis` 那几条（`transforms.plan.toml` 的 `rules_angle_args`
   用例的 `known_fail_msg` 就是 `TRANSPOSE function=loss_mean skip=arity uri=tensor:mean_axis operands=3` 同一族的话）。
2. **"`jvp` 先问"这件事会从读数上消失。** 今天一条 `skip=rule uri=:op/idx_arr` 顺带说明了"这个形状在 jvp 就被拦下了"；统一之后
   同一个读数只是分类变了，"哪家先问"要看 `function=` 和走查顺序。这是接受的代价：分类回答的是机制，不是顺序。
3. **`vmap` 一起改。** 它和 `jvp`、`transpose` 共用 `chooseRuleAmong`（`rule_expand.cpp`），只改两家会留下第三个名字。

## 若作者要保留现状

若裁定是"先问者定名"（`jvp` 继续写 `rule`），那么 **`transpose` 侧的 `rule-shape` 就永远是死分类，应当删掉而不是留着**——它今天唯一
的用处是"哪天 `transpose` 真遇到更窄的模式时别把责任写错"，而没有用例钉住的分类等于没人认领。删它要连 `RuleRefusal::Shape`、
`showcase_s2` 里那条注释一起删，并且接受"形状拒绝在 transpose 里退回 `rule`"这个已知错判。