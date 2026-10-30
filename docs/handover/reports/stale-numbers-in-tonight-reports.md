# 今晚报告里过时的数字：一行一处，带今天的读法

> **一个数字一旦被写进报告，它就开始过时；报告里带数字的句子，要么带取数命令，要么带一个 sha。**

日期：2026-10-31。范围：`docs/handover/reports/` 与 `docs/handover/reviews/` 里今晚写成、带具体数字的结论。只查**今晚可能变**的那几类——表的大小、程序读数、闸门有没有、站点计数；历史账不核。看到旧值请按"依据"一栏重取；**不要改那些报告**，它们各自主人的账。

## 表

| 报告:行 | 写的旧值 | 今天正确的值 | 依据（命令 / sha） |
|---|---|---|---|
| `reports/gpu-g4-g5-argmax-axis.md:295` | "缺口停在 26、`operations()` 停在 21" | `operations()` 现在 **24** 条：22 个字面名字（`neg`…`broadcast_like`）+ `tensor:sum`、`tensor:mean` 两条 `emplace`。缺口数本次没复核 | `modules/tiles/src/graph_regions.cpp:46-79`；`git grep -n "result.emplace(table.intern" -- modules/tiles/src/graph_regions.cpp` → 2 条 |
| `reports/gpu-g4-g5-argmax-axis.md:133` | "C2 的读数才允许从 21 = 8 + 13 变成 21 = 9 + 12"（把表的大小当 21） | 表是 **24**；`TILES_CENSUS ops=` 报的是**这个程序用到的**算子数，不是表的大小 | 同上；`modules/tiles/src/census.cpp:74` |
| `reports/gpu-g4-g5-gap-list.md:134`、`:157` | "`TILES_CENSUS ops=21 device=8 gap=13`，那 21 个名字逐条存在"——被用来证明表里有 21 条 | `ops=` 是程序里的算子数（`census.cpp:74` 的 `surface.operators()`），**不能反推表的大小**；表 24 条。那个程序用 21 个算子、表 24 条，两件事互不证明 | `census.cpp:74`；`graph_regions.cpp:46-79` |
| `reports/gpu-g4-g5-plan.md:13`、`:27` | "今天（表内）20 个名字" | **24** | `graph_regions.cpp:49-78`（22 个字面 + 2 条 `emplace`） |
| `reports/code-review-onnx-tiles.md:82` | "`operations()`（17 个算子……）" | **24** | 同上 |
| `reports/signature-mirrors-and-whether-they-drift.md:57` | "改它不改变任何行为，不改它也不会让任何测试变红" | **有闸门**：`registry.signatures_match` 在 `test/plans/feat/modules/stdlib_registry.plan.toml:81`，每次 `npm run test` 都跑 `--verify-ops`（`:87`）；它拿 `camel inspect --ops` 与声明文件比对，整条签名都比（含形参名，`scripts/stdlib-reference.mjs:1476`），改错名字或条数会红。缺口只剩"签名文字与 C++ 注册类型的逐字对应"（`inspect --ops` 读的是声明侧）。闸门读数（2026-10-31）：550 条重载与声明对比 | `stdlib_registry.plan.toml:81`、`:87`；`scripts/stdlib-reference.mjs:1476` |
| `reviews/questions-2026-10-31-the-ruleset-module-has-no-rules-in-it.md:1`（正文 `:19`） | "规则集模块里是空的 / 索引到的是空的" | `rule` **0 条是对的**，但模块不空：9 个 `func` + 1 个 `type`（`Lift<T>`），其中 7 个是 `*_jvp`；文件 4449 字节，被 `test/cases/compile/rulesets/jvp_lift.cml:3` 导入。空的是**规则索引**——10 条 `rule` 在 `autodiff.cml`，它不以 `rulesets.` 开头 | `git grep -c "^export rule" -- stdlib/autodiff.cml stdlib/rulesets/jvp.cml` → 10 / 0；`Select-String stdlib\rulesets\jvp.cml -Pattern '^(export )?func '` → 9，`-Pattern '^(export )?type '` → 1 |
| `reports/assert-release-hole.md:37`（`:33`、`:35` 是前两批） | 第三批后："`ASSERT` 1199、`GUARD` 25、release 里会说话的 237" | 今天按同一算法：`ASSERT` **1203**、`GUARD` **37**（`release 里会说话的` 本次没复核） | `git grep -o "ASSERT(" -- src include modules tools test ':!include/camel/utils/assert.h' \| Measure-Object` → 1203；`git grep -o "GUARD(" -- src include modules tools test \| Measure-Object` → 37 |

## 为什么是这四类先过时

- **表的大小**（`operations()`）：每加一个设备程序就变一次，而引用它的句子通常只写一次。
- **程序读数**（`TILES_CENSUS ops=`、`major=`、`TILES_OPERATOR_DECLARATION`）：它们量的是**跑的那个程序**，不是语言或表；两个数看着一样，不代表同一个东西。这一条是今天三处里最贵的一处——`ops=21` 与表里的 21 个名字对上了，所以没人再问表是不是更大。
- **闸门有没有**：一个检查只要在计划文件里，就每次都在跑；"没人守"通常是"我没看到它跑"。
- **站点计数**（`ASSERT`/`GUARD`）：每改一处守卫就变，报告里的进度条只在写它的那一刻成立。

## 核过、今天没变的

- `stdlib/signatures/{autodiff,ckpt,nn,tensor,tiles}.cml`：4/1/20/43/7 = **75** 条（`git grep -c "func " -- stdlib/signatures`）。
- `stdlib/autodiff.cml` 的 `export rule` = **10**；`stdlib/rulesets/jvp.cml` = **0**。
- 规则集模块的声明：**9 个 `func` + 1 个 `type`**。

## 这张表不做什么

- 不改任何报告：旧值留在原处，读的人按"依据"重取。
- 不核历史账（上一轮、上几轮报告里的数），也不核耗时读数——耗时几分钟内的漂移就有 ~10%，写下来当天就过时，那类句子应当带"测量时间 + 两棵树背靠背"的限定。