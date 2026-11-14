# 一个计划怎么表达「这次运行一个诊断都不产生」？

## 问什么

到本页为止，计划里能用 `expect_diagnostic_name` / `_code` / `_line` / `_column` 要求**某个诊断出现**，
却没有任何字段能要求**一个诊断都不出现**。这一条不是理论缺口：2026-11-14 之前，
`test/plans/feat/consistency/match.plan.toml` 的 `match.no_added_arm` 就是用
`expect_diagnostic_name = ""` 来表达它的。

那个空串的表达力是假的。`validateTest` 里诊断块的门是

```js
if (
    test.expect_diagnostic_name
    || test.expect_diagnostic_code
    || typeof test.expect_diagnostic_line === 'number'
    || typeof test.expect_diagnostic_column === 'number'
) {
```

所以 `""` 的作用只是**取消 `[defaults]` 继承来的那个名字**（`loadPlanTests` 的
`{ ...doc.defaults, ...test }` 让条目自己的值覆盖默认值，空串为假于是整块被跳过），
它既不表示「无诊断」，也不断言任何东西。写它的人读作「不产生诊断」，runner 读到的是「这一块不检查」。

今天能证明它是**活能力**而不是死字段的，是一次误判：我把它当死字段删掉之后
（commit `f3932eb76`），`feat/consistency/match` 红了 5 条 —— 删掉的不是一句叹息，是取消继承的那只手。
恢复它并写清注释是 commit `8e7a6b481`。

## 证据（原文照录）

基线 `5ec45c70f` 之上，`-Changed -ChangedBase 5ec45c70f -Jobs 8` 两次读数：

| 树 | 读数 |
| --- | --- |
| 删掉两条 `expect_diagnostic_name = ""` | 51 条里 5 条红：`match.complete.{nvm,fvm,jit,prl}`、`match.no_added_arm.nvm`，失败句是 `expected diagnostic SemanticDiag::MatchWithoutDefault` |
| 恢复之后 | 50 绿 1 红，唯一的红是既有的 `runner.backend_matrix` |

同一个字段在 `match.no_added_arm` 上，删掉前实际锁住的东西只有两件：
`expect_exit = 0` 与 `expect_output_not_contains = ["match_fail"]`（后者读的是 stdout+stderr 的拼接）。
**诊断一条都没查**。真正锁住「完整 match 不产生诊断」的，只有同级 `match.complete` 的
`verify_script`（`assert(input.stderr === '')`），一条脚本、一个条目。

## 现状（本批交付的形状）

**按默认建议（下面第 1 条）落地，没有等回复。**

- `test/tools/run-tests.mjs`：`TEST_FIELDS` 加 `expect_no_diagnostics`，`validateTest` 的无条件段加

  ```js
  if (test.expect_no_diagnostics === true) {
      if (result.diagnostics.length > 0) { /* failures.push(`expected no diagnostics, got N: ...`) */ }
  } else if ( test.expect_diagnostic_name || ... ) { /* 原有诊断块 */ }
  ```

  它是**无条件**字段（任何 kind 都读），不进任何 kind 门块。没写这个字段的条目语义一字未变；
  写了它的条目，原来的诊断块**不再被求值** —— 一个条目不能既要求有诊断又要求没有，取强的那条。
- `match.no_added_arm` 用它替换了那个空串；`match.complete` 仍留空串 + `verify_script`
  （脚本断言 `stderr === ''`，比「无诊断」更强，不必改）。树里因此还留着一处空串用法，
  它是「取消继承」的活例子，不是可删的垃圾。
- `test/tools/check-plan-fields.mjs`：真值表加一行（21 个无条件字段、表 29 行），
  `--verify-runner` 现在两个方向都比：三个 kind 门块按名字比，每个无条件字段必须在 runner 里
  被 `test.<字段>` 读到一次（`expect_stdout_exact` / `expect_stdout` 是 `test[field]` 循环读的，
  在那张「间接读」清单里写明）。
- 反证读数（探针计划 `test/plans/probe/zz-no-diagnostics.plan.toml`，跑完即删）：

  | 条目 | 程序 | 读数 |
  | --- | --- | --- |
  | `probe.a_clean_program_records_nothing` | `complete_matches.cml`（`std::nvm`） | PASS |
  | `probe.a_program_with_a_diagnostic_must_fail` | `no_case_matches_string.cml`（`SemanticDiag::MatchWithoutDefault`，`std::nvm`） | FAIL，`expected no diagnostics, got 1: SemanticDiag::MatchWithoutDefault` |

  「让程序产生一个诊断，条目必须红」这件事由此可复算：字段不是又一次「写了不读」。

## 三条修法

1. **给 runner 加 `expect_no_diagnostics`（默认建议，已落地）**：语义直接、一条断言、任何 kind 都能用，
   并且顺带给了「取消继承的诊断名」一个正当写法；代价是 runner 多一个字段，且要定一条规则：
   它和 `expect_diagnostic_*` 同时写时谁说了算（本页取「它说了算」，因为它是更强的断言）。
2. **让人用 `verify_script` 自己断言 `stderr === ''`**：不动 runner，但每条都要写脚本；
   而且它断言的是输出流，不是诊断记录 —— 一个 stderr 上有普通输出、诊断记录为 0 的程序会被它判红，
   反过来一个把诊断写进别处的后端也不会被它看见。
3. **只在 plan 注释里登记缺口**：最省，缺口仍在，下一个人还会以为那行空串在检查什么。

## 默认建议（按它开工，不等回复）

第 1 条。已按它落地：字段、两条反证读数、文档与检查器都到位；`match.no_added_arm`
现在真的锁住了「这个程序一条诊断都不产生」。

留给作者裁的只有两处措辞，不阻塞：

- `expect_no_diagnostics` 与 `expect_diagnostic_*` 同时写时，是「它顶替」还是「判成计划错误」
  —— 本页取前者（顶替），因为它不需要任何人写一条新规则去记「两个字段不能共存」；
- `expect_no_diagnostics = false` 现在与不写完全等价（`=== true` 才读）。要不要把它当
  「明确要求至少有一个诊断」，本页没有替作者定。