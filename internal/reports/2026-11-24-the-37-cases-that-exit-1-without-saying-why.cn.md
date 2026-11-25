# 37 条"退出 1 却没有一句说明"——交给下一条车道逐条读

**这不是一次修复，是一份派工。** 移层红表（`2026-11-24-the-slow-layer-population.tsv`）里
`cause_class` 有一类 `exit-1-with-no-line-saying-why`，共 **37 条**。它的意思是：
用例期望 0、拿到 1，而它的输出里**没有一行**指出为什么——没有诊断、没有断言消息、没有"缺了什么"。
表里其余每一类都能靠那一刻的一句话归类（`(Range)` 没有高阶函数、`to_array` 没进编译器、金标
落后于录制、parser 不接受旧写法），**只有这一类不能**：要知道它为什么红，必须打开它的
`stdout.txt`/`stderr.txt` 读。这是一次判断，不是一次分类，所以不在移层那一笔里做。

**给读的人**：照最后一列打开那次运行的目录，读 `stdout.txt`、`stderr.txt`、`meta.json`（退出码、
信号、超时都在里面），判断它属于哪一种，并把结论就地写下来：

- **产品真的坏了** —— 那要一条缺陷，不是一条红；
- **用例自己的期望过时了** —— 那要改期望，并在用例里写清为什么；
- **它其实有诊断，只是被吞掉了** —— 那本身就是缺陷，而且是最该先报的一类：读不到的原因比读错的
  原因更贵；
- **它被同机别的用例拖死**（超时、OOM、抢占）—— 那要说明是什么，以及当时的负载。

**两条不用重复读**：`docs.links.citations`（红在 `docs/design/graph-store/plan.cn.md:135` 那条
引用上，batch-1 报告已记）与 `docs.snippets_compile`（红在 12 条 `STALE` 的 pending 条目上，归
`lane/misc` 销账）。它们掉进这一类只是因为"末行不是一句诊断"。

**第三列 36 行是同一句** `expected exit 0, got exit 1`——这就是这一类的内容：运行里没有第二句话。
唯一不同的一行是 `trans.cml::print_and_recompile`，它在 Debug 层留了一句
`Camel Debug Assertion Failed: … src/core/type/base.cpp(335) … Type::assignableFrom`，所以那一条
不是"没有交代"，而是"交代留在了另一个层"——读它的人应该顺着那句断言去查，并考虑把 Release 层
那份没有交代这件事本身记成缺陷。

