# 过程痕迹：这一笔补的是第五种编号，剩下 511 行是树里已经持有的债

**状态：已落。** 一笔提交在 `lane/ast-rerecord` 上，基点是 `origin/integration` 的 `28f74cab05`。
本页记四件事：读数、这一单里**已经成立而不必重做**的那一半、这一笔补上的一种写法、
以及剩下 511 行为什么还在（逐条列在附录，按类的理由在下表）。

## 一、读数

| 项 | 值 |
|---|---|
| 命令 | `node scripts/check-process-traces.mjs --root . --list` |
| 命中 | **511**（6443 文件，171034 注释行） |
| 不给 `--list` 时 | **exit 1**，同样 511 行 |
| 自检 | `node --test scripts/check-process-traces.test.mjs` → **8/8** |
| 三个数连起来 | 基点 `28f74cab05` 上是 511 → 加上 `task-<n>` 这条判据，同一棵树读成 **512** → 这一笔把那唯一一行改掉，回到 **511** |

第三行是这一笔的全部作用：它**没有**减少树里的债，它让一种此前读不到的写法读得到；
读到的唯一一处当场清掉，所以前后是同一个数。

## 二、覆盖面这半边已经成立，没有重做

本单写的是「检查器只读部分树，扩到 `scripts/`、`internal/tools/`、`test/`」。树里这一半已经在了：

```
scripts/check-process-traces.mjs
const dirs = ['src', 'scripts', 'internal/tools', 'test'];
```

`68308a89f6` 就是那次扩宽，同时把派工编号（`D-<year>-<n>` / `C-<year>-<n>`）、车道代号和
"待审"加了进去；自检里 `the trees read are the four that carry traces` 那一条钉着这四棵树，
把 `dirs` 收窄回一棵会让它红。所以我按"先看不从零重来"，没有动这半边。

## 三、这一笔补的写法：`task-<n>`

派工编号有两种：一种是 `D-<year>-<n>` / `C-<year>-<n>`（已读），一种是任务板上的
`task-<n>`（此前读不到）。判据里加了一行：

```
'\\btask-\\d+\\b',
```

树里唯一一处命中在 `test/cases/modules/graph_facts/draft_effects_refused.cml:13`：

```
- // CLI prints a runtime fault on stdout today, and task-79 moves diagnostics to stderr. When it
- // lands, this plan's assertion moves with it -- the reason stays the same string, its stream does
+ // CLI prints a runtime fault on stdout while diagnostics still go there. When they move to stderr,
+ // this plan's assertion moves with them -- the reason stays the same string, its stream does
```

判断仍是"说的是代码还是过程"：这句话的用途是提醒下一个读的人**不要**把 stdout 断言当成结论，
所以留着；它只是不该靠一个派工编号来指未来那笔改动。改后说的是这件事本身（诊断仍在 stdout）。

**两个方向的证明**（把改前那行放进一棵临时树的 `test/` 下，用同一个检查器读）：

```
改前那行  → process traces in comments: 1 listed, nothing judged (--list; scanned 1 files, 1 comment lines)
改后那行  → scanned 1 files, 1 comment lines, no trace (--list, nothing judged)
```

自检也加了一例（`test/cases/modules/graph_facts/waiting.cml`），所以这条判据不会在下一次
"收窄读法"时悄悄回退。

## 四、剩下的 511 行：按类，每类一句为什么还在

列可以重叠（一行可能同时是"日期"和"页路径"），所以列和大于 511。

