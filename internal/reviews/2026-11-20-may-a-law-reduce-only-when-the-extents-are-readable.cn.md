# 一条法则能不能只在 extents 读得出时才归约？

**2026-11-20 23:05。R4 已按最新原文实现到 `f197fc992`，**未合 `develop`**，因为它让 19 个 pass 掉了。**

## R4 原文（`internal/reviews/questions-2026-10-29.md:6541`，逐字）

> **R4 广播转置两套法则。** `add_stated_t`/`subtract_stated_t`（`stdlib/rulesets/transpose.cml:255` 起）
> 与 `add_tensor_broadcast_t`（:236）是同一条法则，只是形状从操作数还是端口读；
> …**修法**：一条「`sum_to(ct, shape(operand))`」法则覆盖同型与广播，`*_stated` 族删掉；
> **`shape` 读不出就拒，不答 `(ct, ct)`**。

## 按原文实现之后量到的

| 计划 | 基线 | 合并后 |
| --- | --- | --- |
| `feat/compile/transforms` | 203 pass | **193 pass / 12 fail** |
| `feat/transforms/split` | 30 pass | **29** |
| `feat/showcase_s2` | 10 pass | **2** |
| `feat/compile` + `feat/opt` | 4270 / 0 fail / 16 known | **4256 / 16 fail / 14 known** |

**21 条全停在 `skip=law-reads-a-value uri=tensor:add|subtract`。**

## 根因（量出来的，不是推的）

`shape(operand)` 只从**声明写下的 extents** 读（`rule_expand.cpp#answerTheShapeOf` → `statedShapeOf(ports[i])`，读端口类型），
**不从值读**。而反向侧的操作数是 `float[...]` 开秩切向量 ⇒ **读不出** ⇒ 按原文「拒」⇒ 那 19 个 pass 回不来。

**⇒ 丢的正是「前向半边交出的开秩切向量」这条路。** 而它不是新问题：`2e78f29c0` 曾量到同样九项，
**`868464b54` 就是为它加了 `*_stated` 族** —— 也就是说，**R4 要删掉的那个族，当年正是为了解决 R4 撞上的这个洞。**

## 要问的

**一条转置法则能不能写成「extents 读得出就归约；读不出就不动（把 `ct` 原样交回）」？**

也就是说：规则语言里有没有（或该不该有）这样一个分支，让一条法则**按 extents 是否可读**走两条路，
而不是「读不出就拒」。

## 已答（2026-11-20 23:09，`origin/fsh-dev = 1351b3089`，逐字）

> **「Answer: no (ct, ct) fallback; jvp always hands shapes as values; one transpose law per operator」**

**⇒ 三句各自排掉/指定一条路：**

| 句 | 意思 |
| --- | --- |
| **no `(ct, ct)` fallback** | 兜底禁止（原文本来就禁） |
| **`jvp` always hands shapes as values** | **形状拿得到 —— 问题是 `jvp` 没把它当值交出去** |
| **one transpose law per operator** | 一个算子一条法则，不是三支 |

**⇒ 所以下面那份「默认建议」是**错的**，错法很具体：**它把问题放错了层** ——
我以为缺的是**语言能力**（在法则语言里加一个 extents 可读性分支），
实际缺的是**生产者没交信息**（`jvp` 把形状当值交出去，extents 就读得出了）。
**保留原文在下面，是为了让下一个人看见这个错是怎么犯的；不要照它做。**

**⇒ 而这与另一条墙同形：`split` 那条也是「`jvp` 没写 `tangentArgs`」，两条都在 `transpose` 那侧暴露。
⇒ **「墙在 `transpose`」这个读法本身是错的 —— 墙在 `jvp` 与 `transpose` 的接口上。**

## 默认建议（**已被上面的答案否掉，留作记录，不要照做**）

**「能」。** 三条理由：

