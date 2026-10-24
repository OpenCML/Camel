# 集成树之外的 12 条分支：只在分支上、两条主线都没有的文件与读数

清点日期 2026-10-25。口径：`git branch --no-merged origin/develop` 得 41 条，其中 29 条的 tip 是
`handover/h465-flip-integration` 的祖先（`git merge-base --is-ancestor` 返回 0），余 **12** 条不在集成树里。
两条主线取 `origin/develop`（tip `3f235254f`；集成树 `coord` 与之同点）与
`handover/h465-flip-integration`（tip `415ec0f4d`）。

本文只写事实与读数。每一条都由 `git diff --name-status <b> handover/h465-flip-integration` 取 `D`
（在分支上、不在 h465），再用 `git cat-file -e origin/develop:<path>` 与
`git cat-file -e handover/h465-flip-integration:<path>` 两边都为假确认，原文用 `git show <b>:<path>` 取。

## 一、只有分支上有、两条主线都没有的文件

| 分支 | 文件 | 性质 |
|---|---|---|
| `handover/h469-literal-shape` | `test/cases/compile/array_shape/literal_rank_two.cml`（15 行）、`nested_rows.cml`（16）、`reshape_constant.cml`（13）、`err_literal_rank.cml`（8）、`err_literal_ragged.cml`（8） | 该分支唯一提交 `20e7a7713` 的 5 条例 |
| `handover/h469-literal-shape` | `test/plans/feat/compile/array_shape/array_shape.plan.toml` 的 6 个 `[[tests]]` 段：`literal_rank_two`、`literal_rank_two_gir`、`nested_rows`、`nested_rows_gir`、`reshape_constant`、`err_literal_rank`、`err_literal_ragged` | 只在 h469 的版本里；h465 的同一文件把这一整段删了，并把 `err_return_shape` 的期望从 `SemanticDiag::ReturnTypeMismatch` 改成 `SemanticDiag::ReturnNeedsConversion` |
| `handover/h438-tensor-alias` | `docs/handover/questions/tensor-alias-h438-open-items.md`（104 行，6 条待裁定 + 逐字读数） | 从未进任何主线（`git log --all --diff-filter=D` 该路径 0 命中） |
| `handover/h438-tensor-alias` | `docs/handover/plans/H17-plan.cn.md`（71 行） | 第一节是 `tensor-alias-transition.md` 5.1a 的照录（该文件在 h465 里），第二、三节是 h438 自己的过渡依赖表 |
| `handover/h446-flip-autodiff` | `docs/handover/questions/subscript-operator-choice-and-the-rank-two-refusal.md` | `t[idx]` 因索引元素类型是否静态已知而落到两个算子；秩 ≥2 数值数组的**新引入拒绝** |
| `handover/h484-flip-audit` | `docs/handover/questions/flip-reds-without-an-owner-and-a-green-reading-one-merge-behind.md` | h484 两轮审计读数；文末自述"本文件的两个问题清零" |
| `handover/h485-printer-var-read` | `docs/handover/questions/2026-10-25-cml-read-of-a-write-and-the-write-valued-body.md` | 值输出落点的实测（身体结果那一处 segfault、43 条转红、已 revert `a0001f185`）与两个问句 |
| `handover/h446-flip-autodiff`、`handover/h484-flip-audit` | `notes/h419-base-collect.py`、`notes/h419-base-layer.log`、`notes/h419-base-reds.txt`、`notes/h419-after-collect.py`、`notes/h419-after-layer.log`、`notes/h419-after-reds.txt` | h419 的基线/之后读数草稿（层日志 891 360 / 682 073 字节）。集成线自己提交过它们（`5486745c2` 等），`f020350b5` 又把它们删掉——`runner.backend_matrix` 自检对"没有计划选中的文件"报红。两条主线的 `notes/` 计数都是 **0** |

只有分支上有、但**已被主线按主题重新立过或已被主线删除**的（不属上表）：
`recursion-specialization-fixed-point-followups.md`、`h461-retired-syntax-leftovers.md`、
`h462-static-loop-sequence-boundaries.md`、`tensor-iter-path-left-on-develop.md`、
`bare-tensor-signature-rewrite.md`、`sync-chain-liveness-and-splice.md`、
`verification-report-surface-s0.md`、`fold-screen-contract-without-block.md`、
`diagnostic-fixes-json-and-cause-vocabulary.md` —— 每一条都能在 develop 或 h465 的
`git log --diff-filter=D` 里找到删除提交（`ce63b1ace`、`f2ce4e88a`、`dfca62ccd`、`e2848e854`、
`bc8a374e8`）。
`docs/technical/00_language_reference.md`、`docs/tutorial/examples/06_wait.cml`、
`06_sync_func.cml`、`test/cases/**/wait_*.cml`、`captured_write_without_sync.cml` 属退役语法的旧路径，
两条主线都在 `06a731980` / `3da698ed5` / `48a670ce6` 删掉了。
`test/cases/modules/autodiff/refuse_{cast,concat,new,reshape}_array.cml` 在 `origin/develop` 里**在**，
只是被别名那一刀从 h465 删了。

## 二、h469：它要实现的行为，集成线量到的是相反结果

