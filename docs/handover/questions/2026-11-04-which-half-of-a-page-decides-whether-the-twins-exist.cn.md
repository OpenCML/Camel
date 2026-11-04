# 同一条裁定指向的页面，在「孪生算子该不该存在」上自相矛盾，以哪一段为准？

**状态**：待裁。本页只做一件事：把矛盾逐字摆出来、把它两侧各自的实测边界钉住、给出三条路各带的代价，并写死一个我方默认（第 3 条）。**这条矛盾本身是 2026-11-04 那一笔的最重要产出**——它不是文风问题：它已经长成了一道命令与自己的注脚相冲突的样子。

## 1. 矛盾的两句原话

同一条裁定（作者 2026-11-04 12:40）把我们指向
`docs/handover/reviews/2026-11-04-who-carries-the-shape-as-an-operand.cn.md`，命令先落
`mul_stated` / `matmul_stated` 两个孪生算子。那一页自己说了两件相反的话：

- **页首状态行**（该页 `docs/handover/reviews/2026-11-04-who-carries-the-shape-as-an-operand.cn.md` 第 1 节前的状态段），逐字：

  > 所以"把形状当操作数交进律"这条路对 10 条一条也关不掉，新型算子 `mul_stated`/`matmul_stated` 随之作废。

- **同一页第 1 节第 4 条**（更正上一页结论的那一段），逐字：

  > 所以带长度的孪生算子**正好管这一族**，那页的结论对那六条 `known_fail`（位 1 要读切线的值）依然成立，对这一族不成立。

一句说这两个算子作废，一句说它们正好管这一族。裁定命令落的那两个算子，正是被同页宣告作废的那两个。

## 2. 实测分界（2026-11-04，树 `D:\Projects\Camel-wt\b3redo`，宿主 `b3redo`，提交 `5ca14caf5`，1 m load 23–31：强负载，读数只做同时段比较）

### 2.1 被孪生结掉的那一族

`known failures` **由 20 降到 17**，三条 `known_fail` 现在精确答出被标记时写下的期望数字，标记按规矩摘掉：

| 用例 | 摘标记后答出的读法 |
| --- | --- |
| `transpose_array_product.answers_both_operands` | `value = 20.0`，`d/dxs = 2.0 3.0 4.0`，`d/dw = 1.0 2.0 3.0` |
| `jvp_vjp_array_product.one_point_two_ends` | `<jvp tangent, ct> = 6.0` 与 `<dxs, d/dxs> + <dws, d/dw> = 6.0` |
| `broadcast_product_gradient_of_an_open_rank` | `d/dx numel=3 d/dx=[2.0, 2.0, 2.0]`，`d/dw numel=1 d/dw=[6.0]` |

第三条尤其要紧：它被标记的原因正是"广播积的余切收不回各自形状"，而孪生给的答案与期望逐字相同——那一族归 `mul_stated` 管，实测成立。

顺带：`feat/compile` `total 2337 / pass 2320 / fail 0 / known 17`（原 `2317 / 0 / 20`）；`feat/showcase_s2` `total 7 / pass 5 / fail 2`，被裁定点名的两条（`first_step.tensor_vjp_answers_the_step`、`matmul_t.law_read_through_vjp`）走新路 `stdlib/rulesets/transpose.cml#matmul_stated_t` 答出原数字。

### 2.2 没被孪生结掉的那一族

两条 vector 律仍是红，用例的操作数类型**一个字符没动**（仍写 `float[]`），拒绝句原文：

```
TRANSPOSE function=split_of_jvp_of_sum_of_product_backward skip=cotangent-needs-a-value uri=tensor:matmul_stated position=0 reason=the rule's right side reads a value the caller does not carry
TRANSPOSE function=split_of_jvp_of_sum_of_product_backward skip=cotangent-needs-a-value uri=tensor:matmul_stated position=1 reason=the rule's right side reads a value the caller does not carry
```

读者看到的那句是 `transforms: transpose has no transpose for this function: a parameter holds no cotangent: nothing uses its value.`

两点要连起来读：

1. 拒绝句里的 `uri=` 已经是 `tensor:matmul_stated`，**不是**原来的 `tensor:matmul`——即这两条真的走到了孪生算子这条新路上，卡在里面而不是卡在门口。
2. 同一条律一个字不改、只把操作数写成陈述了长度的类型（`float[2]`），这两条就精确答出 pin 的全部数字（`3.0`、`[0.75, 1.125]`、`[[1,1],[2,2]]`，`max_abs_diff 0.0`；父代理 2026-11-04 20:5x 实测）。所以分界不在律的算式，也不在形状——形状已经是操作数了——而在**开放类型下、被回答的那一位自己的值不在后半程手里**，这与该页第 1 节第 3 条记的"缺的是值不是形状"是同一件事。

## 3. 三条路与各自的代价

- **甲：以页首状态行为准（孪生作废）。** 代价两笔：刚落地并已结掉三条债的那两个算子要撤（`known` 从 17 退回 20，两条被裁定点名的用例退回 `matmul_t` 的说法），且 `mul_stated` 若继续留在树上就**需要一个新理由**——今天它唯一的理由是那一页第 2、3 节，而那段已被宣告作废。
- **乙：以第 1 节第 4 条为准（孪生管这一族）。** 那么"正好管这一族"里的"这一族"必须包含两条 vector，于是要开**「后半程携带开放秩操作数的值」**这条新腿——后半程持有原值。那是作者同页第 4 节末尾明确宣告作废的那一族，所以乙不是修一个缺陷，是建一条新通道。
- **丙（我方默认）：把孪生的管辖范围写死在一条规则里。** 哪一族归它、哪一族不归，写成可判的规则（已随本次提交落在
  `stdlib/rulesets/transpose.cml`，紧挨着六个 `ct_of_*_stated` 的秩分派），并把"开放秩操作数的值不在后半程手里"记为**当前定义域之外**。两条红从此有明确归属：不是"孪生没做完"，是"不在它的定义域里"，入口在 `docs/handover/questions/2026-11-04-who-records-which-closure-member-is-which-parameter.cn.md` 那一族（残差与端口的对应）。

**默认选丙，理由是甲和乙都在赌哪一段是作者的疏忽，丙不赌**：它把"孪生管到哪"变成一条可以被下一次实测判定的规则，而今天两条红的归属也随之一句话写清。若作者裁甲或乙，第 3 条那条规则删掉即可，代码不必动。

## 4. 需要裁的那一句

**同一份裁定指向的页面，在「这两个算子该不该存在」上自相矛盾时，以页首状态行为准还是以第 1 节第 4 条为准？** 若两者都对，请裁它们的适用范围（第 3 条丙就是据此写死的：`mul_stated` 管广播积那一族，管不到开放类型下需要「被回答那一位的值」的那一族）。