# 把 B3 整条 revert 之后：它解释 1 条，不解释那 10 条

**这份报告的存在理由**：一个被证伪的假设不落成文字，下一个人会重做同一次实验。今晚已经付过一次这个代价——`docs/handover/reviews/2026-11-04-who-carries-the-shape-as-an-operand.cn.md` 就是为"记录为什么被推翻"而留的。**读者不是写它的我们，是下一个想 revert 点什么的人。**

## 一、假设原文

`m-train` 在 Linux 主机上读出的 `fail = 11` 里，有 10 条的句子是 `skip=cotangent-needs-a-value`（`tensor:multiply` 5、`tensor:matmul` 4、`tensor:divide` 1）。当时的假设是：

> 这一族来自 `lane-zerocarrier` 的 B3（`821f30c83`）删掉了 extents 的供给。B3 同一笔删了 `statedExtentsNodeOf`、`offeredExtents`、`extentsNode`、`extentsValue`、`valueOf` 的 extents 槽、`intArrayTypeOf`、第二个 `chooseTransposedRule`，"extents 值从哪来"这条线整个消失；而 `multiply`/`matmul`/`divide` 的律都要 `sum_to` 的广播目标形状，那个形状的值就是 extents。于是这一族从"有值"变成"要一个值"。

这个假设有一个可证伪的形式：**整条 revert B3，这一族应当消失。**

## 二、证伪的实测

两个读数都在 Linux 主机上取（`internal/tools/camel-remote.ps1 -Plan all -Tier fast -Jobs 8`），同一台机器、同一天、同样 8 并发：

| 树 | HEAD | `[SUMMARY]` | killed | timed out |
| --- | --- | --- | --- | --- |
| `m-train` | `34d9e7202`（tree `57139ba44`） | total 8804, pass 8750, **fail 11**, skip 13, known failures 30 | 0 | 0 |
| `probe-b3off` | `5fb44476f`（revert `821f30c83`） | total 8804, pass 8751, **fail 10**, skip 13, known failures 30 | 0 | 0 |

**revert 之后那 10 条一条没消**，逐名对照（这里的「两棵树」是 `m-train` `34d9e7202` 与 `probe-b3off` `5fb44476f`，**两棵都还带着 B3**，所以句子相同是意料之中的；这一句只对这两棵树成立，读成 develop 与 train 的对照就是错的）：

| 用例 | 句子 | revert 后 |
| --- | --- | --- |
| `first_step.tensor_vjp_answers_the_step.nvm` | `skip=cotangent-needs-a-value uri=tensor:multiply position=0` | 仍红 |
| `mean_axis1_keepdims_gradient_reads_the_row_length.nvm` | 同上 `tensor:multiply` | 仍红 |
| `erf_gradient_both_legs.nvm` | 同上 `tensor:multiply` | 仍红 |
| `erfc_gradient_both_legs.nvm` | 同上 `tensor:multiply` | 仍红 |
| `transpose_gradient_both_legs.nvm` | 同上 `tensor:multiply` | 仍红 |
| `matmul_t.law_read_through_vjp.nvm` | 同上 `tensor:matmul` | 仍红 |
| `matmul_t.vector_lhs_law.nvm` | 同上 `tensor:matmul` | 仍红 |
| `matmul_t.vector_rhs_law.nvm` | 同上 `tensor:matmul` | 仍红 |
| `linear_gradient_both_legs.nvm` | 同上 `tensor:matmul` | 仍红 |
| `vjp_answers_abs_log_maximum_and_minimum` | `skip=cotangent-needs-a-value uri=tensor:divide position=0` | 仍红 |

**唯一的变化是 `docs.links.citations` 从红转绿**：它红是因为 `include/camel/core/type/composite/array.h` 等 4 处引了 `modules/transforms/transpose.cpp#statedExtentsNodeOf`，而 B3 把这个符号删了；revert 把名字带回来，这条就绿了。

## 三、判词（已按整份 revert 更正）

