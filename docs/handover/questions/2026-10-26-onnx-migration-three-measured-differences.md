# `onnx` 迁移里的三条实测差异（数量、丢弃写法、导出器不填位置）

（都不阻塞施工，但都改了草稿里的数字或写法，所以先告诉你。）

## 一、要改的用例是 **18 个**，不是 7 个

你写的是"`refusals` 那 7 个用例按草稿从故障改成错误值"。实测按"**调用形式下会被拒绝**"来数：`test/cases/modules/onnx/refusals/` 里 6 个（`as_unproven`、`as_always_fails`、`as_constant_fails`、`tuple_example`、`example_mismatch`、`function_branch`）＋`export_var_append`、`export_unsupported`、`export_impure`、`export_capability`、`export_ctrl_effect`、`export_ctrl_effect_callee`、`export_nontail_recursion`、`export_tree_recursion`、`export_loop_carried_array_field`、`export_canonical_kernel`、`export_strict`、`export_unclassified` ＝ **18 个**（后 8 个是这次跑出来的）。改法：`RuntimeDiag::RuntimeError`＋故障 ⇒ `RuntimeDiag::UnhandledError`（`0x04000051`）＋错误值，位置取错误值里记的被拒绝节点；用例本身改成 `main(): void!` 且把调用放在**最后一句**（错误值就是程序结果）。**这只是数量更正，方向没变。**

## 二、忽略结果的地方必须显式丢弃：`let _ = export_model<out: path>(f, x)`

草稿把调用形式的结果定成 `void!`。于是 `export_model(...)` 直接当语句写（137 个调用点里的绝大多数）会各出一条 `SemanticDiag::UnusedFallibleValue`（"an error it holds is lost"）。这不是纸面问题：`onnx.plan.toml` 的 `pass_export_var_param_inlined` 有 `expect_output_not_contains = ["error"]`，而警告文本里带 "error"，直接判红。

**不改断言的唯一出路**是按语言自己"有意丢弃"的写法（`docs/language/07_statements.md:55`）写成 `let _ = export_model<out: path>(f, x)`（比草稿表里那行多一个 `let _ =`）。施工方按这个口径做迁移；**若你想要的不是这样**（例如调用形式的结果不该是 `void!`，或该丢弃不该警告），说一声，改回来只是一次文本替换。

## 三、导出器有两条拒绝**不填 location**（既有空缺，不是这次改出来的）

`example_mismatch`／`tuple_example` 在导出器里只 `throw ExportError(message)`（`checkExamples` 那条路径），**没有填位置** ⇒ 调用形式下错误值只带算子名 `<onnx::export_model>`，报告落在 `main` 那一行并附注"the error was made by …"；而遍形式靠调用点 origin 能定位到 `export_model(...)` 的那一列。**要不要让 `checkExamples` 补 location**（让两种形式报同一处）？这是个后续项，施工方没有顺手改语义。