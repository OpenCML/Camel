# 可执行的判据：一句话 + 它落在哪

- **账本的四个数（files / references / constructions / decode）只能降，最后一步必须归零**：`test/cases/runtime/graph_draft_ledger/check.mjs` 对 `baseline.json` 执行，计划 `feat/runtime/graph_draft_ledger.plan.toml`（`tier = "fast"`，每次闸门都跑），记数行 `[LEDGER] recorded files 66 references 535 constructions 3 decode 6`。
- **诊断页每条示例是钉住的，加一条用例不能挪动别的诊断**：`diagnostics/examples.tsv` + `node scripts/diagnostics-catalog.mjs --verify-doc`（绿即 `docs/diagnostics.md is up to date`）；把被钉的用例移走再 `--write`，页面一个字节都不动。
- **受控文件必须被某条选中规则选中**：`test/plan-selection.json`，由 `test/tools/selftest/run.mjs` 的 `plan-selection:` 检查执行——`diagnostics/examples.tsv` 第一天就是这么被咬住的。
- **冻结只做一种环判定，`sync` 环被拒**：`test/plans/feat/opt/edges.plan.toml` 的 `test::edges_sync_cycle`；把冻结收回 4 种边，这条立刻红。
- **环丢弃的会话数看得见**：`std::stats` 输出的 `cycle_dropped_sessions`（`src/passes/trans/stats/stats.cpp`）。
- **`develop` 上造不出"多条改写合成的环"**：`opt-cycle-composition-not-found-on-develop.md`——拿掉守卫那一支，fast 层 7898 条一条不红。
- **一次落地值不值，看一个数**：闸门的 `[SUMMARY] total / pass / fail / skip / known failures`，不是"我看过了"。
- **中转仓库里某条引用该不该删**：`git merge-base --is-ancestor <sha> refs/camel-tmp/develop`（`build-host-staging-refs.md`）。
- **在主机上手动跑任何测试之前**：先复刻闸门的导出（`LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib`、`CAMEL_HOME=<树>/out/latest`、`CAMEL_TEST_SHOW_OUTPUT=1`），否则 `camel` 报 `GLIBCXX_3.4.36 not found`，读到的红全是自己造的。