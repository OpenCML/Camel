# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：12 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-does-reversing-a-backward-need-a-new-operator`](2026-11-02-does-reversing-a-backward-need-a-new-operator.cn.md) | 让反向半程按轮倒序，需要新增一个数组反转算子 `:struct/reverse_arr` 吗？ |
| [`2026-11-02-which-missing-operator-carries-a-reversed-backward`](2026-11-02-which-missing-operator-carries-a-reversed-backward.cn.md) | 倒序的 backward 该由哪一条来承载：新增 `:struct/reverse_arr`，还是让 draft 能造动态下标读？ |
| [`2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong`](2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong.cn.md) | 一个被循环体捕获的标量，抬升后静默算错 |
| [`2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today`](2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today.cn.md) | 转置成员的 norm 端口数多于调用点实参数：今天不是墙，是被更早一道拒绝挡住的潜在墙 |
| [`2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant`](2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant.cn.md) | 一个超时应该由读数推出来，而不是由人写死 |
| [`2026-11-03-is-a-name-at-a-void-tail-a-value-use`](2026-11-03-is-a-name-at-a-void-tail-a-value-use.cn.md) | 一个 `void` 函数体尾部的名字，是"被丢掉的值"还是"被当成值使用"？ |
| [`2026-11-03-may-a-region-owner-read-its-own-exit`](2026-11-03-may-a-region-owner-read-its-own-exit.cn.md) | 一个区域的拥有者能不能读自己区域的端口？ |
| [`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float`](2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md) | 反传倒着走要能钉住，先要循环状态不是浮点标量 |
| [`2026-11-03-the-capture-does-not-reach-the-body-is-not-the-whole-cause`](2026-11-03-the-capture-does-not-reach-the-body-is-not-the-whole-cause.cn.md) | 捕获"没进循环输入"不是全部原因：两条通道都实现了，错数一个也没动 |
| [`2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow`](2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow.cn.md) | 抬升列表已定为 grouped、空切向占位；剩下的是建图里的端口次序 |
| [`2026-11-03-the-silent-wrong-number-does-not-reproduce`](2026-11-03-the-silent-wrong-number-does-not-reproduce.cn.md) | 循环体里读外部标量的「静默错数」今天不可复现：它还算主线第一件吗？ |
| [`2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth`](2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth.cn.md) | 自检什么时候变贵了：成本已逐项归因，没有余额，也没有可比的前后读数 |
