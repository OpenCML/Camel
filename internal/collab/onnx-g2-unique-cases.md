# `lane/onnx-g2`：代码侧被盖住，测试侧有 4 个"没人有"的用例

**结论一句话**：`lane/onnx-g2@9cea23c74e` 的**代码**变化已被 `lane/onnx-run` 整片盖住，但它的**测试**侧有 4 个用例（含其 plan 条目）**在 `lane/onnx-run` 与 `origin/integration` 上都不存在** —— **删枝前必须先摘这 4 条；现在不删，等 `lane/onnx-run` 那一格判断要不要。**

- 枝：`lane/onnx-g2` = `9cea23c74e`（已打本地标签 `archive/onnx-g2-20261128`，**枝未删**）
- 盘时尖端：`origin/integration` = `352c892f4c`
- 判据：对每个用例文件做 `git cat-file -e lane/onnx-run:<path>` 与 `git cat-file -e origin/integration:<path>`，两者皆非零 ⇒ 无人持有；plan 条目由 `lane/onnx-g2:test/plans/feat/modules/onnx.plan.toml` 中 `name = "…"` 与其后最近的 `case = "…"` 配对得出。

## 这 4 条（用例名 / 文件路径 / plan 条目名 / 上一轮实测）

| plan 条目名 | 用例文件路径 | `lane/onnx-run` 有？ | 尖端 `352c892f4c` 有？ | 上一轮（`9cea23c74e` 上跑整计划）实测 |
|---|---|---|---|---|
| `export_ctrl_effect` | `test/cases/modules/onnx/export_ctrl_effect.cml` | 无 | 无 | **红** |
| `export_ctrl_effect_in_callee` | `test/cases/modules/onnx/export_ctrl_effect_callee.cml` | 无 | 无 | **红** |
| `pass_export_loop` | `test/cases/modules/onnx/export_loop_pass.cml` | 无 | 无 | **红** |
| `pass_export_slice_loop` | `test/cases/modules/onnx/export_slice_loop_pass.cml` | 无 | 无 | **红** |

（上一轮读数：`test/plans/feat/modules/onnx.plan.toml` 在 `9cea23c74e` 上 `total 147, pass 117, fail 29`，这 4 个名字都在那 29 条失败里；同一计划文件在 `lane/onnx-run` / 尖端上**不会跑到它们**，因为 plan 里没有这些条目。）

## 为什么"代码侧被盖住"仍要留测试

代码侧的证据（已被采纳，此处只记要点）：
- g2 净新增的 `src/stdlib/onnx/model.cpp`、`model.h`、`ort_library.h` 在尖端**全不存在**、在 `lane/onnx-run` 上**全都在**；规模上 `ort_library.h` +404（run）vs +195（g2）⇒ run 更全；
- g2 要退役的那批条目在尖端**仍在**、在 `lane/onnx-run` 上**已消失**。

测试侧不成立的理由：
- 上面 4 个文件在 `lane/onnx-run` 与尖端**都不存在**；反方向 `lane/onnx-run` 另有 7 个 g2 没有的条目（`pass_export_bytes_same`、`pass_export_refusal_located.arity`、`pass_export_refusal_located.compute_nodes_over_limit`、`pass_export_refusal_located.compute_nodes_two_shapes.spliced_after`、`…spliced_first`、`pass_export_refusal_located.growth_budget_json`、`pass_export_refusal_located.main_reaches_loop`）。plan 条目数：g2 = 150，onnx-run = 153。
- **红 ≠ 无用**：这 4 条是"写好了、还没通过"的用例，是最贵的资产；删掉之后没人会再去 `archive/` 里翻。

## 处置

1. **现在**：只打标签 `archive/onnx-g2-20261128`，枝保留。
2. **删枝前**必须先把这 4 个 `.cml` 与其 plan 条目摘出来（单独一笔提交，或转交 `lane/onnx-run` 那一格），**不能随枝一起消失**。
3. 是否作废由 `lane/onnx-run` 那一格判断（它才知道 `export_ctrl_effect*` 是否已被 `pass_export_ctrl_effect_json` 那族取代、`export_loop_pass`/`export_slice_loop_pass` 是否已被新条目取代）。