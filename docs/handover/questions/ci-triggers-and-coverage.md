# CI 什么时候跑，跑哪一层

## 量到的事实（2026-10-24）

`ci.yml` 的触发器只有 `push: tags: ["v*", "V*"]` 与 `workflow_dispatch`（`ci.yml:7-21`）：**`develop`
上的合并一次都不跑 CI**。10-21 12:12 到 10-24 08:15 之间 develop 收了三天合并，CI 一次没跑；10-24
08:15 一次推了 `v0.2.28`/`v0.2.29` 两个标签，两个 run 同时变红。

CI 跑的是**快层**，不是慢层也不是全量：`ci-leg.yml:231` 的步骤是 `node scripts/test.js`（无参数），
目标是 `test/plans/smoke` + `test/plans/feat`（`test/runner.mjs:41-44`），tier 默认 `fast`
（`test/tools/run-tests.mjs:533`）。run `40077350582` 的 linux-x64 腿自报：

    [INFO] tier: fast, tests: 5956, jobs: 4
    [SUMMARY] total: 5956, pass: 5940, fail: 2, skip: 14

慢层（batch point 上 2922 条、8 条已知红）从来没有进过 CI；CI 的日志里
`fuzz_lossless_tree` / `churn_prl` / `progen_check` / `tree_training` /
`resnetbn_train_step_export` / `pass_export_equals_export_model` 一次都没出现。
所以“CI 一直红是因为跑了慢层那 8 条”**可以被日志推翻**。

## 这为什么不在本车道决定

触发范围、层数、以及“已知红要不要允许失败”是覆盖范围与策略，属语言作者/负责人；本车道只把代价量出来。

| 选项 | 代价 | 换来什么 |
|---|---|---|
| 保持 tags-only（现状） | 0 | 红只在打标签时暴露：10-24 两个标签因此全红，且 `7baf8ad58`（10-23 15:54）的红等了 16 小时才发现 |
| `push: branches: [develop]` 加进来 | 每次合并 3 腿 × 约 20-33 分钟 runner 时间（develop 一天约 10 次合并 ≈ 6-10 runner 小时） | 当天就能拦住 Windows 构建红与 `.ccache` 红，不用等到标签 |
| develop 上只跑 linux-x64 一腿 | 约 1/3 的上面那个数 | 拦住 Linux 侧一切；**拦不住 Windows**——而 10-24 的两类红里 Windows 是硬红 |
| 标签上再跑 `--tier all` 或慢层 | 慢层 1953-2192 s（`-Jobs 8`，主机实测） | 覆盖变宽；但慢层现在有 8 条红，CI 会永远红，得先把那 8 条的处置定了 |
| 加 runner / 分片 | 多一份并行 runner 配额 | 现状不需要：快层在 4 核 runner 上墙钟 219 s，33 分钟里 13 分钟是编译，编译才是瓶颈（ccache 已在） |

已知红那 8 条：`progen_check`、`tree_training`、毒化 rules 是 pre-existing；两个 ONNX 条目是 harness
把 margin 按 CPU 读（`d0278f224`），该改 harness 而不是标记允许失败；`gc_pacing.churn_prl` 与负载相关，
要改的是它的读数口径；`gpu.tiles/runtime_scalars`、`gpu.tiles/loop_boundaries_faults_permissions` 是
`3f8666d10` 引入的**用户能感觉到**的拒绝，必须先修。建议：**一条都不标允许失败**，先按
`docs/handover/batch-slow-2026-10-24.md` 的归因逐条处置。

## 本车道做了什么

没有改触发器、没有改层数、没有标记任何已知红。只把 CI 现在这一层（fast）的两条真红修掉，并把这张
代价表交上来。