# 待裁九页审计（2026-11-04，只读）
范围：`lane-nopb` / `lane-p7page`（2 页）/ `lane-withgrad` / `lane-linports` / `lane-softmax` / `lane-functrace` / `lane-reach` / `lane-meant` 九页。只判四项是否齐备，不评判内容；只读，未改任何车道树。

| 页（车道） | ①可裁一句（位置） | ②默认建议 | ③代价 | ④判据/哨兵 |
|---|---|---|---|---|
| item-7-deleting…（nopb） | 有：§5「问：」单句 | 有：A 暂不删 | 有：A/B/C 各带 | **缺**：只写"判据用符号"，无一条绿/红 |
| must-the-forward-value…（p7page） | 有：§要裁的那一句 | 有：暂不动（两条路都不选） | 有：10 用例 / 20 行、删了红 12 条 | **缺**：只要求"若取乙先定判据" |
| the-identity…（p7page） | 有：§要裁的那一句 | **部分**：方向"不算乙"，两条写法都"交负责人定" | 有：碰接口面 + `10200dc26` 方向 | 有：两条长红是哨兵、`transpose_hand_closed_lambda` 仍绿 |
| should-the-straight-line…（withgrad） | 有：§五「问：」 | 有：甲补规则 | 有：只对 `liftedFrom()` 有效 | 有：两条红 + `split` 19/19 + 87=84/2/1 |
| who-carries-the-operands…（linports） | 有：§问什么 引句 | 有：丙暂不动 | 有：27 红 / 上限 22→18 | 有：①′ + 87=84/2/1；页内自认 ①′ 到不了 0 |
| when-a-recomputed-call…（softmax） | 有：§问题 | 有：算作可达、后一半内联 | 有：`residuals=` 不得 +1 | 有：`residuals=`、`vjp_arities` 端口数、逐元素 |
| what-the-positional-read…（functrace） | **缺**：开篇"三个问题，各一句"，要裁句在末节 | **缺**：无默认建议节 | **缺**：只写"不这么做"的代价 | 部分：有"可判的句子"，无用例级哨兵 |
| can-the-by-position…（reach） | 有：§问什么（无问号，"还是"二选一） | 有：留着并写清 | 有：代价 0 + 另两路 | 有：11 条分布 + 84/2/1→74/12/1 + 两条长红同机制 |
| does-a-mean-tangent…（meant） | 有：§2「问」 | 有：走 1，记作既有限制 | 有：三条路各带 | 部分：第 `[0][0]` 位 3.333/5 可分，无用例名 |
## ① 要裁的那一句（原文首句，截断处标 …）

1. nopb：「问：第 7 项的"先删 `pullback.cpp` 的建图部分"，是在库层 `grad` 存在之前就删（B），还是等类型层拆包裁定落地…」
2. p7page-A：「让 `transpose` 不靠位置，只有两条路，选哪一条？」
3. p7page-B：「把 `split` 已经知道的身份（`Residual` 的 `parameter`）交给 `transpose`，算不算 14:55 否掉的那个"把位置换成字段"？」
4. withgrad：「问：直线路径要不要补上分支臂那条规则（一个抬升调用穿过它被抬起的原函数转置）…」
5. linports：「转置展开一条展开律时，需要"这个操作数自己的值和形状"（`sum_to` 的第二参数），这件事该由哪一侧承载——」
6. softmax：「`cut` 应当把它们当作可达（在后半边里直接内联成常量节点），还是维持"拒"？」
7. functrace：「要裁的是：把 `split` 已经知道的身份交给 `transpose`，算不算 14:55 否掉的那个"把位置换成字段"？」
8. reach：「要作者裁的是：这条约定留着并写清，还是换一条路」
9. meant：「`mean_axis` 的切线要不要写成**降秩**的…，还是把「轴为最后一维、或 `keepdims=true`」记成这条律**有定义的范围**」

## 重复问句：五页同题（同一个"身份 / 位置"岔口）

- 重叠句：**p7page-B 与 functrace 是同一句**（后者只删去"（`Residual` 的 `parameter`）"）；**linports 的甲/乙 与 p7page-A 的甲/乙 同形**（"身份进闭包" 对 "律不再要前向值"）；**reach 的"还是换一条路"就是这个二选一**（其第 2、3 路即甲、乙）。
- 同枝自相矛盾：p7page-A 写"甲 = 14:55 否掉的乙"（默认不动），p7page-B 写"这不算乙"（默认推进）；两页同在 `lane-p7page`。
- 建议：合并成一页，functrace / linports 的读数降为该页的证据节；理由：四项互不相同的默认（暂不动 / 不算乙推进 / 无默认 / 留着并写清）会让作者对同一问句裁四次。**未删任何页**。

## 待裁表一致性（只在本树 pageaudit 上验）

- 本树 `origin/develop` @ `c69a110fa`：`node scripts/check-doc-links.mjs --questions-index` → exit 0，"lists 47 page(s), one row each"、"no answered page is left in questions/"。本树没有九页（都在车道分支），这条绿灯只覆盖 develop 的 47 页。
- 九页所在分支：nopb / p7page / meant 的 README 已含新行；**withgrad / linports / softmax / functrace / reach 五棵树的 `docs/handover/questions/README.md` 没有 2026-11-04 行**（页已提交、工作树干净，README 末次提交早于该页）→ 合并前须重生成，否则该检查报缺行。

## 行号引用（只列，不改）

- 仅一处，nopb `2026-11-04-item-7-deleting-the-build-side-would-delete-grad-itself.cn.md` §7 原文：「**今天的行号是 49–4881 与 5044–5057**（行号会漂，判据用符号）」。其余八页无 `file:123` 形式，`file#Symbol` 用法合规。