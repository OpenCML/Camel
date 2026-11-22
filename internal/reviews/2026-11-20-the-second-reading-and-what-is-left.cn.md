# 第二次读数：六条 S1 全清，剩下四条实的

**跑批 `44676862205`，树 `8fab0d3e1`，2026-11-20 13:31。**

## 前后对比

| 腿 | 11-19（`865b08bc2`） | 11-20（`8fab0d3e1`） |
| --- | --- | --- |
| linux-x64 | 39 / 9761 | **15 / 9812** |
| linux-x64-nojit | 40 | **15** |
| win-x64 | 50 | **24** |
| macos-arm64 | 49 | **24** |

用例总数从 9761 长到 9812（新增 `runtime.graph_draft_ledger`、`showcase_s2_first_step_gradient`、
`docs.no_unfinished_merge_pattern`、`compile/_s16_determinism` 等）。

## 我 11-19 报的六条 S1：四条腿上一个都不出现了

`perm_query_types`（合法程序被拒）、`onnx` 两个该拒不拒、`commit_refuses_an_arm_reading_its_join`、
`fusion_group.profile.safe`、win 的 `stream.generators_shapes_consumed` —— 逐条在 linux / nojit / win / macos
四份红名单里确认过，全部消失。`beea20ad6`、`48156fcaa`、`fe8…`、`514e39692`、`f34cc72cc` 这一批是它。

## 四条腿都红的，现在只剩这些

| 用例 | 读数（原文） | 判 |
| --- | --- | --- |
| `showcase_s2_first_step_gradient / first_step.tensor_vjp_answers_the_step.nvm` 等 **3 条** | `stdlib/vjp.cml:137:13 [Error]: transforms: transpose has no transpose for this function: the ru...` / `TRANSPOSE function=autodiff::mul_tangent<float> graph=509 type=(float, float, float) => (float, float) nodes=7 arms=0 rhs=add_t,mul_t` | **真缺口**：`mul_tangent` 没有转置规则，matmul_t 的梯度路径跑不起来。三条都是它 |
| `runtime.graph_draft_ledger / never_rises` | `[LEDGER] RED entered files: src/passes/test/rules.cpp`、`undo_selfcheck.cpp`、`src/runtime/draft_undo.cpp`；`an entered member is a new direct connection to the type this step removes` | **不是运行时缺陷**：这是一个「重构有没有做完」的账本，三处还直接写着 `GraphDraft` / `DraftWritings` |
| `trans.cml / print_and_recompile` | exit 1，`cml round trip: PASS` 不出现 | 转写回环仍坏 |
| `consistency.new_values / new_values.sort_rebinding.graph` | 图里没有 `array:sort_d_in_place` | 就地排序仍不见 |
| `consistency.runner / runner.backend_matrix` | 计划 `commit_tier_inplace.plan.toml` 没被任何用例路径选中；CUDA 感知测试 38 对钉的 37 | 账本口径 |
| `exemplars / 05_effect_order` | `FAIL 05_effect_order/dump`：`stdout differs ... at line 95` | 期望文件要跟着改 |
| `docs.ai_reference / docs.ai_reference.current` | `327 entries` / `552 entries` 与源不一致 | 重生成 |
| `docs.links / docs.no_unfinished_merge_pattern_sees_all_four_forms`（linux only） | `catches 4 line(s)` 不出现，实际 `the old one-liner catches 1` | 检查器自己的口径 |
| `modules.tiles / gpu_scope` | 既有真缺陷 | 老面孔 |
| `opt.splice_place / perf_fib_smoke.opt_twice × 4`（linux、nojit） | 6410–6550 ms 对 20000 的 4 倍线 | `std::opt` 仍慢约 30% |

## 平台专属族未变

win：`frontend.ast_snapshot_drop_guard`、`tensor erfc_grad.f64`、`rules` ×3、`load_class` ×2、
`exemplars/08`、`debugger.dap_fusion_group`、`plan_refs.every_case_balances`、`unicode.paths`、
`exit_reporting`、`nested.nest_quartic`。
macos：`trans.cpp` ×3、`tensor gelu/erf/erfc` 四个、`rules` ×3、`load_class` ×2、`debugger.dap_fusion_group`。
这些在 `v0.2.44` 之前就在，照旧不带。

## 结论

**离可发只差四条实的**（showcase 的 `mul_tangent` 转置、`print_and_recompile`、`sort_rebinding`、`opt_twice` 的性能），
其余都是账本、期望与文档。六条 S1 清掉之后，`0.2.45` 才第一次够得着；这四条清掉我就切。
