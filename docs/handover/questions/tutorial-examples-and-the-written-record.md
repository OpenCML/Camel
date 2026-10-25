# 教程示例的两份书面记录：`examples/README` 的表格，与页面没写出来的警告

`docs/tutorial/examples/` 的文件现在逐个跑起来了（`test/plans/feat/tutorial_examples.plan.toml`，四个后端，输出逐字记录在 `test/plans/feat/golden/tutorial_examples/`）。跑起来之后，两份"书面记录"和文件的真实输出之间存在两处需要裁定的地方。正文 `docs/tutorial/**` 本 lane 一字未改。

## 1. `examples/README` 的表格：哪些行算"成品"

裁定是"示例索引只收成品（两条主线的成品），中间步骤只在教程里出现"。执行侧收到的措辞是"不是成品的行要删"。

**逐行读数的结果：23 行没有一行是中间步骤**，每一行都是能直接跑起来的完整程序，`test/plans/feat/examples.plan.toml` 也逐个运行它们：

- 教程两条主线今天的中间版本还不存在（词频线的 `examples/wordcount/step2..8`、拟合线的 `examples/fit/step13..20`），所以没有"是某条主线的一步"的行。
- 各章点名用到的行也都是成品：第 8 章 `text/word_frequency.cml`、第 9 章 `parallel_map.cml`、第 15 章 `ml/train_linear.cml`、第 19 章 `ml/export_onnx.cml` 与 `python/rules/rules.cml`。
- 第 19 章那个 Python 项目（`examples/python/rules/`）不是一个文件而是"Python 项目 + `rules.cml` 模块"，索引里以 `python/` 一节出现，也是成品。

**选项与代价**

- **(a) 表只列成品、文字写明这个口径，一行不删**（本次落地）：索引今天就是对的；代价是若将来某个中间版本落到 `examples/` 下，需要有人记得它不该进表。
- **(b) 逐行点名删掉某些行**：需要裁定方指出是哪几行——按上面的读数，本 lane 找不到这样的行；若本意是"各章点名、将来会变成中间步骤的行也要先删"，那就是删第 8、9、15、19 章点到的四五行，代价是教程里"它就在 `examples/` 下"的指路断掉。

**倾向 (a)**：裁定原文与逐行读数一致，本次已按 (a) 落地（中英文各加一句"表只列能独立运行的程序"）。

## 2. 页面没写、而文件会打到标准错误的警告

按文件运行时，5 个章节示例在标准错误上给出该章正在讲的那种警告，而页面的代码块只有标准输出：

| 文件 | 位置 | 诊断 | 页面 |
|---|---|---|---|
| `04_block_return.cml` | 7:21、7:48 | `SemanticDiag::ExitInUnusedBranch` | 只写"块的值"，没提"`if` 分支里的 `return` 不离开函数"（该章的例子正好踩在这条警告上） |
| `06_callee_decides.cml` | 6:5 | `SemanticDiag::EffectNotExecuted` | 没写 |
| `06_sync_body.cml` | 6:5 | `SemanticDiag::EffectNotExecuted` | 没写 |
| `06_unused_effect.cml` | 6:5 | `SemanticDiag::EffectNotExecuted` | 写了 |
| `09_errors.cml` | 28:5 | `SemanticDiag::UnusedFallibleValue` | 没写 |

计划现在把这 5 处的**诊断名**钉住了（页面检查器对"页面写出来的诊断"也是这么钉的），所以警告出现或消失都会变成一条读数。

**选项与代价**

- **(a) 页面保持原样**（本次落地）：页面只写这一章要讲的东西；代价是读者自己跑一次会多看到一到两条警告，其中 `04_block_return` 那两条正好把该章的例子讲成了另一件事。
- **(b) 页面把这些警告也写出来**：读者看到的和终端一致；代价是第 6 章三条示例的页面会长出三段几乎一样的文字。
- **(c) 改写示例让它们不再警告**（例如 `04_block_return` 换成真从函数返回的写法）：例子更干净；代价是 `06_*` 三条的警告就是第 6 章的教学内容，不能去掉——(c) 只对 `04_block_return` 与 `09_errors:28` 可行。

**倾向 (a) + 对 `04_block_return` 选 (c)**：其余四条页面不写警告可以接受（警告是"文件的读数"，计划已经钉住）；`04_block_return` 那两条应当由正文那条车道决定是改例子还是写进页面。