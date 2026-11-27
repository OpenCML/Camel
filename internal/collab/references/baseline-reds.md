# 基线红清单：没人认领的缺陷，不是"已知可接受"

口径（协调者 2026-11-27 定，`lane-editview-commit` 交回时写入）：
- 基线红 = 在某个尖端上**已经红着**的用例。它们是**没人认领的缺陷**，不是"已接受的现状"。
- 任何"新增红 0"的读数，都只在**同一个尖端**上才成立（见 `pitfalls.md`「同一个绿还必须在同一个尖端上」）。
- 本页只做分组登记，供派卡时挑；分组来自各车道的逐名读数，附 `[provenance] tip`。

## `feat/opt` 9 条（`[provenance] tip d41b67101f` 内容对照，`371f202832` 上逐名相同）

| 组 | 用例 |
|---|---|
| 该做而拒了（3） | `loop_wait_after_inline.non_contiguous_loop.refused`、`loop_wait_after_inline.two_loops_interleaved.refused`、`commit_refusal.loop_body_read` |
| 顺序/纯度（1） | `order_edge_on_a_pure_node_is_refused` |
| 常量折叠诊断（1） | `fold_cast.failing_fails_at_run_time` |
| writing/fusion（2） | `opt.writings.greedy_fusion_reads_no_writing`、`opt.writings.two_effects_wall_clock` |
| 环境/数据（2） | `progen_seed_4`、`nn.mnist` —— **这两条要单独确认原因，不许默认忽略** |

## `feat/compile` 16 条（同上两个尖端，逐名相同，`known failures: 3`）

| 组 | 用例 |
|---|---|
| `sync.*`（4） | 一组四条 |
| 单条（4） | `passes_never_run_json`、`stream.interval_loop_not_materialised`、`region_roots.writing_freedom_of_two_writes`、`cml_var_holding_a_lambda` |
| `transpose` 族（8，全 `.nvm`） | `transpose_loop_escape.*` 六条 + `transpose_runs_a_loop_rounds_backwards.nvm` + `stated_mean_axis_answered_by_the_engine.nvm` |

注：`transpose` 那 8 条即 `task-325`（C4 挡路的那条）要处理的对象。

## 命名口径

红表里写 `poison:<plan>:<case>`，磁盘上的用例名是 `poison_<plan>_<case>`。**条数对不上时先核这两套写法，别先怀疑回归。**