**当初的判词是"B3 恰好解释 1 条，而且它解释的是文档引用失效，不是行为"。这个判词错了，错在实验的宽度，不在读数。** 那次 revert 只回退了 `821f30c83` 一笔（5 个文件里的 2 个）；整条线是两笔（`bb432e8ae` + `821f30c83`，5 个文件）。**整份 revert 之后 `fail = 2`，那 10 条里 8 条转绿——它们是 B3 这两笔的回归，不是欠账。**

**一个只回退了 2/5 文件的对照，得到的是"另外 3/5 还在"的读数——而它看起来像一条完整的判决。** 这句话被当证据用了两次（协调者一次、另一条车道一次），所以它同时进了 `agents/test.md`。

本页保留第一、二节的实测（它们对"`821f30c83` 单独一笔不是原因"仍然成立），第四节按整份 revert 重排，第八节记整份 revert 的读数与它当时不能用的理由。

## 四、`fail = 11` 的分类账

| # | 用例 | 所在 plan | 性质 |
| --- | --- | --- | --- |
| 1 | `first_step.tensor_vjp_answers_the_step.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **B3 的回归**：整份 revert 后 PASS |
| 2 | `matmul_t.law_read_through_vjp.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **B3 的回归**：整份 revert 后 PASS |
| 3 | `matmul_t.vector_lhs_law.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **既有欠账**：整份 revert 后仍红，两棵树句子逐字相同 |
| 4 | `matmul_t.vector_rhs_law.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **既有欠账**：整份 revert 后仍红，同上 |
| 5 | `mean_axis1_keepdims_gradient_reads_the_row_length.nvm` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 6 | `erf_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 7 | `erfc_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 8 | `transpose_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 9 | `linear_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 10 | `vjp_answers_abs_log_maximum_and_minimum` | `test/plans/feat/compile/transforms.plan.toml` | **B3 的回归** |
| 11 | `docs.links.citations` | `test/plans/feat/docs_links.plan.toml` | 文档引用：B3 删了符号没改引用，**已修**（见第六节） |

**2 条是既有欠账（就是那两条 vector 律），8 条是 B3 两笔的回归，1 条是文档引用。**「既有欠账」这个词从今天起只指那两条。

**原稿曾写成本条由 `rule-ranks` 换句而来，该说法来自协调者的一次误记，已由同平台逐字比对推翻。** `skip=rule-ranks uri=tensor:matmul ranks=(3, 2)` 是 `rank_three_is_refused_by_name`（函数 `batched_sum_of_product`）的原话，不是这两条 `*_law`；而 `rank_three` 在两棵树上的差别只有 `node=6` → `node=7`。**一个错的解释写进账本，会自己长出下一份错账。** 留着那句话，下一个人会去查一个不存在的换句。

## 五、门槛写 3、合并树上读出 11：那 8 条是 B3 的红，第一次被一起看见

门槛那 3 条是 `matmul_t` 三兄弟（`vector_lhs_law`、`vector_rhs_law`、`rank_three_is_refused_by_name`）。在合并树上：

- `rank_three_is_refused_by_name` **过**（`node=7` 是 `0dc18fa9d` 自己交代过的编号位移，不是修出来的绿，也没有再动它的 pin）；
- `vector_lhs_law` / `vector_rhs_law` 在 develop 与 train 上**句子逐字节相同**（`skip=cotangent-needs-a-value uri=tensor:matmul position=0`），**与合并无关**。它们不是"换红法"的例子；`rule-ranks ranks=(3, 2)` 那句属于 `rank_three_is_refused_by_name`。**它们是今天唯一还叫"欠账"的两条。**
- 另外 8 条是 **B3 两笔的回归**，落在 `feat/compile/transforms` 与 `feat/showcase_s2` 这两张 plan 里，而**各车道只跑自己那一角的 plan**——`lane-packdots` 记 `8803 = 8757/3` 那次是 B6 之前、且只在 `feat/compile` 上。**这 8 条从没进过任何一条车道的读数，合并之前就已经是红的。**

一句话：**门槛的 3 是"车道上看得见的那一角"，树上的 11 是"合并之后全部看得见的那一片"；差的那 8 条不是新债，是一直没人看见的回归。**

