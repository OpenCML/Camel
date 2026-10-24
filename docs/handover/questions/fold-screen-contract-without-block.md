# `std::opt::fold` 的 screen 合同：离开 `BLOCK` 无法单独落地（裁定里的例子被实测推翻）

决策者 2026-10-24 傍晚的裁定（[questions-2026-10-23.md](../reviews/questions-2026-10-23.md) 的
`### block-model-superseded（以及 inlined-statement-size-limit、function-typed-parameter-tangent）`）说：
`h408-splice-rest` 里"**与 `BLOCK` 无关的改动（`std::opt::fold` 的 screen 等）拆出来单独合**"。
施工方按这条裁定逐提交分类（车道 `h423-splice-partition`，零提交，只做核对），
结论是 **`(A)` 类为空**：`std::opt::fold` 的 screen 那条恰恰**依赖 `BLOCK`**，不是无关改动。

## 证据（都可复现）

1. 那条改动是 `45dca4fa2`（`screen.cpp +28/−2`），它新增的代码调用 `runtime::forwardsValue`；
   `git grep -n forwardsValue origin/develop` = **0 个文件**，在 `h408-splice-rest` 上 9 个文件。
2. `forwardsValue` 的定义逐字（`include/camel/runtime/graph.h:143`，只在 `h408` 上）：
   `return kind == GCNodeKind::Gate || kind == GCNodeKind::Block;` —— 它读穿的正是 `BLOCK`。
3. 它要修的那条红在 develop 上**本来就是绿的**：`onnx.export_train_step_bn` `[PASS] 1.67 s`，
   `feat.opt.screens` 全层（`std::opt check_screens=true`）全绿；develop 上 pass 与 screen 问的是同一句
   （`resolveTupleProjection` 只解 `Accs→Fill`，screen 只认 `Fill`），没有"screen 比 pass 更严"的缺口。
4. `45dca4fa2` 自己的提交信息写明那条红由 `fa77c422e`（引入 `BLOCK` 的 tuple 投影读穿）引入。

## 选项

- **(a) 记进 H242（推荐）**：把"screen 不得比它筛的遍更严"作为一条**合同**写进 H242 的验收
  （按 `docs/design/effects/edges.cn.md` 5.7 的顺序边模型），随 H242 整批落地并自带用例。
  代价：这条合同在 H242 之前没有守卫；好处：不留一条只为旧模型存在的补丁。
- **(b) 现在就按非 `BLOCK` 写法重写并单独合**：需要先写清"顺序边模型下 screen 与 pass 的一致性"是什么，
  并自带用例——因为 develop 上今天没有可修的红，这个改动没有红前证据，属于**无缺陷驱动的改动**。
  代价：在没有红前证据的情况下改优化器的 screen 合同，风险由我们承担。
- **(c) 丢弃这条改动**：与 (a) 的差别是连合同也不记；如果作者认为"screen 与 pass 一致"本就不必成为合同，
  那么 (c) 就是答案。

## 建议

**(a)**。理由：它把价值（合同）保住、把只在旧模型上成立的部分丢掉，并且 H242 已经要整批重做这一带。

## 出处

- `docs/handover/reviews/questions-2026-10-23.md` §`block-model-superseded`（裁定的原文）
- `docs/design/effects/edges.cn.md` 5.7（最终模型）
- 车道 `handover/h423-splice-partition` 的分类报告（30 个提交逐条：`(B)` 21 条丢弃、`(C)` 9 条不进 develop）