| 类 | 行 | 为什么不在这里删 |
|---|---:|---|
| 日期 `20yy-mm-dd` | 366 | 日期多半和"读数的现场"挤在同一个括号里（`(measured …, a shell without LD_LIBRARY_PATH)`）。只抽走日期，剩下那截要么变成宾语、要么留下 `(measured :)`。可行的判据是"抽走日期后剩下的还是不是一个完整从句"，那是逐行判断，写不成一条规则；上一条车道试过三遍自动改写，都退回了（`2026-11-25-traces-to-zero.cn.md` 第五节）。 |
| 交接页路径 `internal/…` | 106 | 路径多在句中当主语或宾语（`(\`internal/questions/…\`) is narrower than …`）。换成"the page"试过 54 行，多数读得更糟，还留下 `/// comment; the page)` 这种句子。 |
| 裸提交哈希 | 86 | 判据只在 `git cat-file` 能把它解析成提交时才算命中，所以这 86 行是真的哈希。删法不统一：有的在括号里（`(26888783c: …)`），有的是 `…-or-later` 这种形状，得逐条读。 |
| `stage N` | 16 | 半数是模型/流水线的"Stage 4"和文件名（`stage2_residual_ln.cml`），半数是重构阶段的真痕迹（`stage 0 of the refactor`）。判据只看这个形状，没有条件能只留下后者。 |
| 车道代号 `lane-…` | 7 | 是含 `lane-` 的工具名（`merge-lane-docs.ps1`）和图里的 `lane-local`，不是某条车道的名字。 |
| `iron rule N` | 6 | 引用仓库的一条规则编号，是规则不是"谁在何时决定"；但它和"铁律是那次事故定的"共用一个形状。 |
| `refactor stage` | 1 | 那一行正是检查器文件头在举例（`refactor stage 2.3`），例子不能改成别的措辞，否则文件头说错它拒绝什么。 |

这 511 行是**树里已经持有的债**，不是任何人的新增：push 钩子用 `--range` 只判一次推送**新增的行**，
所以它们不挡任何人的 push（这一点由自检里 `--list` 与 `--range` 那几条钉着）。要往下减，
下一片仍然是日期那 366 行，做法只能是逐行判断，不是规则。

## 五、附录：剩下的 511 行，逐条

生成方式：`node scripts/check-process-traces.mjs --root . --list`（同一条命令，同一天）。