1. **`*_stated` 族的存在本身证明这条需求是真的** —— 当年只能靠**多加一个算子名字**来表达「形状已在别处写下」，
   而两个名字表达同一条法则不该是答案（这正是 R4 说的「两套法则」）。
2. **「读不出就拒」在**前向**是对的、在**反向**是错的** —— 前向半边的操作数是声明过的 `x`、`w`，extents 读得出；
   反向半边的操作数是 `float[...]` 切向量，它**本来就没有声明形状**，拒它等于说「切向量不能参与加法」。
   **⇒ 这两条路不是一个法则的两种情形，是两种入口。**
3. **不这么做的代价是**二选一**：要么认这 19 个 pass 永久红，要么 `*_stated` 族永久留着 ——
   而后者正是 R4 要消除的「两套法则」。**两条都不是收敛。**

## 若批「能」

```
**`*_stated` 族删掉，`f197fc992` 那 19 个 pass 回来；落点：法则语言加一个「extents 可读性」分支，
`transpose.cml` 里 `add`/`subtract` 各一条法则，走三支：同型有 extents ⇒ 恒等；异型有 extents ⇒ 各自 `sum_to`；
读不出 ⇒ `(ct, ct)` 不动（**这一支是「不动」，不是「兜底答一个错的形状」**）。**
```

## 若批「不能」

```
**R4 这一条就地撤回，本页记「不可执行及原因」；`fix/r4-pinned-ledger`（`f197fc992`）保留作证据
（那 19 个 pass 的清单与 `skip=law-reads-a-value` 的读数都在里面）。**
```

## 同时记下的两件事（不需要裁定，只是别丢）

- **`f197fc992` 的判据 ⑤ 没改 `test/plans/**`**：`transforms.plan.toml:3359` 仍钉 `rhs=…subtract_stated_t`，
  钉的正是被删的那一套。**「不为了绿灯去改期望」这一条它守住了。**
- **它本可以留 `(ct, ct)` 兜底让 19 个 pass 立刻回来，而没有留** —— 因为原文明确禁止。
  **⇒ 这是今晚第一次，一条车道为了让判据诚实而选择让读数难看。**
## 决策者答（2026-11-20 23:15）

**默认不批**；R4 的原文也要改，错在我把"读不出就拒"写成了唯一出路。
- **"读不出就把 `(ct, ct)` 原样交回"不行。** 那正是会丢形状的那条路：广播下 `w` 的余切形状是 `x+w` 的形状而不是 `w` 的，交回 `ct` 就是一个形状错的梯度，静默地错。"不动"在转置里不是中性答案。
- **根因在入口而不在法则**：反向侧拿到的是线性输入，它的 extents 只能来自前向半边的原值。`*_stated` 族之所以能用，是因为 `jvp` 把 `shape(x)`、`shape(w)` 作为**值**交给了线性图。所以要收敛的方向是**只留这一种形式**，而不是两套并存或加一个"可读性分支"。
**改后的 R4**：
1. `jvp` 对会广播的二元算子（`add`、`subtract`、`mul`）**总是**产出带形状的形式（形状是前向半边算出的值），不再产出不带形状、靠类型读 extents 的那一种。
2. 转置侧每个算子只留一条法则：`(sum_to(ct, ax), sum_to(ct, aw))`（`subtract` 取 `-ct`）。同型时 `sum_to` 是恒等，静态 extents 已知时由优化折掉，不在法则里分支。
3. `add_tensor_t`、`add_tensor_broadcast_t`、`subtract_broadcast_t` 删掉。线性图里若出现不是 `jvp` 产出、又读不出 extents 的 `add`，**按名字拒绝**。
4. 名字问题另算：`*_stated` 这个后缀只是写法，保留或改名（例如 `add_shaped`）随你，但一个算子只有一个转置入口。
验收：`f197fc992` 掉的 19 个回来，`transforms.plan.toml:3359` 按新法则名重钉（换名不是放松），`showcase_s2` 回到 10。
