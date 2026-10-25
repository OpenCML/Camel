# 教程示例页的两个问题（2026-10-25）

把 `docs/tutorial/examples/` 的文件当文件跑起来之后（`test/plans/feat/tutorial_examples.plan.toml`，逐字记录每个文件的输出），有两处只有作者能定。正文 `docs/tutorial/**` 一字未改。

## 1. `examples/README` 的表：要删掉哪些行

裁定是"示例索引只收成品（两条主线的成品），中间步骤只在教程里出现"。传达到执行侧的措辞是"不是成品的行要删"。

逐行核对 `examples/README.md` 的 23 行之后：**没有一行是中间步骤**。每一行都是能直接跑起来的完整程序，`test/plans/feat/examples.plan.toml` 也逐个运行它们。教程两条主线今天的中间版本（词频线的 `examples/wordcount/step2..8`、拟合线的 `examples/fit/step13..20`）还不存在；各章引用到的行（第 8 章 `text/word_frequency.cml`、第 9 章 `parallel_map.cml`、第 15 章 `ml/train_linear.cml`、第 19 章 `export_onnx.cml` 与 `python/rules/rules.cml`）也都是成品。

所以这次只改了文字说明（中英文各一句：表只列能独立运行的程序，中间版本只出现在教程里），**一行未删**。若本意是"各章点名、但将来会变成中间步骤的行也要先删"，请指出是哪几行。

## 2. 页面没写、文件会打到标准错误的警告

按文件运行时，5 个章节示例在标准错误上给出该章正在讲的那种警告，而页面的代码块只有标准输出：

- `04_block_return.cml:7:21` 与 `7:48`：`SemanticDiag::ExitInUnusedBranch`（两条，同一句话）——页面讲"块的值"，没提"`if` 分支里的 `return` 不离开函数"。
- `06_callee_decides.cml:6:5`、`06_sync_body.cml:6:5`、`06_unused_effect.cml:6:5`：`SemanticDiag::EffectNotExecuted`——第 6 章讲的正是这件事，而三条里只有 `06_unused_effect` 的页面写出了这条警告。
- `09_errors.cml:28:5`：`SemanticDiag::UnusedFallibleValue`。

计划现在把这 5 处的诊断名都钉住了（页面检查器对"页面写出来的诊断"也是这么钉的），所以"警告消失了"会变成一条读数。页面要不要把这些警告也写出来（或者改写示例让它们不再警告），由正文那条车道定；`examples/README` 与计划侧不需要再改。