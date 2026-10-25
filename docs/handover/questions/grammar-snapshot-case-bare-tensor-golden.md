# P4 收尾 lane 的一处阻塞：语法快照用例里的裸 `Tensor` 与它在本机无法重生成的 golden（h592 lane，2026-10-26）

> 本文只报逐字读数与被卡住的位置，不定语义，也不改任何被裁定页面（`docs/language/**`、
> `docs/design/**`、`docs/handover/reviews/**`、`docs/design-principles*`）。

本 lane 在 `handover/h465-flip-integration`（tip `29a5df7d6`）之上做 P4 删除步的机械收尾：
`kUnknownDim` 与"负数是未知维度"的哨兵改成"没有维度就是空条目"、`tensorOf` 的元素类型缺失由抛异常
改成断言、`carriesGradient` 按元素类型递归。这些都有零命中/逐处读数，见提交与 lane 报告。这里只放
**一件我没有做的事**，以及它卡在哪里。

## 一、`test/cases/tools/grammar/operators.cml` 还写着裸 `Tensor`

今天的读数（`git grep -n ": Tensor[,)]" -- test/cases`）：

```
test/cases/compile/array_alias/bare_tensor.cml:11:func count(t: Tensor): int {
test/cases/modules/onnx/ort_struct_casts.cml:2: * ... `type Run = { state: Tensor, steps: int }` is nominal:
test/cases/tools/grammar/operators.cml:17:func arithmetic(a: float, b: float, X: Tensor, w: Tensor): float {
test/cases/tools/grammar/operators.cml:26:func assign(var x: int, var t: Tensor, u: Tensor) {
```

`bare_tensor.cml` 是**故意**的：`test/plans/feat/compile/array_alias.plan.toml:44` 期望它报
`SemanticDiag::TensorElementTypeRequired`（`0x0001ED`）。`ort_struct_casts` 的两处是注释。真正还写着
裸 `Tensor` 的**代码**只有 `operators.cml` 的 17、26 两行。

## 二、为什么没有直接改成 `Tensor<f32>`

这个用例只被 `test/plans/feat/tools/grammar.plan.toml#grammar.scopes.operators` 以
`kind = "snapshot"` 使用：`args = ["editors/vscode/scripts/tokenize.mjs", "${case}"]`、
`golden = "./golden/grammar/operators.scopes"`，并且带
`skip_unless = ["node", "editors/vscode/scripts/have-deps.mjs"]`。主机上没有
`vscode-textmate` / `vscode-oniguruma` 这两个开发依赖，`have-deps.mjs` 因此不通过，这条测试在主机上
一直是 SKIP，golden 只能在装好依赖的机器上用
`node test/tools/run-tests.mjs --update-golden test/plans/feat/tools/grammar.plan.toml` 重生成。
只改源码不重生成 golden 会留下不一致，而**手改生成物**是本 lane 的禁令。

另一点读数：这份 golden 现在就已经落后于今天的词法器输出（它写着 `>sync func assign(...)` 这类今天的
tokenize.mjs 不再产出的行），所以一次重生成会带上与裸 `Tensor` 无关的差异，需要人看一眼而不是盲收。

## 三、选项与代价

1. **在 `editors/vscode` 里装两个开发依赖并重生成 golden**：用例与 golden 一次落到今天的状态；代价是
   要在检出里装 npm 依赖（本 lane 的许可范围里没有 `editors/**`），并且 golden 会连带带上其它陈旧差异。
2. **只把用例改成 `Tensor<f32>`，不动 golden**：用例恢复了可编译性；代价是把"golden 与用例不一致"推给
   下一个人，装好依赖的机器上这条快照测试会红或产生新 golden。
3. **保持现状并记录**：今天没有任何计划编译这个用例（它只被词法器 tokenize），所以它不影响任何门；
   代价是审计项"裸 `Tensor` 的测试命中"一直开着，文件里写着一个今天不成立的类型。

## 四、卡住什么

卡住的是第 1 条需要的两件许可：在 `editors/vscode` 装开发依赖（改动落在本 lane 许可范围之外的目录），
以及"生成物可以由脚本重生成、且重生成带来的无关差异可以一并收下"的确认。这两件都不是本 lane 能定的，
所以这条留作未做的读数报告。