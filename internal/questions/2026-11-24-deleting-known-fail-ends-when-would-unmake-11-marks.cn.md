# `known_fail_ends_when` 不删：改成可判的两种形状，跑测工具每次评估

2026-11-24 ｜ 来自 `lane/test-refactor-2` ｜ 对象：`test/tools/run-tests.mjs`、`test/tools/toml-lite.mjs`、`test/tools/check-plan-fields.mjs`

## 一、问句与答复

**问句**（决策者 2026-11-24 上午）：删掉 `known_fail_ends_when`、让检查器不再放行它。理由是一句
判断：*一个没有任何代码读的字段，写的是"归还条件"，而它只存在于一个已经不存在的车道的记忆里。*

**答复**（当日晚）：**不删。按负责人 2026-11-21 的裁定落实成可判的形状**——那条裁定本来就说这个
字段写的是"这条用例到时候要打印的那一行"。于是取值收成结构化的两种之一，工具侧三件事跟上：

| | 形状 | 谁读它 |
|---|---|---|
| ① | `known_fail_ends_when = { prints = "那一行" }` | 跑完在这条用例自己的输出里找它；找到 ⇒ 墙已经移开 |
| ② | `known_fail_ends_when = { passes = "另一条用例名" }` | 全部用例跑完后读另一条的判定；它过了 ⇒ 这条的标记已经结束 |

工具侧：

- **跑测工具每次评估**：条件已满足就报「已知失败已经结束、删掉标记」**并记失败**（严格 xfail）。
- **检查器拒绝纯散文**：只剩 ① 与 ② 两种形状能过；`prints` 必须是该条目自己钉过的一行，
  `passes` 必须点名树里真实存在的用例。
- **现有 27 处逐条改写**；写不出可判条件的那几条报回，不硬凑。

## 二、为什么"不删"：这条读数是这一笔的理由

全树原有 **27 处**（9 个计划文件 + 自测矩阵 8 处）。其中 **11 个条目的标记只有这个字段、没有
`known_fail_msg`**：

- `feat/transforms/compositions`：`jvp_vjp_f`、`nested_grad_min`、`nested_grad_through_a_binding`、`hessian_of_a_quadratic`；
- `feat/transforms/nested`：`nest_square`、`nest_cube`、`nest_named`、`nest_lambda`、`hessian`、`confusion`、`nest_quartic`。

`run-tests.mjs` 的载入规则是"**`known_fail_msg` 可以没有，只要 `ends_when` 在**"，所以删字段 ⇒
这 11 条同一刻变成 `known_fail needs known_fail_msg or known_fail_ends_when` ⇒ **两个计划文件拒绝
加载**。它们今天全是 XFAIL，加起来是底树快层 30 条已知失败里的 11 条。裁定原以为"删字段不会改变
任何用例的红绿"；在这个读数下，改的不是红绿，是**两个计划文件还能不能打开**。

那 11 条要按"删"处理，就得先给每条钉一句今天的诊断，而那些墙正是"数字还没到"——钉住会移动的墙
是 2026-11-21 那条裁定要避开的。**所以形状留下，但要变成可判的。**

## 三、落地：改了什么

1. **`toml-lite.mjs` 支持内联表**（`{ prints = "…" }`）：一个键重复在同一张表里被拒，与表里重复
   键同理。这是"取值结构化"在 TOML 里最轻的写法，字段名不变，检查器的"已知字段"判定不变。
2. **`run-tests.mjs`**：
   - `knownFailEndCondition()` 把取值读成两种形状之一，别的一律视为非法；
   - 载入时**拒绝纯散文**，报文点明两种形状与"为什么句子不行"；
   - **严格 xfail**：`prints` 在标记触发处就地评估（输出里出现了那一行 ⇒ `[FAIL]`，
     `known failure has ended: this entry already prints "..." -- delete the mark`）；
     `passes` 在**全部用例落定之后**评估（它要读另一条的判定，而那条可能是最后一个跑完的），
     命中就把该条从被豁免的红移进失败，并在结果之后印
     `[KNOWN] known failure has ended: this entry waits on \`<case>\`, which passed -- delete the mark`；
   - `[XFAIL]` 行照旧先印，结算块把它重读一遍——两类读数用同一句话，所以对不上时看得见。
3. **`check-plan-fields.mjs`**：三条散文规则换成结构性的判据——取值必须是两种形状之一；
   `prints` 必须是该条目自己钉的行（`end-trigger-names-another-reading`）；`passes` 必须点名树里
   存在的用例（新判据 `end-trigger-names-nowhere`）。全树 `findings` 仍是 **317**、`fields=29`、
   上限不变。
4. **27 处逐条改写**：16 处 `prints`（值取自该条目自己钉的第一行）、9 处 `passes`（"这条自己转绿"
   与"等另一条"）、2 处由人判（`with_grad_from_the_library`、`vmap_grad_f`——前者要等库改名，后者要等
   三个数，都写成 `passes = "<自己>"`，即"这条转绿即结束"）。**没有一条硬凑**。
5. **自测**（`test/tools/selftest/knownfail/`）：新增 `ended.mjs`/`anchor.mjs` 与三份计划——
   `ends_arrived`（`prints` 已满足 ⇒ `[FAIL]` + 删标记）、`waits_on`（`passes` 已满足 ⇒ 结算块）、
   `prose_ends`（散文 ⇒ 载入即拒）。`[known_fail gate]` 现在读作
   `missing-owner=refused; …; prose-trigger=refused; all-three=accepted; roster=named; strict-xfail=prints+passes`。

## 四、留给下一批的

- `feat/linear/effects_model_edges` 的两条标记写的是 `unowned batch item 9 (P0-2)`——同一种病、另一种
  拼法（主人不是角色），不在那 12 条里。
- 如果有车道愿意把 `prints` 再收窄成"必须是 `expect_stdout_contains` 里那条"，那是检查器一行的事；
  今天要求的是"自己钉过的行之一"，因为 `expect_stdout` 整块的那种条目钉的是多行。