## 六、`array.h` 那条引用改到了哪里

`statedExtentsNodeOf` 今天不存在。写"把类型的 extents 变成图里那个 `int[]` 已知操作数"的是 **`modules/transforms/rule_expand.cpp#statedExtentsValue`**（在 `expandRuleRightSide` 里被调用，`rule_expand.cpp:865`）。

4 处引用全部改指它，**没有把符号加回去**（加回去就是同一个事实的第二种拼写，下一轮还会撞）：

1. `include/camel/core/type/composite/array.h`（`statedExtentsOf` 的注释里"两个读者"中的第二个）；
2. `src/builtin/operators/struct.cpp`（`extent_of` 的解析器注释）；
3. `stdlib/rulesets/transpose.cml`（`take_from_type_t` 上方的注释）；
4. `test/cases/compile/transforms/transpose_refuses_a_parameter_value_it_cannot_be_handed.cml`。

`node scripts/check-doc-links.mjs --citations` 现在：**828 / 828 条符号引用、2373 条路径引用、6254 个文件，全绿。**

## 七、给下一个想 revert 点什么的人

- 这次实验花了两次全量闸门（各约 300 s，主机 build 12–26 s），换来一句判词。**便宜，但只在你有一个可证伪的句子时便宜。**
- `probe-b3off`（`5fb44476f`）留在本地，不推：它是证据，不是交付。
- 想重做这个实验：`git switch -c <名字> m-train; git revert 821f30c83`。冲突在 `docs/handover/questions/README.md`（用脚本重生成，别手改）与 `stdlib/rulesets/transpose.cml`（取 revert 那一侧，即 `take_from_type_t`/`mean_axis_from_type_t` 重新把 extents 当 `at` 端口收，并用今天的三点包写法 `T[...]`）。
- **别为了让 fail 变 3 去动任何 pin。** 那 10 条要的是律里"把形状当已知实参交给乘法"的落地，不是标记。

## 八、`probe-b3full` 的 `fail = 2`：分量，以及它为什么现在还不能用

**先说分量。** `probe-b3full`（`5d3017ca2`，19:31:54 从 `c044d373d` 建、19:34:22 amend）读到：

`[SUMMARY] total: 8804, pass: 8759, fail: 2, skip: 13, known failures: 30`，killed 0，timed out 0。

**如果它是真的，它比 `develop` 的 `fail = 3` 还低一条**：整条线 revert 之后，合并树不但没有回归，还少一条红。这同时说明**本页第三节的判词只对 `821f30c83` 这一笔成立**——我 revert 的只是那一笔（4 个文件：`transpose.cpp`、`transpose.cml`、两处 docs），而 `probe-b3full` 覆盖 10 个文件，多出 `modules/transforms/rule_expand.cpp`、`rule_expand.h`、`include/camel/core/type/composite/array.h`、`src/builtin/operators/struct.cpp` 与两条测试。**那 8 条不是被 B3 关掉的，是 B3 之外的那几笔关掉的**——供给是在整条线里断的，不是在这一笔里断的。

**再说它为什么现在还不能用。** 量它的那次 run 和另一条 run 共用了 host 上同一个 checkout 名 `mtrain`：一条 19:47 起（`m-train`），另一条 19:54 起（`probe-b3full`），两次 run 的 checkout/patch 阶段互相改写对方 plan 阶段正在读的树。证据是 `[provenance] tip 5d3017ca2…` 出现在一条属于 `m-train` 的 run 里，而那条分支的树从来没有过这个提交；`[SUMMARY]` 本身一个字都不怪，这正是它危险的地方。

**这笔账后来结清了**：两条独立车道（一条量 `5d3017ca2`，一条量 train 尖端同角）各自读到 `fail = 2`，互相印证；我自己那次撞车的读数作废，不再引用。**本页第四节因此按整份 revert 记账。**

**顺带记下别人留在 `D:\Projects\Camel-wt\mtrain` 的 10 个改动**（原本打算先记后撤，撤之前它们已被提交成 `10abd59d0`，所以我没有 `checkout -- .`、也没有丢任何东西）：

