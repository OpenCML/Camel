# 报告：位置这一个自变量，让模型越过了弃权线

> 线：`lane-c3-merged`（分支 `lane-c3-merged`，工作树 `showcase`）。
> 读数全部来自构建后实跑（远程 `-Plan`），不是代码推演；参照是 `baseline/` 下的复步求导。
> 相关：`showcase/c3-char-transformer/WALLS.cn.md`、用例 `grow.stage5_lm_head` 与 `grow.stage6_pos_emb`。

## 1. 三档 loss，与那条地板

| 步数 | loss | 两行目标类概率 | argmax |
| --- | --- | --- | --- |
| 200 | 0.949558422 | 0.621136 / 0.622910 | [1, 0] |
| 500 | 0.226041985 | 0.893478 / 0.892786 | [1, 0] |
| 1000 | **0.034005788** | **0.983175 / 0.983107** | [1, 0] |

地板是**弃权值** 2·log2 = **1.386294**（两行各 log 2）。 三个数并排：

    1.386294（地板）→ 1.4447（stage5，无位置嵌入，没越过）→ 0.0340（stage6，越过并继续降）

引擎与参照在这三处一致到 12 位（1000 步：`0.034005787975` 对 `0.034005788`）。

## 2. argmax 训练前后

训练前 **[0, 0]**（两行都错）→ 训练后 **[1, 0]**（两行都对）。 位置 0 要类 1、位置 1 要类 0。

## 3. 置信度随步数

**0.6211 → 0.8935 → 0.9832**（200 / 500 / 1000）。 上升，不是饱和 ⇒ 结论是**方向对、只是没学够
（步数）**，不是容量的第二个上限。 1000 步逐点单调，200 步之后没有回升；学习率未动，只加了步数。

## 4. 新头走同一条梯度通路

stage6 的前 20 步与 `grow.py --stage 6` **12 位一致**（新增的 `pos` 也同步进了参照），20 行 loss
钉在用例 `grow.stage6_pos_emb` 里。 这条比"loss 降"更硬：它说明新参数走的是同一个回抽。

## 5. 复现

```powershell
camel showcase/c3-char-transformer/grow/stage6_pos_emb.cml --arg count=1000
python showcase/c3-char-transformer/baseline/grad_first_step.py --stage 4
python showcase/c3-char-transformer/baseline/grad_first_step.py --stage 2 --params 200
python showcase/c3-char-transformer/baseline/grow.py --stage 6 --exact
```

计划条目：`grow.stage6_pos_emb`（`test/plans/perf/showcase_c3.plan.toml`）。
前两条分别是"第一步梯度逐元素对参照"与"200 步参数对参照"。