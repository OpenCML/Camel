# 踩过的坑（症状 / 为什么 / 怎么避免）

每个生成器、每条守卫都有一条"看起来修好了、其实把红推给了下一层"的路。这张表只记实测过的，
每条都附当时的读数。

| 症状 | 为什么 | 怎么避免 |
| --- | --- | --- |
| `registry.reference_is_generated` 修绿之后，`docs.ai_reference.current` 立刻变红，报文是 `the 'stdlib-signatures' region is not what its source produces (534 entries)`（2026-11-03 实测：4 次运行里第 2 次） | AI 参考的 stdlib 签名目录**不是从 `stdlib/*.cml` 直接生成的**，而是从 `docs/stdlib-reference.md` 生成的。改了 `stdlib/*.cml` 的文档行、只重录了中间那层，就等于把红推给下一层：`stdlib/tensor.cml` → `docs/stdlib-reference.md` → `docs/reference/camel-for-ai-stdlib-signatures.md`，三个文件、两个生成器 | 改 `stdlib/*.cml` 的文档行之后**按链条顺序跑两次**：`node scripts/stdlib-reference.mjs --write`，再 `node scripts/camel-for-ai.mjs --write`。两个自检分别要 `up to date` 与 `what its sources produce`——**这两条不是一回事**，只过一条不算修好 |
| 两笔各自合法、各自测过，合到一起整层的 `total` 会跳（例如 `feat/compile` 从 2182 变 2187） | 整层的 `total` 由该层各 plan 声明的测试条目决定，任何一笔新增或删除用例都会改分母；单笔跑的是自己的 plan，看不到别人的分母 | 合前用**实时 `origin/develop`** 复跑那一层，不要用任何写死的 sha；**pass 与 total 分别报**，别用一个数代替另一个；比值下降时先问"是分子掉了还是分母变了" |