| 计划 | 用例 | 运行里唯一的交代 | 去哪读 |
|---|---|---|---|
| `compile.diag_catalog` | `catalog.doc_is_generated` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/compile.diag_catalog/catalog.doc_is_generated/` |
| `consistency.runner` | `runner.backend_matrix` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/consistency.runner/runner.backend_matrix/` |
| `docs.links` | `docs.links.citations` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/docs.links/docs.links.citations/` |
| `docs.links` | `docs.snippets_compile` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/docs.links/docs.snippets_compile/` |
| `mm.capacity` | `capacity.dead_function_closure_verify` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.capacity/capacity.dead_function_closure_verify/` |
| `mm.gc_pacing` | `gc_pacing.churn_fvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.churn_fvm/` |
| `mm.gc_pacing` | `gc_pacing.churn_mark_sweep` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.churn_mark_sweep/` |
| `mm.gc_pacing` | `gc_pacing.churn_nursery8` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.churn_nursery8/` |
| `mm.gc_pacing` | `gc_pacing.churn_nvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.churn_nvm/` |
| `mm.gc_pacing` | `gc_pacing.churn_prl` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.churn_prl/` |
| `mm.gc_pacing` | `gc_pacing.live_fvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.live_fvm/` |
| `mm.gc_pacing` | `gc_pacing.live_nvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.live_nvm/` |
| `mm.gc_pacing` | `gc_pacing.live_steps` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.live_steps/` |
| `mm.gc_pacing` | `gc_pacing.promoted_floor` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.promoted_floor/` |
| `mm.gc_pacing` | `gc_pacing.promoted_fvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.promoted_fvm/` |
| `mm.gc_pacing` | `gc_pacing.promoted_steps` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.promoted_steps/` |
| `mm.gc_pacing` | `gc_pacing.views_once` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/mm.gc_pacing/gc_pacing.views_once/` |
| `modules.autodiff` | `resnet_gradient_relu_kinks_vs_torch64` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.autodiff/resnet_gradient_relu_kinks_vs_torch64/` |
| `modules.autodiff` | `resnet_gradient_smooth_vs_torch64` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.autodiff/resnet_gradient_smooth_vs_torch64/` |
| `modules.autodiff` | `resnetbn_gradient_relu_kinks_vs_torch64` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.autodiff/resnetbn_gradient_relu_kinks_vs_torch64/` |
| `modules.autodiff` | `resnetbn_gradient_smooth_vs_torch64` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.autodiff/resnetbn_gradient_smooth_vs_torch64/` |
| `modules.import_audit` | `import.audit` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.import_audit/import.audit/` |
| `modules.onnx` | `export_loop_for` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.onnx/export_loop_for/` |
| `modules.stdlib_registry` | `registry.docs_complete` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.stdlib_registry/registry.docs_complete/` |
| `modules.stdlib_registry` | `registry.migrations_stay_migrated` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/modules.stdlib_registry/registry.migrations_stay_migrated/` |
| `opt.effects_kept` | `opt_matches_interpreter.mnist.nvm` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/opt.effects_kept/opt_matches_interpreter.mnist.nvm/` |
| `opt.pass_params` | `docs.word_form_only` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/opt.pass_params/docs.word_form_only/` |
| `parse.frontend` | `frontend.ast_snapshot` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/parse.frontend/frontend.ast_snapshot/` |
| `tools.autodiff_migration_ledger` | `tools.autodiff_migration_ledger.ratchet` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/tools.autodiff_migration_ledger/tools.autodiff_migration_ledger.ratchet/` |
| `tools.debugger` | `debugger.dap_session` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/tools.debugger/debugger.dap_session/` |
| `tools.debugger` | `debugger.dap_stepping` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/tools.debugger/debugger.dap_stepping/` |
| `tools.load_class` | `load_class.tree` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/tools.load_class/load_class.tree/` |
| `tools.portability` | `portability.std_includes` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/tools.portability/portability.std_includes/` |
| `trans.canonical_gir` | `canonical_document_and_equivalence` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/trans.canonical_gir/canonical_document_and_equivalence/` |
| `trans.cml` | `print_and_recompile` | `[INFO] note: On the Debug tier this is red with `Camel Debug Assertion Failed: Location: ../src/core/type/base.cpp(335), Function: bool camel::core::type::Type::assignableFrom(Type *) const, Expressio` | `(no log path printed)/` |
| `trans.cml` | `print_then_run_agrees_with_the_origin` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/trans.cml/print_then_run_agrees_with_the_origin/` |
| `trans.cpp` | `every_expect_program_links_no_runtime` | `[INFO] expected exit 0, got exit 1` | `test/results/log/20261124_044813_2404244/fail/trans.cpp/every_expect_program_links_no_runtime/` |

## 口径

这些数来自一对读数：底树计划内容 + 本分支产品代码的整层快层（9,916 条，1,384 红）与本分支的整层
慢层（560 条，169 红）。分类只在慢层真的跑起来的 560 条里做——一条用例在快层里没出现，不是"它
不红了"，而是它当时就在慢层、那份读数没跑过它。详见
`2026-11-24-does-the-move-change-any-red.cn.md`。

**这 37 条里 19 条"移层前后都红"、18 条"当时已在慢层"，没有一条是这笔移层造成的**：
移层只改选择，改不了用例的命令与结果，两份红表逐条比过了。所以读它们的人不必怀疑移层。
