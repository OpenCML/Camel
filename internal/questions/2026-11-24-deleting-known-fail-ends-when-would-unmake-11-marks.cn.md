# 删掉 `known_fail_ends_when` 会让 11 条标记变成不合法的——它是负责人裁定的形状

2026-11-24 ｜ 来自 `lane/test-refactor-2` ｜ 对象：`test/tools/run-tests.mjs`、`test/tools/check-plan-fields.mjs`

## 一、裁定要做的，与它的一句理由

裁定（决策者转述）：**从全树每一个计划文件里删掉 `known_fail_ends_when`（30 条以上），并让
`check-plan-fields.mjs` 不再放行它**。理由是一句判断：*一个没有任何代码读的字段，写的是"归还
条件"，而它只存在于一个已经不存在的车道的记忆里。*

**后半句是对的**：没有一处代码**评估**这个条件成没成立——检查器只检查它引了反引号、并点名了该
条目自己钉的那句话。所以"归还条件"今天是一句承诺，不是一条读数。前半句（"没有任何代码读"）不
成立，而这不是措辞问题，是**删了会坏**。

## 二、读数

| 谁读它 | 读它做什么 |
|---|---|
| `test/tools/run-tests.mjs:264-286` | 标记载体的形状：**`known_fail_msg` 可以没有，只要 `known_fail_ends_when` 在**。注释里写着这是「作者的裁定 2026-11-21 02:20」选的形状，给的是"目标是数字、墙还在施工"的那种用例 |
| `test/tools/check-plan-fields.mjs:418-445` | 三条规则：每个标记必须有它（`mark-without-end-trigger`）；它必须引一段反引号（`end-trigger-names-nothing`）；它必须点名该条目自己钉的那句话（`end-trigger-names-another-reading`） |
| `test/tools/run-tests.mjs:1660-1665` | 跑完印 `[KNOWN] N of M known failure(s) carry no known_fail_ends_when` |
| `test/tools/selftest/run.mjs` | 用正则**断言那一行**出现在自测的输出里 |
| `scripts/plan-inventory.mjs` | 数标记时读它 |
| `docs/test-plans.md`、`docs/design/program-transforms/nested-transforms.cn.md` | 两处文档写着它 |
| `internal/reviews/2026-11-21-*`（三页） | 三页审查就是为它写的：end trigger 算不算 early warning、钉住的那句话会不会跟着墙移动 |

**删了会怎么坏**（这条是决定性的）：全树 **27 处、9 个计划文件**；其中 **11 个条目的标记只有这个
字段、没有 `known_fail_msg`**——

- `feat/transforms/compositions`：`compositions.jvp_vjp_f`、`nested_grad_min`、
  `nested_grad_through_a_binding`、`hessian_of_a_quadratic`；
- `feat/transforms/nested`：`nested.nest_square`、`nest_cube`、`nest_named`、`nest_lambda`、
  `nested.hessian`、`nested.confusion`、`nest_quartic`。

这 11 条今天全是 **XFAIL**（底树快层那 30 条已知失败里有它们）。删掉字段，它们在同一刻变成
`known_fail needs known_fail_msg or known_fail_ends_when`——**两个计划文件直接拒绝加载**，那不是
"红绿不变"，那是这一层少了 11 条用例。裁定说"删字段不会改变任何用例的红绿"，在这个读数下不成立。

## 三、三条路，各带代价

**A. 照裁定删。** 要让树继续加载，那 11 条必须先有 `known_fail_msg`：给它们钉一句今天的诊断。可
这 11 条的墙是"数字还没到"，钉一句就会随墙移动、要人反复重打——正是 2026-11-21 那条裁定要避开的
事。再加：撤检查器三条规则、撤 runner 的 `[KNOWN]` 读数与自测对它的断言、改两处文档、
`plan-inventory` 一处、`matrix.plan.toml` 自测矩阵 8 处。代价：**撤销负责人的裁定 + 改 10 个文件
+ 11 条标记的墙变成会动的钉子。**

**B. 留着字段，让它可判（推荐）。** 把 `known_fail_ends_when` 的取值收窄成**能被代码读的形状**：
`file#symbol`（等这个符号出现）、或一条能跑的命令（等它的退出码变 0）。检查器拒绝纯散文。这样
"归还条件"从一句承诺变成一条读数，裁定想修的那个洞被修上，而没有人要撤销自己的裁定。代价：定义
一个很小的词汇表 + 一条用例 + 现有 27 处要挑出句子里的那个符号（可以做，且是逐条判断，正好是可
以派给车道的活）。

**C. 只加读数（最小）。** 不动字段，加一条 `--tier-report` 读数：点名"这 N 个标记的归还条件无法
被任何代码评估"，让它在每次层报告里出现。代价：最小，但只是把洞照亮，不修。

**默认建议：B，且第一步先做 C**（读数先落地，再把 27 处收窄成可判的形状）。

## 四、这一轮我做了什么、没做什么

- **没做**：一个字都没删 `known_fail_ends_when`，检查器一条规则没动。理由在上一节。
- **做了**（同一批裁定里的 ②）：那 12 条 `unowned` 逐条按今天的读数处理——6 条还在静默豁免的换成
  角色主人，6 条说法过期或没有主人的删掉标记、让它照实报红。见提交与 `2026-11-24-defect-…` 那一页。
- 另有一条同形的：`feat/linear/effects_model_edges` 的两条标记写的是 `unowned batch item 9 (P0-2)`
  ——同一种病、另一种拼法，不在那 12 条里，留给下一批。

**要决策者做的**：A / B / C 选一条。选 A 之前请先看 §二那张表——它要撤销的是 2026-11-21 02:20
的裁定，那一页的注释还写着作者的日期。