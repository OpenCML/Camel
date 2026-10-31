# 解构计数诊断的迁移影响：全树 265 处 `let (`，3 处需要改，0 处在 stdlib/examples/test

**一句话**：`let (x, y) = <四元组>` 以前静默绑定前两个、丢掉其余，现在报
`SemanticDiag::TupleUnpackingCountMismatch`，位置在 `let` 上。**树里只有 3 处靠这个静默，
全在 `showcase/`**；`stdlib/`（46 个 `.cml`）、`examples/`（37 个里剩余的）、`test/cases`（fast 层
8032 条测试）里一处都没有。外部程序不在这 3 处之列——**它们就是这次迁移的读者**。

## 数字怎么来的

grep 只数得出 `let (` 有 **265** 处，数不出"哪几处的名字比值少"——那要看值的类型（元组长度），
只有编译器知道。所以清单是编译出来的，不是数出来的：

| 范围 | 文件数 | 需要改 |
|---|---|---|
| 全树 `.cml`（`git grep -l 'let ('`） | — | 265 处 `let (` 是分母 |
| `stdlib/**/*.cml` 全部编译 | 46 | **0** |
| `showcase/**/*.cml` + `examples/**/*.cml` 全部编译 | 37 | **3**（全在 showcase，examples 0） |
| `test/plans` fast 层整层跑 | 8032 条 | **0** |
| `docs/` | — | 不编译（散文里的例子，本轮改了 `docs/language/07_statements.md` 中英两条规则） |

## 3 处

| 位置 | 写法 | 改成 | 为什么以前不报 |
|---|---|---|---|
| `showcase/d4-mcts/selfplay.cml:101` | `let (boards, results) = loop bs, rs, g` | `let (boards, results, _)` | 循环带三个状态、名字写两个；第三个是计数，本来就没用 |
| `showcase/c6-vae/train.cml:102` | `let (trained, optimizer) = loop m, opt, step` | `let (trained, optimizer, _)` | 同上：`step` 是循环自己推进的计数 |
| `showcase/d5-tree-lstm/train.cml:151` | `let (trained, optimizer) = loop p, opt, step` | `let (trained, optimizer, _)` | 同上 |

三处都是**多出来的那个是无用计数**，所以 `_` 是准确的写法，不是补一个名字凑数。三处改完各自的
计划都跑过：`showcase_d4`（20 条）、`perf/showcase_c6`、`perf/showcase_d5`，29/29 PASS；
`d4-mcts` 改后重跑 16 局 × 10 步，loss 首位 `0.640625`、末位 `0.261926`，与改前逐位相同——
`_` 没有换掉任何一个被绑定的名字。

## 对外部程序意味着什么

1. **以前温绿、现在会红**的只有一类写法：名字少于元组元素个数的解构。报在 `let` 上，一句话说清
   "名字的个数与值的个数不一致"，不会指到别的行。
2. 修法两条：补上 `_`（有意丢）或补全名字（本来就要用）。
3. **反方向（名字多于元素）本来就会报**，但以前报的是 `InvalidAccessIndex: Invalid access index:
   '2'`，箭头指在 `func`（1:1）——读者会去改函数。现在两个方向同一条诊断、同一个位置，
   这也意味着**依赖旧报文文本做匹配的工具会看到变化**（诊断名从 `InvalidAccessIndex` 变成
   `TupleUnpackingCountMismatch`）。
4. `let _ = e`（单值丢弃）不受影响，`var _` 仍不是写法。

## 复现

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:/Projects/Camel-wt/h-mcts -Plan 'test/plans' -Tier fast -Jobs 8
# 定向：两个方向各一条
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:/Projects/Camel-wt/h-mcts -Plan 'test/plans/feat/compile/lambda_tuple_param.plan.toml'
```

新钉住的两条用例：`test/cases/compile/err_let_unpack_arity.cml`（四状态写两名字，`expect_diagnostic_line = 2`）、
`test/cases/compile/let_unpack_wildcard.cml`（`_` 在首尾两个位置都能写，普通的两个名字仍绿）。