# 问题：限额处理器是"只在本来什么都不会印的时候才印"，还是无条件印

事实与读数，不含该选哪一方的判断。基座 `8f1a2f1ac`（`origin/develop`），分支 `handover/h524-held-sink`。

## 1. 要求说了什么

限额处理器这一条的原话是：

> 限额处理器打印 `e.what()`：命令行和调试器两处都要。不允许运行失败而输出一个字都没有。

"不允许运行失败而输出一个字都没有"是它的目的；"两处都要"是它的范围。**没有说**当运行本来就已经
把那条诊断印出来过（只是印在别的对象上）时，还要不要再印一次。

## 2. 今天落成的样子

三处限额处理器（`tools/camel-cli/main.cpp` 里 run 分支的 catch 与 main 末尾的 catch，
`tools/debugger/src/run.cpp` 里 `compileInto` 的 catch）现在都打印 `e.what()`，但**命令行 run 分支
那一处是有条件的**：只在它刚 dump 出来的那个 sink 里没有 error 时才打印到标准错误。另两处无条件。

理由是：run 分支那条路径上，撞限额的诊断几乎总是 runtime sink 自己的（例如运行期故障），
`dump` 已经把它整段印在了标准输出上，再打一行单行形态就是说两遍同一件事；而"一个字都没有"这种
情形在它这里指"dump 出来是空的"，正是这个条件放行的那一种。

**调试器那一处无条件**，因为它的 dump 目标是 `std::cout`，没有测试钉过它的字面内容。

## 3. 无条件打印会让哪 5 条红

无条件打印（即去掉那个 `if (!diags->hasErrors())`）在主机上 `-Plan smoke,test/plans/feat -Tier fast
-Jobs 8` 实测**恰好 5 条**由绿转红（其余 6175 条不变，基线 `total 6180 / pass 6161 / fail 0 /
skip 11 / known failures 8` 的名单逐条一致）：

| 计划 | 用例 | 被钉住的东西 |
| --- | --- | --- |
| `test/plans/feat/opt/fold_warning.plan.toml` | `constant_fault_reached.default` | `input.normalized.stderr === ''` |
| `test/plans/feat/opt/fold_warning.plan.toml` | `constant_fault_reached.fold` | `stderr` 恰好一行，且那一行是那条 `[Warn]` |
| `test/plans/feat/opt/fold_warning.plan.toml` | `constant_fault_reached.simplify_fvm` | 同上 |
| `test/plans/feat/opt/fold_warning.plan.toml` | `constant_fault_reached.json` | `JSON.parse(input.stderr)` 必须是一元数组 |
| `test/plans/feat/trans/macro.plan.toml` | `failed_depth.error` | stdout+stderr 合起来 `MacroEvaluationFailed` 只出现 1 次 |

前四条是运行期除零那一族：撞限额的诊断就是 runtime sink 自己的，dump 已经把整段印了出来，
多出来的那一行落在 stderr 上，于是 `stderr === ''`、"恰好一行"、`JSON.parse` 这三条断言各自失守。
第五条是 `std::macro` 求值失败：断言数的是 stdout 与 stderr 合起来 `MacroEvaluationFailed` 出现的
**次数**，多出来的一行把计数从 1 变成 2。

所以**要无条件打印，就得先改这 5 处期望**（`fold_warning` 里 4 条改 stderr 断言、
`macro` 里 1 条把计数从 1 改成 2，或者改成"至少一次"）。

（同一次测量里还有第 6 条红 `feat.consistency.runner` 的 `runner.backend_matrix`，与本条无关：当时
源码树里留了一个未跟踪的暂存目录，计划选择门禁因此报"匹配不到规则的文件"；把它移出工作树后即绿。）

## 4. 为什么没有替作者决定

- 5 处都是**被钉住的期望**（`verify_script` 里的字面断言），改它们不是本车道的权限；"不许改期望"
  是这一轮的前提之一。
- 无条件打印会与 `constant_fault_reached.json` 的载体形式冲突：那一条要求 `stderr` 整体是合法 JSON，
  一行纯文本诊断放进去就不是 JSON 了，要无条件打印就得连**诊断的载体**一起定（换到别处，还是
  让 JSON 模式只打数组、诊断另走一个流）。
- "两处都要"在**条件成立时都做到了**：命令行 run 分支在 dump 为空时打印（实测 `camel <22 个错误的
  程序>` 的 stderr 拿到那条 `Error[SemanticDiag::UnresolvedReference] ...`），main 末尾那处无条件打印
  （实测 `camel --max-errors 1 <2 个语法错误的文件>` 的 stderr 拿到 `Error[SyntaxDiag::MissingClosingDelimiter]
  ...`），调试器无条件打印（同一程序两边那一行 md5 相同，已去 ANSI：`aa3a20a89c5604143fb40a164630b11d`）。
  争议只在**运行期故障那一族要不要说第二遍**。

## 5. 要问的

1. **改那 5 处期望、让命令行 run 分支也无条件打印**，还是**维持"只在本来会空的时候打印"**？
2. 若选无条件：`test/plans/feat/opt/fold_warning.plan.toml` 的 `constant_fault_reached.json` 要怎么办——
   它在 `--output-format json` 下把 `stderr` 整体当 JSON 读；是让限额那一行改走别的流，还是这条期望
   改成"先 JSON 数组、后一行诊断"的新形状？

## 6. 没做到的

- 只在主机上量了"无条件打印会让哪 5 条红"，没有量"改完这 5 条之后还有没有别的计划受影响"
  （`-Plan smoke,test/plans/feat` 之外没跑）。
- 没有查 JSON 模式下"诊断另走一个流"会不会与既有的 `--output-format json` 约定冲突
  （`docs/cli.md` 里 stdout/stderr 的分工没有通读）。