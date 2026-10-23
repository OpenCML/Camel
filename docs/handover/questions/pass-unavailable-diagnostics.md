# 问题：模块在、遍不可用时诊断该说什么（H130，note ⟨5dd110d8⟩）

## 问题
H130 把"pass not found"拆成三种情形：名字背后没有模块（`UnrecognizedGraphPass` 加全部遍列表）、
模块在但加载失败（`InvalidPassParameter`，但 `help:` 仍是与模块无关的"参数按 `key=value` 写"）、
模块在但遍不在（`PassNotProvidedByModule` `0x000186` 与 `PassNotExportedByModule` `0x000187`，
只说模块名与它提供的遍）。三件事要不要再改：加载失败该不该有专用 `help`；由 C++ 注册遍的模块
该不该列举"它注册了哪些遍"；那两个代码要不要合成一条。

## 选项与代价
| 选项 | 代价 |
|---|---|
| 1. 给"pass 模块不可用"另写一段 `help`（A） / 保持通用 help（B） | A：`diagnostics/registry.tsv` 多一段文本与用例；B：模块加载失败时用户读到的建议与事实无关（现状，`InvalidPassParameter` 的 help 讲的是参数怎么写） |
| 2. 由 C++ 注册遍的模块也列举"它注册了哪些遍"（A） / 只说"it provides no passes"（B） | A：要让诊断读 C++ 的遍注册表（`registerModulePass` 的路径），是 H130 没走的那条；B：`onnx::nosuchpass` 会说"onnx 不提供任何遍"，而 `onnx::export` 就在那里（现状，用例注释已记下这处不足） |
| 3. 保留两个代码（A） / 合成一条消息里的从句（B） | A：一个事实两个码，而 [`reviews/H49-plan.md`](../reviews/H49-plan.md) Q5 的规则是"同一个事实两个码，正是 5.01 要清掉的那类重复"；B：JSON/脚本失去"没提供"与"声明了没导出"的区分（`pass_not_found.plan.toml` 按名字断言），既有期望要改 |

## 倾向
1 选 **A**（说实话的 help 很便宜），2 选 **A**（"它不提供任何遍"在 `onnx` 这种模块上是错话，
宁可不说也不要反着说），3 选 **A**——但理由是这两个码不是"同一个事实"：修法不同（换个名字 / 补
`export`），JSON 那一侧也确实在分辨它们，所以 5.01 那条清理规则不适用；若作者仍要一条码，B 可以
立刻照做。**判断在决策者**（诊断形状与措辞）；只有 3 选 B 才需要作者点头，因为那改变用户可见的
输出形状。

## 出处
- note：`git notes --ref=handover show 5dd110d8`，报告《H130》§9「未决问题（留给语言作者）」，
  三种情形的分界见 §1。
- 现状：`throwUnresolvedPass`（`src/execute/pass.cpp`）按 `resolved.problem`、`resolved.missingPass`、
  其余三种情况分派；`listAvailablePasses` 是"名字背后没有模块"时的全局列表。
- 现状证据：`test/plans/feat/modules/pass_not_found.plan.toml`
  （`pass_absent_from_a_declaration_module` 期望 "it provides no passes"；
  `pass_module_that_cannot_be_loaded` 走 `InvalidPassParameter`；`a_name_with_no_module_stays_unknown`
  锁住全局列表那条路）；`test/plans/feat/compile/diag_json.plan.toml` 的
  `unknown_pass_of_known_module` 现在用 `ckpt`（由 C++ 提供、没有声明文件的模块）做主体。
- 通用 help 的位置：`diagnostics/registry.tsv` 的 `InvalidPassParameter`（`0x000060`）。
- 相邻记录：报告《H124》§9 第 4 条（"pass not found" 丢掉模块自己的诊断）建议另开一行，H130 就是那一行；
  H179 给 `onnx` 加了声明模块，因此它从"原生模块"那一类移到了短诊断这一类。