```
internal/tools/camel-gate-state.ps1:6
internal/tools/camel-oversight/rules.js:72
internal/tools/camel-remote-cleanup.test.ps1:3
internal/tools/camel-remote-targets.ps1:9
internal/tools/camel-remote-targets.ps1:103
internal/tools/camel-remote-targets.test.ps1:3
internal/tools/camel-remote.ps1:417
internal/tools/camel-remote.ps1:464
internal/tools/camel-remote.ps1:486
internal/tools/camel-remote.ps1:655
internal/tools/camel-remote.ps1:724
internal/tools/camel-remote.ps1:837
internal/tools/camel-remote.ps1:875
internal/tools/camel-remote.ps1:876
internal/tools/camel-remote.ps1:889
internal/tools/camel-remote.ps1:945
internal/tools/camel-remote.ps1:1170
internal/tools/camel-remote.ps1:1726
internal/tools/camel-remote.ps1:1894
internal/tools/camel-remote.ps1:1972
internal/tools/camel-remote.ps1:1974
internal/tools/camel-remote.ps1:1993
internal/tools/camel-remote.ps1:2228
internal/tools/camel-remote.ps1:2341
internal/tools/camel-remote.ps1:2753
internal/tools/camel-remote.ps1:2818
internal/tools/camel-remote.ps1:2821
internal/tools/camel-remote.ps1:2833
internal/tools/camel-remote.ps1:2896
internal/tools/camel-remote.ps1:2900
internal/tools/camel-remote.ps1:2903
internal/tools/camel-remote.ps1:3009
internal/tools/camel-remote.ps1:3178
internal/tools/camel-remote.ps1:3663
internal/tools/host-case.sh:12
internal/tools/host-tokens.sh:8
internal/tools/host-tokens.sh:53
internal/tools/host-tokens.sh:138
internal/tools/lane-audit.mjs:53
internal/tools/red-table-context.mjs:128
internal/tools/red-table.mjs:7
internal/tools/red-table.mjs:18
internal/tools/red-table.mjs:338
internal/tools/sync-oversight-live.mjs:17
scripts/autodiff-migration-ledger.mjs:9
scripts/autodiff-migration-ledger.mjs:335
scripts/bisect-merge.mjs:48
scripts/bisect-merge.mjs:51
scripts/bisect-merge.mjs:52
scripts/camel-for-ai.mjs:170
scripts/camel-pass-scan.mjs:318
scripts/check-ast-hashes.mjs:145
scripts/check-ast-hashes.mjs:507
scripts/check-capture-order.mjs:19
scripts/check-doc-links.mjs:15
scripts/check-doc-links.mjs:40
scripts/check-doc-links.mjs:41
scripts/check-doc-links.mjs:423
scripts/check-doc-links.mjs:424
scripts/check-doc-links.mjs:436
scripts/check-doc-links.mjs:437
scripts/check-doc-links.mjs:944
scripts/check-effect-facts.mjs:10
scripts/check-plan-debt.mjs:9
scripts/check-plan-refs.mjs:11
scripts/check-plan-refs.mjs:24
scripts/check-plan-refs.mjs:143
scripts/check-process-traces.mjs:18
scripts/check-process-traces.test.mjs:137
scripts/check-program-holders.mjs:15
scripts/check-references.mjs:263
scripts/check-references.mjs:264
scripts/check-source-artifacts.mjs:120
scripts/check-std-includes.mjs:42
scripts/check-std-includes.mjs:47
scripts/check-wheel-tags.mjs:9
scripts/collect-out.js:207
scripts/common.js:724
scripts/conflict-markers.mjs:8
scripts/conflict-markers.mjs:9
scripts/conflict-markers.mjs:10
scripts/conflict-markers.mjs:20
scripts/conflict-markers.mjs:158
scripts/count-graph-readers.mjs:12
scripts/count-graph-readers.mjs:36
scripts/cxx-runtime-guard.mjs:11
scripts/gates.mjs:9
scripts/gates.mjs:13
scripts/headers.mjs:90
scripts/law-cotangent-ports.mjs:27
scripts/law-cotangent-ports.mjs:28
scripts/law-cotangent-ports.mjs:37
scripts/law-cotangent-ports.mjs:45
scripts/law-cotangent-ports.mjs:1015
scripts/layout-move.mjs:975
scripts/migrate-pack-dots.mjs:37
scripts/migrate-pack-dots.mjs:39
scripts/old-tree-probe.mjs:8
scripts/plan-dir-markers.mjs:9
scripts/plan-inventory.mjs:56
scripts/plan-inventory.mjs:60
scripts/plan-inventory.mjs:61
scripts/plan-inventory.mjs:510
scripts/plan-inventory.mjs:515
scripts/pypi-rollup.js:37
scripts/pypi-rollup.js:270
scripts/pypi-rollup.js:417
scripts/pypi-rollup.js:495
scripts/release-gate.mjs:124
scripts/release-gate.mjs:142
scripts/release-gate.mjs:143
scripts/release-gate.mjs:156
scripts/stdlib-reference.mjs:955
scripts/stdlib-reference.mjs:2107
scripts/stdlib-reference.mjs:2777
scripts/tier-report.mjs:10
scripts/tier-report.mjs:157
src/kernel/runtime/draft_inline.cpp:1820
src/stdlib/test/include/camel/passes/test/loop_range.h:8
src/stdlib/test/loop_range.cpp:80
test/cases/compile/constas/constas_forward_field.cml:3
test/cases/compile/constas/constas_not_first.cml:3
test/cases/compile/constas/constas_unused.cml:3
test/cases/compile/diag_json/dropped_effects.cml:3
test/cases/compile/function_values/captured_var_in_two_arms.cml:18
test/cases/compile/literals/expected_type_parameter_holes.cml:4
test/cases/compile/literals/expected_type_typed_value_not_converted.cml:4
test/cases/compile/native/declaration_module_body.mjs:5
test/cases/compile/region_roots/read_only.cml:7
test/cases/compile/region_roots/write_order_print_first.cml:3
test/cases/compile/rest_params/with_pack_lambda_is_a_value.cml:1
test/cases/compile/state_escape/err_escape_array.cml:6
test/cases/compile/state_escape/err_escape_field.cml:6
test/cases/compile/state_escape/err_escape_return.cml:6
test/cases/compile/state_escape/err_escape_stored_by_callee.cml:6
test/cases/compile/state_escape/ok_read_escape_into_closure.cml:6
test/cases/compile/state_escape/ok_read_escape_return.cml:6
test/cases/compile/static_expansion/indices_two_names.cml:3
test/cases/compile/static_expansion/static_loop_sequence_runtime_array.cml:4
test/cases/compile/static_expansion/static_loop_sequence_tuple.cml:4
test/cases/compile/static_expansion/static_loop_sequence_unknown.cml:3
test/cases/compile/stream/composition.cml:3
test/cases/compile/stream/interval_own_range.cml:3
test/cases/compile/stream/type_signature.cml:3
test/cases/compile/stream/while_effects.cml:4
test/cases/compile/surface/dropped_value_statement.cml:2
test/cases/compile/tags/err_tag_argument.cml:2
test/cases/compile/tags/err_tag_shadows_permission.cml:3
test/cases/compile/transforms/chain_place_after_a_branch.cml:36
test/cases/compile/transforms/grad_array_with_int.cml:1
test/cases/compile/transforms/library_grad_of_a_with_weight.cml:31
test/cases/compile/transforms/matmul_transpose_vector_lhs.cml:25
test/cases/compile/transforms/matmul_transpose_vector_lhs_stated_extent.cml:36
test/cases/compile/transforms/matmul_transpose_vector_rhs_stated_extent.cml:22
test/cases/compile/transforms/mean_axis1_keepdims_gradient_reads_the_row_length.cml:34
test/cases/compile/transforms/pick_at_forward_rule.cml:37
test/cases/compile/transforms/pow_gradient_of_a_caret_through_the_library.cml:5
test/cases/compile/transforms/pow_gradient_through_the_library.cml:21
test/cases/compile/transforms/transpose_call_first_argument_is_a_function_value.cml:14
test/cases/compile/transforms/transpose_of_a_stated_shape_answers.cml:13
test/cases/compile/transforms/transpose_unread_state_answers_zero.cml:5
test/cases/compile/transforms/transpose_unread_state_answers_zero.cml:7
test/cases/compile/transforms/vjp_const_function_answers_zero.cml:3
test/cases/compile/transforms/vjp_gradient.cml:15
test/cases/compile/transforms/vjp_loop_state_unread_answers_zero.cml:7
test/cases/compile/transforms/vmap_capture_is_a_constant.cml:9
test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml:2
test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml:28
test/cases/compile/var_closure/err_captured_var_put_into_an_array.cml:15
test/cases/compile/var_closure/err_closure_assigns_captured.cml:3
test/cases/compile/when/generic_overload_none_apply.cml:5
test/cases/consistency/attempt/partial_fault.cml:4
test/cases/consistency/fail_never/never_with_argument_named.cml:3
test/cases/consistency/fail_never/never_with_argument_run.cml:3
test/cases/consistency/fail_never/statement_after_fail.cml:3
test/cases/effects/callback_write_through_parameter.cml:9
test/cases/effects/err_capture_escapes.cml:2
test/cases/effects/err_closure_capture_pure.cml:7
test/cases/effects/hof_map_effect.cml:3
test/cases/effects/hof_map_pure.cml:3
test/cases/effects/ok_capture_local_var.cml:9
test/cases/format/compose_and_return_type.cml:1
test/cases/linear/byref/lambda_capture_ignored.cml:7
test/cases/linear/runtime/effects_outside_sync.cml:2
test/cases/loops/zero_leaf_state.cml:3
test/cases/modules/autodiff/grad_first_with_parameter.cml:39
test/cases/modules/autodiff/has_aux_library_plain.cml:16
test/cases/modules/autodiff/reshape_adjoint_shape.cml:12
test/cases/modules/autodiff/rules_angle_args_mean_transpose.cml:28
test/cases/modules/autodiff/second_order_refused_vjp.cml:27
test/cases/modules/graph_function/outline.cml:7
test/cases/modules/nn/lr.cml:47
test/cases/modules/rules/check_accuracy_library.cml:13
test/cases/modules/rules/check_every_rule.cml:3
test/cases/modules/rules/generic_literal_witness.cml:3
test/cases/modules/rules/verify_report_instance.cml:3
test/cases/opt/adb67e081/rc04_seed72_within_call.cml:7
test/cases/opt/adb67e081/rc04_seed72_within_call.cml:8
test/cases/opt/adb67e081/rc04_seed72_within_call.cml:16
test/cases/opt/algebra_rules/agreement.cml:7
test/cases/opt/algebra_rules/library_only.cml:9
test/cases/opt/algebra_rules/library_only.cml:19
test/cases/opt/algebra_rules/tensor_forms.cml:3
test/cases/opt/commit_checks/identity_at_exit.cml:2
test/cases/opt/commit_checks/inline_two_reads.cml:14
test/cases/opt/generic/concat_chain.cml:1
test/cases/opt/inline_cycle/seed23_fold_inline_within_call.cml:7
test/cases/opt/inline_cycle/seed23_fold_inline_within_call.cml:8
test/cases/opt/inline_cycle/seed23_fold_inline_within_call.cml:15
test/cases/opt/pass_written/check.mjs:101
test/cases/runtime/graph_draft_ledger/check.mjs:10
test/cases/runtime/graph_draft_ledger/check.mjs:16
test/cases/tools/argv/argv.mjs:9
test/cases/tools/cli/pass_declarations_missing.mjs:64
test/cases/tools/lsp/navigation.test.mjs:54
test/cases/tools/tree_guard.mjs:8
test/cases/transforms/compositions/hessian_of_a_quadratic.cml:23
test/cases/transforms/nested/rule_writes_a_constant.cml:12
test/cases/transforms/split/split_capture_of_a_tensor.cml:21
test/cases/transforms/split/split_forward_half.cml:1
test/cases/transforms/split/split_loop_taped_rounds.cml:1
test/cases/transforms/split/split_tuple_member_read.cml:16
test/cases/transforms/split/split_whole_and_member_answers.cml:7
test/plans/feat/compile/entry_args.plan.toml:104
test/plans/feat/compile/extent_of.plan.toml:5
test/plans/feat/compile/extent_of.plan.toml:8
test/plans/feat/compile/fold_arms.plan.toml:40
test/plans/feat/compile/function_values.plan.toml:372
test/plans/feat/compile/higher_order_clause.plan.toml:14
test/plans/feat/compile/inferred_resources.plan.toml:7
test/plans/feat/compile/lambda_inference.plan.toml:13
test/plans/feat/compile/lambda_inference.plan.toml:81
test/plans/feat/compile/native_declarations.plan.toml:17
test/plans/feat/compile/ref.plan.toml:702
test/plans/feat/compile/refwrite.plan.toml:1
test/plans/feat/compile/region_roots.plan.toml:192
test/plans/feat/compile/rest_params.plan.toml:339
test/plans/feat/compile/rules_angle_args.plan.toml:14
test/plans/feat/compile/rules_angle_args.plan.toml:31
test/plans/feat/compile/rules_angle_args.plan.toml:69
test/plans/feat/compile/rulesets.plan.toml:520
test/plans/feat/compile/rulesets.plan.toml:528
test/plans/feat/compile/rulesets.plan.toml:540
test/plans/feat/compile/rulesets.plan.toml:617
test/plans/feat/compile/rulesets.plan.toml:640
test/plans/feat/compile/state_escape.plan.toml:3
test/plans/feat/compile/state_escape.plan.toml:7
test/plans/feat/compile/state_escape.plan.toml:55
test/plans/feat/compile/stated_extents.plan.toml:61
test/plans/feat/compile/stream.plan.toml:1
test/plans/feat/compile/tagged.plan.toml:1430
test/plans/feat/compile/tags.plan.toml:605
test/plans/feat/compile/transforms.plan.toml:126
test/plans/feat/compile/transforms.plan.toml:150
test/plans/feat/compile/transforms.plan.toml:399
test/plans/feat/compile/transforms.plan.toml:401
test/plans/feat/compile/transforms.plan.toml:410
test/plans/feat/compile/transforms.plan.toml:586
test/plans/feat/compile/transforms.plan.toml:1119
test/plans/feat/compile/transforms.plan.toml:1249
test/plans/feat/compile/transforms.plan.toml:1400
test/plans/feat/compile/transforms.plan.toml:1446
test/plans/feat/compile/transforms.plan.toml:1460
test/plans/feat/compile/transforms.plan.toml:1474
test/plans/feat/compile/transforms.plan.toml:1613
test/plans/feat/compile/transforms.plan.toml:1615
test/plans/feat/compile/transforms.plan.toml:1622
test/plans/feat/compile/transforms.plan.toml:1688
test/plans/feat/compile/transforms.plan.toml:1689
test/plans/feat/compile/transforms.plan.toml:1704
test/plans/feat/compile/transforms.plan.toml:1713
test/plans/feat/compile/transforms.plan.toml:1963
test/plans/feat/compile/transforms.plan.toml:2070
test/plans/feat/compile/transforms.plan.toml:2146
test/plans/feat/compile/transforms.plan.toml:2217
test/plans/feat/compile/transforms.plan.toml:2314
test/plans/feat/compile/transforms.plan.toml:2389
test/plans/feat/compile/transforms.plan.toml:2410
test/plans/feat/compile/transforms.plan.toml:2541
test/plans/feat/compile/transforms.plan.toml:2553
test/plans/feat/compile/transforms.plan.toml:2872
test/plans/feat/compile/transforms.plan.toml:2920
test/plans/feat/compile/transforms.plan.toml:2935
test/plans/feat/compile/transforms.plan.toml:3011
test/plans/feat/compile/transforms.plan.toml:3051
test/plans/feat/compile/transforms.plan.toml:3131
test/plans/feat/compile/transforms.plan.toml:3182
test/plans/feat/compile/transforms.plan.toml:3197
test/plans/feat/compile/transforms.plan.toml:3223
test/plans/feat/compile/transforms.plan.toml:3234
test/plans/feat/compile/transforms.plan.toml:3370
test/plans/feat/compile/transpose_array_product.plan.toml:34
test/plans/feat/compile/unused_values.plan.toml:157
test/plans/feat/consistency/limits.plan.toml:125
test/plans/feat/consistency/match.plan.toml:82
test/plans/feat/consistency/new_values.plan.toml:216
test/plans/feat/consistency/new_values.plan.toml:507
test/plans/feat/consistency/runner.plan.toml:27
test/plans/feat/consistency/runner.plan.toml:41
test/plans/feat/docs_links.plan.toml:186
test/plans/feat/docs_links.plan.toml:290
test/plans/feat/docs_links.plan.toml:305
test/plans/feat/docs_links.plan.toml:316
test/plans/feat/docs_links.plan.toml:324
test/plans/feat/docs_links.plan.toml:332
test/plans/feat/docs_links.plan.toml:339
test/plans/feat/effects/masking_transition.plan.toml:10
test/plans/feat/effects/masking_transition.plan.toml:191
test/plans/feat/examples.plan.toml:266
test/plans/feat/format/core.plan.toml:143
test/plans/feat/format/golden/compose_and_return_type.cml:1
test/plans/feat/linear/fvm_nvm_diff.plan.toml:6
test/plans/feat/linear/jit_framed.plan.toml:229
test/plans/feat/linear/jit_framed.plan.toml:232
test/plans/feat/linear/jit_framed.plan.toml:234
test/plans/feat/linear/var_read_before_write.plan.toml:53
test/plans/feat/linear/var_read_before_write.plan.toml:92
test/plans/feat/linear/var_read_before_write.plan.toml:131
test/plans/feat/linear/var_read_before_write.plan.toml:170
test/plans/feat/linear/var_read_before_write.plan.toml:209
test/plans/feat/linear/var_read_before_write.plan.toml:273
test/plans/feat/modules/autodiff.plan.toml:457
test/plans/feat/modules/autodiff.plan.toml:474
test/plans/feat/modules/autodiff.plan.toml:2603
test/plans/feat/modules/autodiff_inspection.plan.toml:160
test/plans/feat/modules/fusion_group.plan.toml:238
test/plans/feat/modules/graph_inline.plan.toml:5
test/plans/feat/modules/graph_outline.plan.toml:10
test/plans/feat/modules/graph_pass_function.plan.toml:32
test/plans/feat/modules/jax_alignment.plan.toml:25
test/plans/feat/modules/nn.plan.toml:1246
test/plans/feat/modules/nn.plan.toml:1277
test/plans/feat/modules/nn.plan.toml:1297
test/plans/feat/modules/nn.plan.toml:1305
test/plans/feat/modules/nn.plan.toml:1319
test/plans/feat/modules/onnx.plan.toml:259
test/plans/feat/modules/onnx.plan.toml:479
test/plans/feat/modules/onnx.plan.toml:1068
test/plans/feat/modules/onnx.plan.toml:1098
test/plans/feat/modules/onnx.plan.toml:1128
test/plans/feat/modules/onnx.plan.toml:1581
test/plans/feat/modules/onnx.plan.toml:1611
test/plans/feat/modules/onnx.plan.toml:3213
test/plans/feat/modules/onnx.plan.toml:3371
test/plans/feat/modules/remat.plan.toml:9
test/plans/feat/modules/remat.plan.toml:55
test/plans/feat/modules/rules.plan.toml:1009
test/plans/feat/modules/selected_protection.plan.toml:397
test/plans/feat/modules/selected_protection.plan.toml:417
test/plans/feat/modules/stdlib_registry.plan.toml:335
test/plans/feat/modules/stdlib_registry.plan.toml:343
test/plans/feat/opt/adb67e081.plan.toml:7
test/plans/feat/opt/adb67e081.plan.toml:8
test/plans/feat/opt/adb67e081.plan.toml:21
test/plans/feat/opt/adb67e081.plan.toml:33
test/plans/feat/opt/adb67e081.plan.toml:50
test/plans/feat/opt/adb67e081.plan.toml:51
test/plans/feat/opt/adb67e081.plan.toml:67
test/plans/feat/opt/adb67e081.plan.toml:86
test/plans/feat/opt/adb67e081.plan.toml:103
test/plans/feat/opt/adb67e081.plan.toml:122
test/plans/feat/opt/adb67e081.plan.toml:143
test/plans/feat/opt/algebra_rules.plan.toml:25
test/plans/feat/opt/commit_refusal.plan.toml:6
test/plans/feat/opt/commit_refusal.plan.toml:8
test/plans/feat/opt/commit_refusal.plan.toml:9
test/plans/feat/opt/commit_refusal.plan.toml:67
test/plans/feat/opt/commit_refusal.plan.toml:257
test/plans/feat/opt/commit_refusal.plan.toml:286
test/plans/feat/opt/cxx_isolation.plan.toml:10
test/plans/feat/opt/cxx_isolation.plan.toml:27
test/plans/feat/opt/cxx_isolation.plan.toml:102
test/plans/feat/opt/cxx_isolation.plan.toml:212
test/plans/feat/opt/e9.plan.toml:7
test/plans/feat/opt/e9.plan.toml:339
test/plans/feat/opt/e9.plan.toml:356
test/plans/feat/opt/e9.plan.toml:790
test/plans/feat/opt/e9.plan.toml:905
test/plans/feat/opt/e9.plan.toml:941
test/plans/feat/opt/e9.plan.toml:978
test/plans/feat/opt/e9.plan.toml:981
test/plans/feat/opt/e9.plan.toml:1008
test/plans/feat/opt/e9.plan.toml:1015
test/plans/feat/opt/e9.plan.toml:1054
test/plans/feat/opt/e9.plan.toml:1091
test/plans/feat/opt/e9.plan.toml:1130
test/plans/feat/opt/e9.plan.toml:1167
test/plans/feat/opt/e9.plan.toml:1205
test/plans/feat/opt/edges.plan.toml:179
test/plans/feat/opt/effects_kept.plan.toml:379
test/plans/feat/opt/effects_kept.plan.toml:387
test/plans/feat/opt/effects_kept.plan.toml:553
test/plans/feat/opt/effects_kept.plan.toml:572
test/plans/feat/opt/effects_kept.plan.toml:581
test/plans/feat/opt/generic.plan.toml:1649
test/plans/feat/opt/generic.plan.toml:1684
test/plans/feat/opt/generic.plan.toml:1695
test/plans/feat/opt/generic.plan.toml:1734
test/plans/feat/opt/generic.plan.toml:1829
test/plans/feat/opt/inline.plan.toml:1050
test/plans/feat/opt/inline_cycle.plan.toml:46
test/plans/feat/opt/inline_cycle.plan.toml:56
test/plans/feat/opt/inline_cycle.plan.toml:73
test/plans/feat/opt/inline_cycle.plan.toml:84
test/plans/feat/opt/opt_second_pass.plan.toml:7
test/plans/feat/opt/opt_second_pass.plan.toml:32
test/plans/feat/opt/opt_second_pass.plan.toml:71
test/plans/feat/opt/pass_params.plan.toml:554
test/plans/feat/opt/progen.plan.toml:56
test/plans/feat/opt/progen8_arm.plan.toml:33
test/plans/feat/opt/progen8_arm.plan.toml:59
test/plans/feat/opt/progen8_arm.plan.toml:64
test/plans/feat/opt/rounds_warning.plan.toml:98
test/plans/feat/opt/rounds_warning.plan.toml:99
test/plans/feat/opt/splice_place.plan.toml:170
test/plans/feat/opt/writings.plan.toml:1
test/plans/feat/opt/writings.plan.toml:56
test/plans/feat/para/exec_pred.plan.toml:178
test/plans/feat/parse/frontend.plan.toml:21
test/plans/feat/parse/frontend.plan.toml:51
test/plans/feat/runtime/graph_draft_ledger.plan.toml:7
test/plans/feat/showcase_s2.plan.toml:7
test/plans/feat/showcase_s2.plan.toml:12
test/plans/feat/showcase_s2.plan.toml:226
test/plans/feat/tools/argv.plan.toml:7
test/plans/feat/tools/facts.plan.toml:26
test/plans/feat/tools/member_init.plan.toml:10
test/plans/feat/tools/plan_fields.plan.toml:15
test/plans/feat/tools/selfchecks.plan.toml:169
test/plans/feat/trans/cml_sweep.plan.toml:15
test/plans/feat/trans/same_graph.plan.toml:3
test/plans/feat/transforms/compositions.plan.toml:77
test/plans/feat/transforms/compositions.plan.toml:116
test/plans/feat/transforms/compositions.plan.toml:153
test/plans/feat/transforms/compositions.plan.toml:169
test/plans/feat/transforms/compositions.plan.toml:185
test/plans/feat/transforms/nested.plan.toml:3
test/plans/feat/transforms/nested.plan.toml:64
test/plans/feat/transforms/nested.plan.toml:131
test/plans/feat/transforms/nested.plan.toml:147
test/plans/feat/transforms/split.plan.toml:22
test/plans/feat/transforms/split.plan.toml:26
test/plans/feat/transforms/split.plan.toml:127
test/plans/feat/transforms/split.plan.toml:333
test/plans/feat/transforms/split.plan.toml:339
test/plans/feat/transforms/split.plan.toml:392
test/plans/feat/transforms/split.plan.toml:413
test/plans/gpu/tiles.plan.toml:1632
test/plans/perf/fib.plan.toml:4
test/plans/perf/gemm_threads.plan.toml:18
test/plans/perf/showcase_c2.plan.toml:66
test/plans/perf/showcase_c2.plan.toml:118
test/plans/perf/showcase_c3.plan.toml:27
test/plans/perf/showcase_c3.plan.toml:56
test/plans/perf/showcase_c3.plan.toml:119
test/plans/perf/showcase_c3.plan.toml:148
test/plans/perf/showcase_c3.plan.toml:152
test/plans/perf/showcase_c3.plan.toml:162
test/plans/perf/showcase_c3.plan.toml:187
test/plans/perf/showcase_c3.plan.toml:197
test/plans/perf/showcase_c3.plan.toml:218
test/plans/perf/showcase_c3.plan.toml:230
test/plans/perf/showcase_c3.plan.toml:242
test/plans/perf/showcase_c3.plan.toml:253
test/plans/probe/refleak.plan.toml:2
test/plans/probe/refleak.plan.toml:14
test/plans/probe/refleak.plan.toml:16
test/plans/probe/refleak.plan.toml:148
test/probe/refleak/loop_fault_ref.cml:4
test/probe/refleak/loop_nested_after.cml:4
test/probe/refleak/loop_nested_outer_ref.cml:4
test/probe/refleak/loop_outer_ref.cml:3
test/runner.mjs:966
test/tools/check-no-algo.mjs:60
test/tools/check-plan-fields.mjs:13
test/tools/check-vjp-parity.mjs:15
test/tools/check-vjp-ruleset.mjs:25
test/tools/diff-compare.mjs:25
test/tools/diff-compare.mjs:38
test/tools/diff-gate/gate.mjs:6
test/tools/diff-gate/gate.mjs:25
test/tools/diff-gate/gate.mjs:140
test/tools/import-audit.mjs:6
test/tools/opt-cost/measure.mjs:14
test/tools/opt-cost/measure.plan.toml:16
test/tools/opt-cost/measure.plan.toml:17
test/tools/para-speedup.mjs:58
test/tools/parsecheck/CMakeLists.txt:5
test/tools/reading-refusal.mjs:13
test/tools/roundtrip-compare.mjs:101
test/tools/roundtrip-compare.mjs:110
test/tools/run-tests.mjs:431
test/tools/run-tests.mjs:432
test/tools/run-tests.mjs:1269
test/tools/run-tests.mjs:1803
test/tools/run-tests.mjs:1850
test/tools/run-tests.mjs:3851
test/tools/run-tests.mjs:5366
test/tools/runner-output.mjs:186
test/tools/selection-docs-only.test.mjs:6
test/tools/selection-docs-only.test.mjs:7
test/tools/selftest/run.mjs:68
test/tools/selftest/run.mjs:69
test/tools/selftest/run.mjs:70
test/tools/selftest/run.mjs:81
test/tools/selftest/run.mjs:1481
test/tools/selftest/selection-cases.mjs:276
test/tools/selftest/selection-cases.mjs:317
test/tools/selftest/selection-cases.mjs:582
test/tools/startup-cost.mjs:50
test/tools/startup-cost.mjs:299
```
