# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：9 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-04-should-an-assertion-pin-a-node-number`](2026-11-04-should-an-assertion-pin-a-node-number.cn.md) | 断言侧钉 `node=`/`param=` 的期望串，要不要一律改成按名字或关系断言？ |
| [`2026-11-04-which-half-of-a-page-decides-whether-the-twins-exist`](2026-11-04-which-half-of-a-page-decides-whether-the-twins-exist.cn.md) | 同一条裁定指向的页面，在「孪生算子该不该存在」上自相矛盾，以哪一段为准？ |
| [`2026-11-04-which-word-does-a-declaration-that-states-no-shape-get`](2026-11-04-which-word-does-a-declaration-that-states-no-shape-get.cn.md) | 同一个原因（声明未陈述形状），该用哪个词？ |
| [`2026-11-05-can-match-type-take-an-array-types-dimensions-apart`](2026-11-05-can-match-type-take-an-array-types-dimensions-apart.cn.md) | `match type` 能不能像拆类型包那样拆开数组类型的维度列表，让 `ShapeOf<T>` 用 GN 写得出？ |
| [`2026-11-05-does-a-println-in-a-loop-body-need-do-sync`](2026-11-05-does-a-println-in-a-loop-body-need-do-sync.cn.md) | `loop` 体里的 `println` 不产出输出：要采纳的写法是 `do sync`，而 `EffectNotExecuted` 的 help 指错了补救？ |
| [`2026-11-05-does-a-shape-declaration-need-a-length-in-its-return-type`](2026-11-05-does-a-shape-declaration-need-a-length-in-its-return-type.cn.md) | `tensor:shape` 的声明返回 `int[]`（无长度）——要不要让它能说「每轴一条」？ |
| [`2026-11-05-does-a-sweep-grow-with-every-test-that-is-added`](2026-11-05-does-a-sweep-grow-with-every-test-that-is-added.cn.md) | `[[sweeps]]` 的派生数随「被命名计划里的条目数」自动增长，这是有意设计还是需要一个上限？ |
| [`2026-11-05-does-a-type-position-need-pattern-syntax-or-is-firstof-the-channel`](2026-11-05-does-a-type-position-need-pattern-syntax-or-is-firstof-the-channel.cn.md) | 类型位置要模式语法，还是就用 `FirstOf<P>` 这条既有通道？ |
| [`2026-11-05-should-the-jvp-with-promotion-go-with-the-language-gap`](2026-11-05-should-the-jvp-with-promotion-go-with-the-language-gap.cn.md) | 删掉 jvp 里提升 `with` 值的过渡写法，三条用例的对数字会变成 0.0——这一件按什么次序做？ |
