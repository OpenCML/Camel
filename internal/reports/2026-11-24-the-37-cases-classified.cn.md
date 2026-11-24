# 37 条"退出 1 却没有一句说明"：逐条分类

**这是一张分类表，不是一次修复。** 37 条用例取自
`test-refactor-2` 的移层红表（`cause_class: exit-1-with-no-line-saying-why`），证据取自那次运行的
完整 `stdout.txt`/`stderr.txt`/`meta.json`（169 个失败目录，本表只覆盖报告点名的 37 条）。

## 判据

- **① 产品真的坏了**：证据行里有一句 `[Error]: …`、`Unresolved reference: …`、
  `the declaration … has a body`、`a semantic error` —— 执行器说了为什么，理由是产品的。
- **② 期望或生成物过期**：证据行要求"重新生成/重新记账"（`is not what … produces`、`--write`、
  ledger/ratchet 的一行账目）—— 用例的期望落后于树，不是产品坏了。
- **③ 诊断被吞 / 没有诊断**：`stdout`/`stderr` 有字节数，但失败那行 `exit code 1:` 后面是空的；
  或者两个文件都是 0 字节，`meta.json` 只有 `expected exit 0, got exit 1`。
  **这一类单列，因为读不到的原因比读错的原因更贵。**
- **④ 被同机别的用例拖死**：证据行里出现 `Killed`、`signal`、`timeout`、`out of memory`。

## 表头结论（`compile.collections` 那一行给的）

`collfuzz.quick` 的 `stdout.txt` 是 2258 字节，而它开头 13 行都是 `FAIL seed 0 ops 60 size 24
[nvm/tree]: exit code 1: ` —— 冒号后面是空的，四个 VM × 六个种子都如此。这不是"诊断被吞"，
是**那时候还没有诊断**："执行器说了失败却没说为什么"要改诊断分层，"被执行的那段实现根本不产出
交代"要补实现。两者在表里必须分开。