```
D  docs/handover/questions/2026-11-04-a-sum-to-that-takes-the-shape-itself.cn.md
M  docs/handover/questions/README.md
M  include/camel/core/type/composite/array.h
M  modules/transforms/rule_expand.cpp
M  modules/transforms/rule_expand.h
M  modules/transforms/transpose.cpp
M  src/builtin/operators/struct.cpp
M  stdlib/rulesets/transpose.cml
M  test/cases/compile/transforms/transpose_refuses_a_parameter_value_it_cannot_be_handed.cml
M  test/plans/feat/compile/transforms.plan.toml
```

`git diff --stat HEAD` 当时是空（全部已 staged），现在为空是因为那 10 个文件已是 `10abd59d0` 的提交。**`m-train` 的 tip 因此被推到 `10abd59d0`（整条线 revert），而本车的交付点仍是 `merge-train` = `c044d373d`。**

## 九、补记：第三车道量出的三份读数，与两处判据的更正

三份全量读数，各自独占 host checkout、`[provenance]` 无 `dirty`，同日同 helper（`-Plan all -Tier fast -Jobs 8`）：

| 树（checkout 名） | tip | `[SUMMARY]` | `fail` 名单 | killed / timed out |
| --- | --- | --- | --- | --- |
| `b3full` | `5d3017ca2` | total 8804, pass 8759, **fail 2**, skip 13, known failures 30 | `vector_lhs_law`、`vector_rhs_law` | 0 / 0 |
| `b3rev` | `10abd59d0` | total 8804, pass 8759, **fail 2**, skip 13, known failures 30 | 同上（与上一行逐字相同） | 0 / 0 |
| `devbase` | `00dc468f8` | total 8790, pass 8744, **fail 3**, skip 13, known failures 30 | 同上 + `rank_three_is_refused_by_name` | 0 / 0 |

`killed` / `timed out` 不进 `[SUMMARY]` 的正文：`test/tools/run-tests.mjs` 只在它们大于 0 时才追加 `, timed out: N` / `, killed by a signal: N`，所以「没有这两个字段」就是 0。

**判词更正，写死，别再用旧措辞**：**那 8 条不是欠账，是 B3 两笔造成的回归；先前的「句子逐字相同」是拿一份含 11 条的名单与一份含 3 条的名单比出来的，而被比较的两个名字在其中一份里根本不存在。**

**比较两份 `fail` 名单之前，先确认它们的 `total` 相同**：`8804` 与 `8790` 相差 14 条，这两份名单不可直接相减，也不可逐名对照——那 8 条在 develop 的 plan 里确实存在（`first_step.tensor_vjp_answers_the_step`、`matmul_t.law_read_through_vjp` 在 `feat/showcase_s2.plan.toml`，`erf_gradient_both_legs`、`linear_gradient_both_legs` 在 `feat/compile/transforms.plan.toml`），在 develop 上是绿的；它们没出现在 develop 的 `fail` 里，是因为它们本来就绿，不是因为「句子相同」。

**判据的参照点选错，会让一个正确的工作看起来不合格**：原判据写「这 5 个文件与 `821f30c83^` 相同」，而 `821f30c83^` 里已经含着 B3 的另一半 `bb432e8ae`、且它是 lane 老基（早于 develop 漂移），**那个状态从来没有存在过**，判据要求的是一个不存在的点。量 `bb432e8ae^` 才分得开：`src/builtin/operators/struct.cpp` **逐字节相同**（它根本不是这次的改动），其余四个的残差全是 B3 之后别人做的活（`8f713d53a` 的聚合余切、rank-zero 转换、三点包 `T[...]`、`6703113b6` 的括号、`4e4697456` 的结构性零切向）。

**落地状态**：`m-train` 与 `origin/develop`（`8dc01598d`）的 tree 都是 `6675ae8d3`，`git diff m-train origin/develop` 为空——revert 已随 develop 落地，`git ls-remote` 上 `origin/m-train` = `dd62f0f147`。**`devbase` 的 `fail = 3` 是 develop 当前基线的唯一实测，那棵树别收。**