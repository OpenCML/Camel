# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：4 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-does-reversing-a-backward-need-a-new-operator`](2026-11-02-does-reversing-a-backward-need-a-new-operator.cn.md) | 让反向半程按轮倒序，需要新增一个数组反转算子 `:struct/reverse_arr` 吗？ |
| [`2026-11-02-which-missing-operator-carries-a-reversed-backward`](2026-11-02-which-missing-operator-carries-a-reversed-backward.cn.md) | 倒序的 backward 该由哪一条来承载：新增 `:struct/reverse_arr`，还是让 draft 能造动态下标读？ |
| [`2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant`](2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant.cn.md) | 一个超时应该由读数推出来，而不是由人写死 |
| [`2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth`](2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth.cn.md) | 自检什么时候变贵了：成本已逐项归因，没有余额，也没有可比的前后读数 |
| [`2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow`](2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow.cn.md) | 抬升列表已定为 grouped；只改签名不改图端口，`split` 就切错位 —— 下一步是端口次序 |
