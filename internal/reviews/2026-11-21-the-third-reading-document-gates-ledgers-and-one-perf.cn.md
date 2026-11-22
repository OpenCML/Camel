# 第三次读数：S1 之后第二轮，剩的是文档门、账本与一条性能

**跑批 `44878175494`，树 `526aa6c9b`，2026-11-21 15:52。**

## 三轮对照

| 腿 | 11-19 `865b08bc2` | 11-20 `8fab0d3e1` | 11-21 `526aa6c9b` |
| --- | --- | --- | --- |
| linux-x64 | 39 / 9761 | 15 / 9812 | **12 / 9827** |
| linux-x64-nojit | 40 | 15 | **11** |
| win-x64 | 50 | 24 | **19** |
| macos-arm64 | 49 | 24 | **20** |

## 我 11-20 列的四条「实的」：三条已绿

| 项 | 状态 |
| --- | --- |
| `showcase_s2_first_step_gradient` ×3（`mul_tangent` 没有转置规则） | **四条腿全绿**，转置规则已经在了 |
| `trans.cml / print_and_recompile`（转写回环） | **全绿** |
| `consistency.new_values / sort_rebinding.graph`（就地排序） | **全绿**，`array:sort_d_in_place` 回来了 |
| `opt.splice_place / perf_fib_smoke.opt_twice ×4` | **仍红**：`used 6.48 s of CPU against a cpu_budget_ms of 5.00 s`，四条后端一致，仍慢约 30% |

## 这一轮新出现的

| 用例 | 读数 | 判 |
| --- | --- | --- |
| `/ transpose_refusal_is_one_cause_in_four_places`（四条腿，plan id 为空） | `expected exit 0, got exit 2`；缺 `every cause has one word, one sentence, and a site that records it` | 今天新加的用例，检查器自己退 2 |
| `modules.stdlib_registry / registry.reference_is_generated`（四条腿） | `docs/stdlib-reference.md is up to date` 不出现 | **文档没重生成**，机械修 |
| `docs.links / docs.links.citations`（四条腿） | 引用了不存在的路径 | 同上 |
| `mm.gc_root_window / root_window.residual_mlp.moving_stress`（linux、nojit） | `used 32780 ms of CPU against a 120000 ms timeout, below the 4x margin` | 时间余量；这条压测比上限的 1/4 多了 2.8 s |
| `modules.nn / residual_mlp_large_bytecode`（linux） | `2590 ms` 对 `2500 ms` | 时间余量，擦线 |

## 仍在的（都与上一轮同一条）

`graph_draft_ledger.never_rises`（账本：三处还写着 `GraphDraft`）、`runner.backend_matrix`（账本口径）、
`modules.tiles / gpu_scope`（既有真缺陷）。

win / macos 的专属族比上一轮少了：`print_then_run_agrees_with_the_origin` 只剩 win，
`exemplars/05_effect_order` 已全绿。

## known_fail 的变动我核过

这一轮 plan 里加了 20 处、去掉了 12 处 `known_fail*`，全部是**重登记**（带 `known_fail_ends_when`，
写明这条什么时候会自己转绿），依据是 2026-11-21 02:20 作者对那十五个 transform 用例的裁定。
不是把红刷成绿，也没有绕过检查。

## 结论

**没有 S1 了。** 剩下三类：文档没重生成（两条）、账本（两条）、一条 30% 的优化性能。
前两类是机械的，第三类要一个决定。所以 `0.2.45` 现在要么等一个很短的机械回合，要么带着这三类发并各自写明。
