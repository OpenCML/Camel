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

**revert 之后那 10 条一条没消**，逐名对照（两棵树上句子逐字相同）：

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

## 三、判词

**B3 恰好解释 1 条，而且它解释的是"文档引用失效"，不是行为。** 所以"删掉唯一供给"这个说法在这一族上**不成立**——那 10 条在 B3 之前的那棵树上红得一模一样。继续在这条线上找原因，找的是不存在的东西。

这条判词不评价 B3 该不该存在。它只说明：**B3 不是那 10 条的原因**，两个事实在时间上相邻，在因果上无关。

## 四、`fail = 11` 的分类账

| # | 用例 | 所在 plan | 性质 |
| --- | --- | --- | --- |
| 1 | `first_step.tensor_vjp_answers_the_step.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **回归**：develop 上过、train 上不过（develop 侧读数引自 `lane-packdots` 的二分，本页没有重跑） |
| 2 | `matmul_t.law_read_through_vjp.nvm` | `test/plans/feat/showcase_s2.plan.toml` | **回归**：同上 |
| 3 | `matmul_t.vector_lhs_law.nvm` | `test/plans/feat/showcase_s2.plan.toml` | 既有欠账：**两棵树句子逐字相同**，与合并无关 |
| 4 | `matmul_t.vector_rhs_law.nvm` | `test/plans/feat/showcase_s2.plan.toml` | 既有欠账：**两棵树句子逐字相同**，与合并无关 |
| 5 | `mean_axis1_keepdims_gradient_reads_the_row_length.nvm` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账 |
| 6 | `erf_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账 |
| 7 | `erfc_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账 |
| 8 | `transpose_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账 |
| 9 | `linear_gradient_both_legs.nvm` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账 |
| 10 | `vjp_answers_abs_log_maximum_and_minimum` | `test/plans/feat/compile/transforms.plan.toml` | 既有欠账（条目在 develop 上就有） |
| 11 | `docs.links.citations` | `test/plans/feat/docs_links.plan.toml` | 文档引用：B3 删了符号没改引用，**已修**（见第六节） |

**10 条是既有欠账／回归的行为欠账，1 条是文档引用。** 这 11 条里没有一条是"合并把两棵绿的树撞红了"之外的第三种东西。

**原稿曾写成本条由 `rule-ranks` 换句而来，该说法来自协调者的一次误记，已由同平台逐字比对推翻。** `skip=rule-ranks uri=tensor:matmul ranks=(3, 2)` 是 `rank_three_is_refused_by_name`（函数 `batched_sum_of_product`）的原话，不是这两条 `*_law`；而 `rank_three` 在两棵树上的差别只有 `node=6` → `node=7`。**一个错的解释写进账本，会自己长出下一份错账。** 留着那句话，下一个人会去查一个不存在的换句。

## 五、门槛写 3、合并树上读出 11：不是多了 8 条债，是第一次一起看见

门槛那 3 条是 `matmul_t` 三兄弟（`vector_lhs_law`、`vector_rhs_law`、`rank_three_is_refused_by_name`）。在合并树上：

- `rank_three_is_refused_by_name` **过**（`node=7` 是 `0dc18fa9d` 自己交代过的编号位移，不是修出来的绿，也没有再动它的 pin）；
- `vector_lhs_law` / `vector_rhs_law` 在 develop 与 train 上**句子逐字节相同**（`skip=cotangent-needs-a-value uri=tensor:matmul position=0`），**与合并无关**。它们不是"换红法"的例子；`rule-ranks ranks=(3, 2)` 那句属于 `rank_three_is_refused_by_name`。
- 真正"多出来"的 8 条是同一族欠账，落在 `feat/compile/transforms` 与 `feat/showcase_s2` 这两张 plan 里，而**各车道只跑自己那一角的 plan**——`lane-packdots` 记 `8803 = 8757/3` 那次是 B6 之前、且只在 `feat/compile` 上。**这 8 条从没进过任何一条车道的读数，不是合并造出来的。**

一句话：**门槛的 3 是"车道上看得见的那一角"，树上的 11 是"合并之后全部看得见的那一片"；两者相差的不是债的数量，是视野。**

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

**再说它为什么现在还不能用。** 量它的那次 run 和另一条 run 共用了 host 上同一个 checkout 名 `mtrain`：一条 19:47 起（`m-train`），另一条 19:54 起（`probe-b3full`），两次 run 的 checkout/patch 阶段互相改写对方 plan 阶段正在读的树。证据是 `[provenance] tip 5d3017ca2…` 出现在一条属于 `m-train` 的 run 里，而那条分支的树从来没有过这个提交；`[SUMMARY]` 本身一个字都不怪，这正是它危险的地方。**这条读数要在独占的 checkout 名下重量一次才算数。**

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