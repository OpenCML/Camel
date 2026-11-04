# 问题：`rulesets.jvp` 要不要给四个"stated"拼写补前向律？

**日期**：2026-11-04
**状态**：**待作者裁**（本车道按第 4 节默认取乙继续）。记录原因：`lane-vjpparity` 补上了引擎侧三条 `setVjp`，补完之后这两条腿对这四个名字的状态变成了**交叉**的——引擎答、库层不答——这是之前没有的形状，必须留一页。

## 1. 读数（本车道树，`origin/develop` 34b6591）

两张反向表的逐键对比（`node scripts/vjp-parity.mjs --tables`，抽取方法写在
`scripts/vjp-parity.mjs` 的头注释里；库层 56 条律归到 29 个键，引擎层 113 条注册归到 101 个键）：

| 状态 | 数量 | 键 |
|---|---|---|
| 两条都答 | 28 | `+@2` `-@2` `*@2` `/@2` `-@1` `^@2` `float@1` `f32@1` `f16@1` `bf16@1` `add@2` `mul_tensor@2` `relu@1` `tanh@1` `exp@1` `sigmoid@1` `log_softmax@1` `subtract@2` `matmul@2` `index_into@2` `sum@1` `mean_axis@3` `reshape@1` `take@2` `softmax@1`，加上本车道补的三条 `sum_stated@2` `take_stated@2` `mean_axis_stated@2` |
| 只有库层有律 | 4 → **1** | `pick_at@2`（`lane-pickatvjp` 已结清，未合入）、**`sum_stated@2`、`take_stated@2`、`mean_axis_stated@2`（本车道补齐）** |
| 只有引擎有规则 | 73 | 见 `scripts/vjp-parity-exemptions.tsv`，逐条带 owner 与理由 |

补之前，`grad` 对这三个名字**拒**（实测，`test/plans/feat/compile/stated_extents.plan.toml`，红读数）：

```
test/cases/compile/transforms/stated_sum_answered_by_the_engine.cml:31:12: [Error]:
    autodiff: no derivative rule for operator 'tensor:sum_stated' ...
```

`take_stated` 与 `mean_axis_stated` 是同一条句子换名字。补之后同三个用例 exit 0，两条腿的数相同
（`engine dL/dw=…` 与 `library dL/dx=…` 印在同一行）。

## 2. 这是个新形状：两条腿对这四个名字**交叉**了

补之前：`grad` 拒（引擎表没行），`vjp` 对**直接写这四个名字**的程序也拒——`stdlib/rulesets/jvp.cml`
里没有一条左值是 `sum_stated`／`take_stated`／`mean_axis_stated`／`pick_at` 的律（这正是
`--tables` 把它们列成"只有库层有律"的原因：律在 `transpose.cml` 里，前向半边没有配对的那一条）。
两条腿都拒，**一致地拒**。

补之后：`grad` 答，`vjp` 仍拒。**一致地拒变成了一边答一边拒**，而且方向与 `lane-pickatvjp` 记录的
那次相反（那次是 `vjp` 答、`grad` 拒）。原因不是谁写错了，而是这四个名字的**前向律**从来没被要求过：
它们本是前向半边的**线性半**发出来的节点（`jvp_sum` 的切向调用就是 `sum_stated`，
`jvp_take` 的是 `take_stated`），库层走的是"`sum` → `sum_stated`"这条路，用户直接写 `sum_stated` 是
另一条路，那条路上前向半边先停。

`lane-pickatvjp` 自己在树里加过 `jvp_pick_at`（它的问题页第 1 节这么写），但**没有**进 develop：
`stdlib/rulesets/**` 那次提交一行没动。所以今天四个名字在库层都是"反向有律、前向无律"。

## 3. 三条路

**甲：给四个名字补前向律**（`jvp_sum_stated`、`jvp_take_stated`、`jvp_mean_axis_stated`、`jvp_pick_at`）。
代价：四条律 + 四个用例；四个名字从此在两条腿上都是公开可求导的拼写。这四条律的右值几乎是恒等
（这些都是线性算子：`sum_stated(t, at) => (dt) => sum_stated(dt, at)`），但把它们写对等于承诺
"线性半的拼写也是用户的拼写"，而那正是 `pick_at` 那一页第 2 节乙案当初要否掉的东西。

**乙（默认）：不动。** 这四个名字留在"反向半的拼写"里：库层答它们的路是 `sum`／`take`／`mean_axis`／
`x[i]`，用户写这四个操作，不写它们的 stated 拼写。代价：`grad` 与 `vjp` 对这四个名字**交叉**——
引擎答、库层拒——这本身就是一条账，只是记在库层而不是引擎层；而且对称性检查
（`scripts/vjp-parity.mjs`）**看不见**它：那张表比的是两张**反向**表，前向半边不在它的视野里。

**丙：把四个名字从公开面撤下**（`exports = {}`，像 `expand_axis`），用户只剩 `sum`／`take`／
`mean_axis`／`x[i]`。代价：`stdlib/tensor.cml#sum_stated` 一带四行 `export func` 与它们的 `@example`
要撤、`modules/tensor/module.cpp#kDeclaredOperators` 要撤、参考页要重写；而且前向半边的线性半仍要
发这些节点（规则右值不受出口限制），所以"公开名字"少了四个，算子一个不少。

## 4. 默认（按乙继续）

**本车道不动 `stdlib/rulesets/**`**（它是本次任务的禁区，八条车道在
`stdlib/rulesets/transpose.cml` 上撞过），只补引擎侧的三条 `setVjp`。交叉这一半写在本页第 2 节，
等一条允许改 rulesets 的车道裁：若裁甲，补四条前向律并把
`test/cases/compile/transforms/stated_*_answered_by_the_engine.cml` 的库层半边从 `sum`／`take`／
`mean_axis` 改成直接写 stated 拼写；若裁丙，撤四个公开名字并把这三个用例的引擎半边改成对一个
内部节点的读数。

**这不是「本版本暂不支持」**：四个名字的导数在两条腿上都已经有答案（引擎三条 `setVjp` 本车道补，
库层四条 `_t` 律本来就在），差别只在"用户能不能用同一个拼写从前向半边走进来"。

## 5. 未测点

- `vjp` 对 `sum_stated(x, [3])` 这个**直接拼写**的拒绝句子没有逐字量过（本车道没有构建过那棵树）：
  按 `modules/transforms` 的前向走法它应当停在"没有前向律"，但这句话不是本页的证据，只有"前向
  半边没有这四条律"这个事实是（`--tables` 可复现）。
- 交叉状态只在 `nvm` 后端量过。
- `pick_at` 那一条随 `lane-pickatvjp` 合入而消失；它合入之后本页第 1 节表里"只有库层有律"归零，
  `scripts/vjp-parity-exemptions.tsv` 的 `pick_at` 行会变 stale（gate 会打印 note，不红）。