| 用例 | 归类 | stderr / stdout 字节 | 证据行（原文，截断） |
|---|---|---|---|
| `compile.diag_catalog/catalog.doc_is_generated` | ③ 诊断被吞 | 0 / 168 | diagnostics registry: 1 problem(s) |
| `consistency.runner/runner.backend_matrix` | ③ 诊断被吞 | 60421 / 458 | negative-sweeps declared=1321 swept=1139 excluded=182 reason=a |
| `docs.links/docs.links.citations` | ③ 诊断被吞 | 0 / 160 | docs/design/graph-store/plan.cn.md:135: 'kernel/execute/pass.cpp#extractWritings': the file exists but does not name 'ex |
| `docs.links/docs.snippets_compile` | ① 产品坏了 | 0 / 42772 | src/stdlib/array.cml:66: a semantic error -- ['a', 'b']->enumerate |
| `mm.capacity/capacity.dead_function_closure_verify` | ③ 诊断被吞 | 0 / 547 | FAIL mixed n=100 passes=[std::profile gc_stats=1 report=false hints=false std::opt std::nvm] 0.02s peak=17MB exit 1, exp |
| `mm.gc_pacing/gc_pacing.churn_fvm` | ① 产品坏了 | 0 / 576 | (gc_block_churn)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_churn.cml:16:20: [Error]: No candidate of |
| `mm.gc_pacing/gc_pacing.churn_mark_sweep` | ① 产品坏了 | 0 / 576 | (gc_block_churn)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_churn.cml:16:20: [Error]: No candidate of |
| `mm.gc_pacing/gc_pacing.churn_nursery8` | ① 产品坏了 | 0 / 576 | (gc_block_churn)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_churn.cml:16:20: [Error]: No candidate of |
| `mm.gc_pacing/gc_pacing.churn_nvm` | ① 产品坏了 | 0 / 576 | (gc_block_churn)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_churn.cml:16:20: [Error]: No candidate of |
| `mm.gc_pacing/gc_pacing.churn_prl` | ① 产品坏了 | 0 / 576 | (gc_block_churn)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_churn.cml:16:20: [Error]: No candidate of |
| `mm.gc_pacing/gc_pacing.live_fvm` | ① 产品坏了 | 0 / 578 | (gc_block_pacing)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_pacing.cml:17:20: [Error]: No candidate  |
| `mm.gc_pacing/gc_pacing.live_nvm` | ① 产品坏了 | 0 / 578 | (gc_block_pacing)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_pacing.cml:17:20: [Error]: No candidate  |
| `mm.gc_pacing/gc_pacing.live_steps` | ① 产品坏了 | 0 / 578 | (gc_block_pacing)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_pacing.cml:17:20: [Error]: No candidate  |
| `mm.gc_pacing/gc_pacing.promoted_floor` | ① 产品坏了 | 0 / 1011 | (gc_promoted_blocks)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_promoted_blocks.cml:20:21: [Error]: No matc |
| `mm.gc_pacing/gc_pacing.promoted_fvm` | ① 产品坏了 | 0 / 1011 | (gc_promoted_blocks)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_promoted_blocks.cml:20:21: [Error]: No matc |
| `mm.gc_pacing/gc_pacing.promoted_steps` | ① 产品坏了 | 0 / 1011 | (gc_promoted_blocks)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_promoted_blocks.cml:20:21: [Error]: No matc |
| `mm.gc_pacing/gc_pacing.views_once` | ① 产品坏了 | 0 / 576 | (gc_block_views)/home/zjwei/camel-work/testref2/test/cases/feat/mm/gc/gc_block_views.cml:19:66: [Error]: No candidate of |
| `modules.autodiff/resnet_gradient_relu_kinks_vs_torch64` | ③ 诊断被吞 | 2052 / 66 | ==== resnet  seed=benchmark  activation=relu  torch 2.14.1+cu130 |
| `modules.autodiff/resnet_gradient_smooth_vs_torch64` | ③ 诊断被吞 | 2027 / 66 | ==== resnet  seed=benchmark  activation=tanh  torch 2.14.1+cu130 |
| `modules.autodiff/resnetbn_gradient_relu_kinks_vs_torch64` | ③ 诊断被吞 | 2068 / 68 | ==== resnetbn  seed=benchmark  activation=relu  torch 2.14.1+cu130 |
| `modules.autodiff/resnetbn_gradient_smooth_vs_torch64` | ③ 诊断被吞 | 2041 / 68 | ==== resnetbn  seed=benchmark  activation=tanh  torch 2.14.1+cu130 |
| `modules.import_audit/import.audit` | ③ 诊断被吞 | 750 / 0 | file:///home/zjwei/camel-work/testref2/test/tools/import-audit.mjs:477 |
| `modules.onnx/export_loop_for` | ③ 诊断被吞 | 657 / 50 | total: 9 nodes, 1 Loop, outputs ['tensor(float)'] |
| `modules.stdlib_registry/registry.docs_complete` | ① 产品坏了 | 0 / 13888 | src/stdlib/autodiff/signatures/autodiff.cml:74: the declaration 'with_grad' has a body; a signature file declares operat |
| `modules.stdlib_registry/registry.migrations_stay_migrated` | ① 产品坏了 | 0 / 15114 | --verify-ops: src/stdlib/autodiff/signatures/autodiff.cml:74: the declaration 'with_grad' has a body; a signature file d |
| `opt.effects_kept/opt_matches_interpreter.mnist.nvm` | ③ 无诊断 | 0 / 56 | (empty) |
| `opt.pass_params/docs.word_form_only` | ③ 诊断被吞 | 0 / 644 | FAIL 'changes.log' is excluded from the pages and the tree holds nothing of that name: the exclusion outlived what it wa |
| `parse.frontend/frontend.ast_snapshot` | ③ 诊断被吞 | 0 / 1237 | probes: 227 |
| `tools.autodiff_migration_ledger/tools.autodiff_migration_ledger.ratchet` | ③ 诊断被吞 | 0 / 30756 | case                                                                    exit  want  off  first diagnostic |
| `tools.debugger/debugger.dap_session` | ③ 诊断被吞 | 0 / 287 | FAILED: a var shows the current content of its cell, after the assignment |
| `tools.debugger/debugger.dap_stepping` | ③ 诊断被吞 | 0 / 3196 | FAILED: std::fvm: a println nothing depends on, outside a sync block, has no node to stop at |
| `tools.load_class/load_class.tree` | ③ 诊断被吞 | 0 / 3339 | WARN compile.capture_order:compile.capture_order.two_insertion_orders names test/results/capture_order.reading.txt, whic |
| `tools.portability/portability.std_includes` | ③ 诊断被吞 | 0 / 969 | std includes: read 1308 source file(s) under src/kernel, src/parse, src/compile, src/stdlib, src/tools |
| `trans.canonical_gir/canonical_document_and_equivalence` | ③ 诊断被吞 | 877 / 0 | Traceback (most recent call last): |
| `trans.cml/print_and_recompile` | ③ 诊断被吞 | 1140 / 0 | Traceback (most recent call last): |
| `trans.cml/print_then_run_agrees_with_the_origin` | ③ 诊断被吞 | 0 / 554 | case=consistency/fail_never/statement_after_fail rc=1 stdout=identical |
| `trans.cpp/every_expect_program_links_no_runtime` | ③ 诊断被吞 | 0 / 2332 | test/cases/consistency/values/higher_order.cml |

## 读这张表的人要注意的三件事

1. **`stderr` 几乎全空**（169 段合计 163 个 `bytes=0`）：这类失败的交代都在 `stdout` 里。
   所以"去看 stderr"在这批用例上是一个错的读法。
2. **"期望的输出没出现"与"运行时一句话没说"是两件事**：`meta.json` 的 `failures` 记着
   `expected exit 0, got exit 1` 与 `stdout does not contain …`，后者是用例的判据，不是运行时的交代。
3. **`docs.links` 的两条不在这一件里重复读**：`docs.links.citations` 红在
   `docs/design/graph-store/plan.cn.md:135` 的引用上，`docs.snippets_compile` 红在 12 条 `STALE` 的
   pending 条目上（销账归 `lane/misc`）。它们掉进这一类，只是因为末行不是一句诊断。
