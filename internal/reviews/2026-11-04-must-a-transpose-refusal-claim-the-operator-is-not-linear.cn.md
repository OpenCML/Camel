# `transpose` 拒绝时，是否必须声称「第 k 个参数不是线性的」？

2026-11-04，`lane-powzero`。作者在 `d79408e45`（06:55）裁定：裸 `transpose` 遇到非线性算子作用在
线性输入上要按语言规则拒绝，句子点名算子与参数、并指向 `vjp`。前提是**走查知道「这个算子非线性」**。

## 这个事实今天拿不到

作者那句话的形状是「`tensor:log` 对它的第 k 个参数不是线性的」。要说出它，走查得先知道
`tensor:log` 是**非线性的**。今天的两个可能的来源，读源码即可判定，不需要跑：

- **「有没有前向规则」**：`src/core/forward.cpp#forwardRulesOf` 是 `rulesIn("rulesets.jvp", uri)`，
  而 `rulesIn` 走的是 `camel::parse::ast::lookupRules(family, head, arity)` ——**按家族名查一个索引**。
  一个家族只有**被构建进这个程序**才在索引里；这正是同一个拒绝路径上 `no-such-family` 那支的含义
  （程序没 import `rulesets.transpose` 时，`transposeRuleOf` 就答 none、lookup 记 `no-such-family`）。
  裸 `transpose(f)` 的程序只 `import rulesets.transpose`，**`rulesets.jvp` 不在里面**，所以这个接口
  在**那个程序里**答空。
- **「家族没有这条律」**：这是同一个索引的另一面，`candidates.empty()` 已经知道，而且**这个事实是可靠的**。

所以在裸 `transpose` 程序里，我们**能证明**的只有：**`rulesets.transpose` 的索引里没有这个算子的律**。
我们**不能证明**：**它对第 k 个参数不是线性的**（要证明它，得看前向规则——而前向规则不在程序里）。

## 默认（下一轮就照它写，不再问）

**能证明才说，证明不了就只说三件真话，并指向 `vjp`：**

> `a step of <graph> (node N, operator `tensor:log`) is one the transpose family `rulesets.transpose`
> cannot turn around: the family holds no rule for `tensor:log`; `transpose` is for linear functions,
> so take the gradient with `vjp`.`

三件都是事实：**哪个算子**、**家族里没有它的律**、**该改用哪条路**。

**为什么这比硬凑一句好**：一句错的解释比一句没有解释更坏——用户会照「非线性」去推理，而那条推理的
依据不存在；而「没有这条律」恰恰是**用户能行动的**（去写律，或改用 `vjp`），「非线性」不是。

**什么时候可以升级成作者那句**：当走查能读到那个程序的**前向规则索引**（为 `transpose` 也把
`rulesets.jvp` 的索引装进来，或从 `rule_expand.cpp` 的规则索引按家族名查而不按已构建家族查）。
那不是这一笔的事，也不该由这一笔顺手改——它是「哪些索引在哪种程序里可用」的一次决定。