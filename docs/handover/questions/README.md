# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：11 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-a-plan-whose-verdict-depends-on-load`](2026-11-02-a-plan-whose-verdict-depends-on-load.cn.md) | 一条计划的判定取决于当时的负载 |
| [`2026-11-02-can-a-known-failure-be-per-tier`](2026-11-02-can-a-known-failure-be-per-tier.cn.md) | 一个用例能不能"只在某一层算 known failure" |
| [`2026-11-02-is-an-unknown-escape-in-a-string-a-warning-or-an-error`](2026-11-02-is-an-unknown-escape-in-a-string-a-warning-or-an-error.cn.md) | 字符串里的未知转义是警告还是错误？ |
| [`2026-11-02-is-clearMarks-the-upper-bound-fallback`](2026-11-02-is-clearMarks-the-upper-bound-fallback.cn.md) | 这一处是不是"收集器『按上界扫』的兜底"：`clearMarks()` 每次回收都把整个图空间扫一遍清 mark 位 |
| [`2026-11-02-the-backward-half-of-a-loop-cannot-be-shown-to-go-in-reverse`](2026-11-02-the-backward-half-of-a-loop-cannot-be-shown-to-go-in-reverse.cn.md) | 循环的反向半程「按轮倒序」在今天的树上不可观测：先按 4.2 落「每轮一条残差」等聚合状态能 lift，还是先放宽 `pairTypeOf` 让它可判？ |
| [`2026-11-02-the-nine-shapes-the-generator-machine-refuses`](2026-11-02-the-nine-shapes-the-generator-machine-refuses.cn.md) | 生成器拒绝的九种形状：哪些能从源码走到 |
| [`2026-11-02-the-resolver-signature-greens-fifteen-not-twenty-nine`](2026-11-02-the-resolver-signature-greens-fifteen-not-twenty-nine.cn.md) | 转置解析器的新签名：绿 15 条，不是 29 条 |
| [`2026-11-02-transpose-tangent-parameters`](2026-11-02-transpose-tangent-parameters.cn.md) | `transpose` 怎么知道一个函数的参数是切向（半身），而不是普通参数？ |
| [`2026-11-02-which-generator-does-the-dangling-node-mean`](2026-11-02-which-generator-does-the-dangling-node-mean.cn.md) | 「生成器悬垂节点」指的是哪一件事：`yield` 状态机降级出的无人读节点，还是 `test/tools/progen` 生成的程序里的悬垂？ |
| [`2026-11-03-does-a-loop-body-declare-its-next-state-or-compute-it`](2026-11-03-does-a-loop-body-declare-its-next-state-or-compute-it.cn.md) | 一页一问：`loop` 的下一个状态是"体算出来的"，还是"臂声明的"？ |
| [`2026-11-03-may-transpose-of-a-function-declare-its-signature-after-building-the-member`](2026-11-03-may-transpose-of-a-function-declare-its-signature-after-building-the-member.cn.md) | 一页一问：`transpose(f)` 的类型能不能在"建完成员之后"才定？ |
