# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：10 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-does-reversing-a-backward-need-a-new-operator`](2026-11-02-does-reversing-a-backward-need-a-new-operator.cn.md) | 让反向半程按轮倒序，需要新增一个数组反转算子 `:struct/reverse_arr` 吗？ |
| [`2026-11-02-which-missing-operator-carries-a-reversed-backward`](2026-11-02-which-missing-operator-carries-a-reversed-backward.cn.md) | 倒序的 backward 该由哪一条来承载：新增 `:struct/reverse_arr`，还是让 draft 能造动态下标读？ |
| [`2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong`](2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong.cn.md) | 一个被循环体捕获的标量，抬升后静默算错 |
| [`2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today`](2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today.cn.md) | 转置成员的 norm 端口数多于调用点实参数：今天不是墙，是被更早一道拒绝挡住的潜在墙 |
| [`2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant`](2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant.cn.md) | 一个超时应该由读数推出来，而不是由人写死 |
| [`2026-11-03-does-a-loop-body-declare-its-next-state-or-compute-it`](2026-11-03-does-a-loop-body-declare-its-next-state-or-compute-it.cn.md) | 一页一问：`loop` 的下一个状态是"体算出来的"，还是"臂声明的"？ |
| [`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float`](2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md) | 反传倒着走要能钉住，先要循环状态不是浮点标量 |
| [`2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow`](2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow.cn.md) | 抬升列表已定为 grouped、空切向占位；剩下的是建图里的端口次序 |
| [`2026-11-03-which-p0-2-does-the-order-mean`](2026-11-03-which-p0-2-does-the-order-mean.cn.md) | 排序里那条 `P0-2` 指 A 还是 B？A 的两处是不是同一个条件（逐分支量：不是） |
| [`2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth`](2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth.cn.md) | 自检什么时候变贵了：成本已逐项归因，没有余额，也没有可比的前后读数 |
