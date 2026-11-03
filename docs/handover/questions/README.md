# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：10 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-03-when-the-tangent-recomputes-a-value-what-does-split-carry`](2026-11-03-when-the-tangent-recomputes-a-value-what-does-split-carry.cn.md) | 切线重算的那个"带上下文输入的节点"，`split` 该拷什么？ |
| [`2026-11-04-can-the-by-position-take-be-dropped`](2026-11-04-can-the-by-position-take-be-dropped.cn.md) | 按位置取值删不掉：11 条命中里 10 条真需求、1 条空转，而空转那条也丢不得 |
| [`2026-11-04-does-a-mean-tangent-reduce-the-axis-it-averaged`](2026-11-04-does-a-mean-tangent-reduce-the-axis-it-averaged.cn.md) | `mean` 的切线今天不降秩：轴不是最后一维且 `keepdims=false` 时是「响亮拒绝」，不是错数 |
| [`2026-11-04-how-is-the-by-content-rule-built-at-a-call-site`](2026-11-04-how-is-the-by-content-rule-built-at-a-call-site.cn.md) | 「答 `(value, tangent)` 二元组就切它、转置后一半、把调用转过来」——施工规格 |
| [`2026-11-04-item-7-deleting-the-build-side-would-delete-grad-itself`](2026-11-04-item-7-deleting-the-build-side-would-delete-grad-itself.cn.md) | 第 7 项「先删 `pullback.cpp` 的建图」今天能删吗：`buildGradientGraph` 仍是 `grad` 的唯一实现 |
| [`2026-11-04-must-the-forward-value-a-transpose-needs-carry-a-name-into-the-closure`](2026-11-04-must-the-forward-value-a-transpose-needs-carry-a-name-into-the-closure.cn.md) | 转置要的前向值，要不要以可辨认的身份进闭包 |
| [`2026-11-04-the-identity-already-exists-on-the-cut-side`](2026-11-04-the-identity-already-exists-on-the-cut-side.cn.md) | 身份其实已经在切点这一侧——只是没交给 `transpose` |
| [`2026-11-04-what-the-positional-read-is-a-copy-of`](2026-11-04-what-the-positional-read-is-a-copy-of.cn.md) | 证据页：那 18 条按位置取值读的是"切线链自己必须打的那通电话"的一份副本 |
| [`2026-11-04-when-a-recomputed-call-carries-an-unreachable-context-argument-should-the-cut-inline-it`](2026-11-04-when-a-recomputed-call-carries-an-unreachable-context-argument-should-the-cut-inline-it.cn.md) | 重算的调用带着出口不可达的参数时，`cut` 应当把它们算作可达，还是拒绝？ |
| [`2026-11-04-who-carries-the-operands-own-shape-into-the-transposition`](2026-11-04-who-carries-the-operands-own-shape-into-the-transposition.cn.md) | 转置展开律时要"操作数自己的值和形状"，这件事该由哪一侧承载？ |