`handover/h469-literal-shape` 的唯一提交 `20e7a7713`（`feat: a rank-2 constant array takes the shape its
position states, and nothing is reshaped to fit`，21 个文件、+641/−36）在 `src/compile/gct/builder.cpp`
加了 228 行、`src/core/rtdata/conv.cpp` 加了 64 行（其中 `statedExtents()` 把"类型写全了的常量形状"
当成值的形状）。h465 的同一处没有这段：`git diff handover/h469-literal-shape
handover/h465-flip-integration -- src/core/rtdata/conv.cpp` 显示 `statedExtents()` 整个不存在，
`TypeCode::Array` 分支仍是 `Array::create(allocator, arrayType, 0)` 后逐个 `append`。

h465 把这一主题立成了
`docs/handover/questions/rank-two-constant-array-has-no-source-form.md`（h453 lane，2026-10-25），
里面第三行的读数正是相反的一条：

```
let b: f32[2, 2] = [1.0, 2.0, 3.0, 4.0]   ->  core.loopDomain: t < len(node 0) = 4; element port 2 reads xs(t)
```

即"注解没有把平铺字面量按声明成形"，而该文件第二节把"该不该按声明成形"作为待裁定问题之一。

`git grep -l -e 'h469' -e 'literal_rank_two' -e 'err_literal_ragged' -e 'reshape_constant'
handover/h465-flip-integration` 为 0 命中：h465 里没有任何地方提到这条分支或它的用例。

## 三、建议（只列事实指向，不代替裁定）

1. 若作者裁定"平铺字面量按声明成形"成为语言的意图，`handover/h469-literal-shape` 上有一份**完整实现**
   （代码 + 5 条用例 + plan 段），可整体复活，不必重做。
2. 若裁定维持今天的读数（"秩 ≥2 的常量数组没有源形式"），那么这 5 条用例与 6 个 plan 段应当随该分支
   一起丢弃——它们钉的正是被否掉的行为，补进树会让 `array_shape` 计划红。
   这种情况下请在 `rank-two-constant-array-has-no-source-form.md` 里落一句，好让删除有据。
3. 这 12 条里 6 条**无工作树**：`handover/h438-tensor-alias`、`handover/h469-literal-shape`、
   `handover/h484-flip-audit`、`handover/h484-flip-audit-r2`、`handover/h484-flip-audit-r2b`、
   `handover/h484-flip-audit-tip`——它们是批次删除里唯一会丢掉内容的；另外 6 条
   （`handover/h446-flip-autodiff`、`handover/h480-runner-and-tensor-api`、
   `handover/h484-flip-audit-r2c`、`handover/h485-printer-var-read`、`handover/h487-graph-facts`、
   `handover/h488-data-model-alias`）有工作树，按规矩不动。
   注：工作树 `D:\Projects\Camel-wt\h484-flip-audit` 检出的分支是 `handover/h484-flip-audit-r2c`，
   不是 `handover/h484-flip-audit`。
4. `handover/h446-flip-autodiff` 的
   `docs/handover/questions/subscript-operator-choice-and-the-rank-two-refusal.md` 引用的
   `notes/h446-r3-layer-after.log` 在该工作树里**不存在**（`Test-Path` 为假），也不在任何分支的树里
   ——那 31 条实验的完整 `[FAIL]` 名单只剩问题文件正文里的 6 行摘要。

## 四、`handover/h438-tensor-alias` 上那两个记录件的判断（本条由集成 lane 补，仍只报事实与依据）

结论：**都不并进翻转线**，一个在线上已有等价物，一个只是过渡期的旧计划。

- `docs/handover/questions/tensor-alias-h438-open-items.md`（104 行，6 条）——**六条在线上都已重立或被裁定**，
  逐条依据：
  1. "数组拼法的张量 `t[i]` 落到核心数组索引"：别名已由 `a5a32a077` 落地，`isTensorType` =
     `isNumericArrayType`（`modules/tensor/type.cpp`），线上记录在 `tensor-alias-transition.md`。
  2. "未写元素类型的字面量数组 f64 还是 f32"：同一个问题在本线上以**逐字两侧读数**重立为
     `rank-zero-array-and-a-float-literal.md`（`kernel f64, declaration f32`）。
  3. "梯度算子的节点类型（`addTensorOperFor`）"：`tensor-alias-transition.md` 有专条（该文件里
     `addTensorOperFor` 1 处命中）。
  4. "打印收口只做了一半"：作者"一种打印形"的裁定已定，线上记录在
     `h450-spelling-and-print-form-leftovers.md`，且 `docs.snippets_compile` 在 tip 上已绿。
  5. "两个真缺陷"：**线上都已不红** —— `si_var_argument_plain`（`test/plans/feat/modules/tensor/
     shape_inference.plan.toml:230`）与 `single_instance` 在 `-Plan .` 与 `smoke,feat` 两个口径的
     最新跑里都是 `[PASS]`；`docs/handover/` 下"未述元素类型"12 处命中覆盖第 6 条那一族。
  6. "22 处未述元素类型的叶子"：同上，`element-type-convention-leftovers.md` 一族在线上。
- `docs/handover/plans/H17-plan.cn.md`（71 行）：其 §一 已是 `tensor-alias-transition.md` 的
  "一、建议加进计划的文字"（照录），§二/§三是 h438 自己的**过渡依赖表**；别名落地后过渡结束，它只是
  旧计划。留一句引用即可，不搬实体。