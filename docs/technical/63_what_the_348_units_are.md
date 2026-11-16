# 63. 量的是哪 348 个单元：`:var/new` 的五种来由

`list_sha=ac8288b4f2895df5a0dea5755a4b22c8d81e680524619f61cabc04b98c893f8b`

口径就是 `scripts/check-capture-census.mjs` 的口径：`test/cases/` 下所有 `.cml`，排序；`files=3853
listed=3853 counted=2688`，`var_new=348`、`var_new_empty=44`、`cell_typed_nodes=1922`。本页把这 348 个按
「它为什么存在」拆开，并对每一类回答一句：**建图时直接构造 SSA（名字→当前值映射、赋值改映射、JOIN 与循环头放 φ）
之后，这个单元还在不在。**

判据全部取自节点与它所在图的形状，不靠文件名：`std::jgir` 的 JSON 里，节点看 `label` / `outputType` /
`callee` / `parameter`，边看 `normal|context|control|order|sync`，图看 `capturePorts` 与 `regions`。一次性只读
脚本在 `D:\Temp\cellcount-classify.mjs`（宿主 `/tmp/cellcount-classify.mjs`，结果 `/tmp/cellcount-classify-out.json`），
**不进仓库**。它对同一批文件得同一个 `list_sha`、同样 348 个节点，所以下面的数就是这 348 的拆分。

## 表

| 类 | n | 文件 | 是什么（怎么读出来的） | 直接 SSA 之后 |
|---|---:|---:|---|---|
| **A** | 0 | 0 | 声明图名以 `__gen_` 开头、或声明图调用 `__gen_*` 的 `:var/new` | 无对象 |
| **B-机器** | 64 | 23 | 捕获目标是调用 `__gen_step__` 的图（捕获口名是 `__gen_pc__`/`__gen_halt__`/生成器局部） | 机器的状态包，不是程序的单元（作者 2026-11-16 第 4 条） |
| **B-程序** | 53 | 44 | 被程序写的闭包捕获；43 只读、10 在闭包图里被写（10 个全声明在 `__root__`） | 43 消失；10 → `Ref<T>` |
| **C** | 81 | 55 | 同一非闭包图里一读一写，没被闭包捕获、没作 `var` 实参、没进容器 | 81 消失；55 不需要 φ，26 变 φ |
| **D** | 79 | 51 | 交给 `var` 形参的单元（今天形参就是调用者的单元，写回是调用处的赋值） | 79 消失（传入值、传回新值） |
| **E-无读者** | 54 | 28 | 有写，但一个读算子（`:var/get`、`:var/peek`、`:var/take`、`:var/after`）都没有 | 消失——但见下面的 §c，**不是「死单元」的计数** |
| **E-无写者** | 8 | 8 | 只有初值，没有 `:op/assn*` | 消失（就是个 `let`） |
| **E-闭包局部** | 5 | 2 | 声明、读、写都在同一个闭包图里 | 消失 |
| **E-判不了** | 4 | 4 | 只交给一个 CALL，函数值来自端口，GIR 里没有 callee | 判不了（见问题页） |
| **合计** | **348** | — | — | **270 消失 / 78 留** |

**78 留** = 64 机器状态（不属 `var` 语义）+ 10 模块级共享（→ `Ref`）+ 4 判不了。

## A：一个数字换了口径就是另一个数

**A = 0（按声明图：机器的单元声明在外层函数图，不在生成器体里）；若按「被捕获进机器图」计，是 64，那 64 属于
B 的机器那一半。**

两个数都成立，因为它们数的**不是同一批东西**：`check-capture-census` 的
`capture_ports_generator=300` 数的是**捕获口**，这一页数的是 **`:var/new` 节点**。生成器把状态单元声明在外层
函数的图里（`test/cases/compile/stream/generators.cml` 的 `count`/`quad`/`sign`/`reading`），再由调用
`__gen_step__` 的 `__g_*` 图捕获进去；生成器体自己的局部是另一个算子 `:var/new/empty`（44 个，census 单列）。
把 64 说成「A」或把它并进「程序写的闭包」，下一个读这两个数的人都会以为少了 64 或多了 64。

## C：81 个里 55 个是这一轮最干净的一格

判据：① 声明图既不是生成器图（两条 `__gen_` marker 都不成立）、也不是闭包体（不是任何 FILL 的函数体）；
② 从单元出发、把 GATE 与 `:var/after` 当透明，下游 `normal` 用户只有 `:var/get|peek|take|after` 与
`:op/assn*`，没有 CALL/FUNC/ACCS/FILL/普通算子接手，也没有 `context` 边进入 FILL（＝没被闭包捕获）；
③ 至少一个读者、至少一个写者；④ 没有任何一处的 callee 形参是 `var T`（那归 D）。②③④ 一起就给出了
「没作 `var` 实参传出、没存进容器」。这套判据与编译器自己判「值有没有观察者」的
`src/execute/exec_plan.cpp#cellEscapes`（GATE 透明、非 `:var/*`/`:op/assn` 的用户即逃逸）同构，只是把「捕获」单列。

81 个全部满足（C 里没有逃逸的）。其中 **55 个读写都落在 body 区、连 φ 都不需要**；26 个跨区：
13 个只在 `arm` 区、7 个 `body+loop_body`、5 个 `arm+body`、1 个 `block+body`——这 26 个在 SSA 里变成汇合处或
循环头的 φ，**只报数，不算「可删」**。

## B：不会整体留下，只有 10 个留下

107 个只读捕获在 SSA 之后消失：闭包捕获的是创建时这个名字的值（`docs/design-principles.cn.md` 3.5），
端口变成值端口，不需要共享物。10 个在闭包图里被写的是**模块级 `var`（全在 `__root__`）**，落在 `Ref<T>`
（句柄是值，闭包捕获句柄），不是图内的 φ。依据：3.5 的「闭包不能给外面的名字赋值，诊断给三种改法：返回新值、
循环状态/fold、用 Ref」与同节的 `Ref<T>` 段；今天它确实是「被捕获的单元」有据——
`src/compile/gir/captures.cpp#Builder::captureInto` 里 `const bool isVar = isVariable(origin)` 之后走的就是把
单元本身作为捕获值的路；`test/cases/effects/callback_write_through_parameter.cml` 的注释也这么写：
「The state a closure may write is a handle or a module `var`」。

## D：作者那句与代码相符

`src/compile/gir/builder_calls.cpp#Builder::requireMutableArgument` 的原话是
「The argument is handed to the callee as a resource: the cell is shared with it, so it keeps its cell and is
never made SSA」与「A `var` parameter is the caller's cell, and the callee's write-back is the assignment
written at the call」；`:var/after` 的签名就是 `(variable: var T, after: any) => var T`
（`src/builtin/modules/globals.cpp#:var/after`，注释「the cell as it stands after the call that wrote it」）。
所以「`var` 形参照写回、`:var/after`/`take`/`peek` 那套单元式接口随之删除」与代码相符。
**唯一对不上的一处**：`:var/take` 今天不只服务 `var` 形参，它还围着 `xs = xs->sort` 的原地形式
（`globals.cpp#:var/take` 注释、`Builder::inPlaceRebindingOf`）；「原地」属 10.5，那句话覆盖不到（见问题页）。

## §c 54 不是「可以删」的 54

```
54 is the count of cells with no reader, not the count of dead cells: in GIR "nothing reads this value"
and "the readers were folded onto the written value" look the same. Do not read 54 as "safe to delete".
```

GIR 里「值没人用」和「读者已被折叠到写出的值上」同形。`test/cases/consistency/values/int_compound_wrap.cml`
就是后者：`a += 1` 之后 `println(a)` 的值**直接从 `:op/add_l` 取**，中间没有任何 `:var/get`，单元只剩「写的位置」。
两者都说明单元可以没有，但**不是同一件事**，所以这一格只报「没有读者」。

## 名单（去重；前缀 `test/cases/` 已省；同一文件可在多类，因为它有多个单元）

- **A（0）**：无。
- **B-机器（23）**：`compile/stream/generators.cml`（8）、`generators_shapes_consumed.cml`（6）、
  `generators_error_element.cml`（4）、`generators_loop_computed_initialiser.cml`、`generators_sync_order.cml`、
  `generators_body_fault.cml`、`generators_cell_type.cml`、`generators_destructured_local.cml`、
  `generators_early_stop.cml`、`generators_error_binding_across_yield.cml`、`generators_for_yield.cml`、
  `generators_local_across_yield.cml`、`generators_loop_shared_cell.cml`、`generators_loop_two_states.cml`、
  `generators_loop.cml`、`generators_match_binding_across_yield.cml`、`generators_match.cml`、
  `generators_no_sync.cml`、`generators_yield_in_block.cml`、`yield_formatting.cml`、`yield_multi_value.cml`、
  `yield_statement.cml`、`compile/var_closure/ok_generator_body_is_not_a_program_closure.cml`。
- **B-程序（44）**：`compile/decorators/blocks.cml`、`compile/function_values/captured_var_in_two_arms.cml`、
  `compile/function_values/nested_function_value.cml`、`compile/generics/effects.cml`、
  `compile/nullable_assign.cml`、`compile/state_escape/ok_read_escape_array.cml`、`…_field.cml`、
  `…_into_closure.cml`、`…_return.cml`、`…_stored_by_callee.cml`、`compile/var/capture_value_port.cml`、
  `effects/callback_write_ignored.cml`、`effects/callback_write_through_parameter.cml`、
  `effects/effect_through_parameter.cml`、`effects/module_var_read.cml`、`effects/module_var_write.cml`、
  `effects/overcounted_writer_keeps_the_value.cml`、`effects/two_calls_through_values_keep_both_writes.cml`、
  `effects/two_references_write_one_object.cml`、`effects/two_references_write_one_object_unsynced.cml`、
  `effects/unreachable_pair/{with,without}/reading.cml`、
  `effects/value_made_after_a_call_captures_its_version.cml`、`linear/byref/captured_write_graph.cml`、
  `…_order.cml`、`…_value.cml`、`linear/nvm/fib1.cml`、`linear/runtime/partial_application_written_var.cml`、
  `linear/toplevel_var/{argument_order,counter,grad_refused,initial_value,unread_write}.cml`、
  `modules/autodiff/grad_captured_var_written.cml`、`modules/autodiff/grad_module_var_sync.cml`、
  `modules/onnx/refusals/var_outer.cml`、`modules/selected_protection/{capture,dead_write_held}.cml`、
  `opt/adb67e081/rc04_seed72_within_call.cml`、`opt/effects/dead_write_aggregate_two_members.cml`、
  `opt/effects/dead_write_held_by_program.cml`、`opt/effects/dead_write_rooted_closure.cml`、
  `opt/generic/effect_order_folded.cml`、`opt/inline_cycle/seed23_fold_inline_within_call.cml`。
- **C（55）**：`linear/byref/path_assigned.cml`（5）、`consistency/entering/assignment.cml`（4）、
  `modules/stdlib/array_sort_kernels.cml`（4）、`consistency/entering/arm_cell_void.cml`（3），其余每个 1–2：
  `consistency/new_values/{peek,append_new,macro_append}.cml`、`trans/cpp/{field_store,complex_scalar,
  recursive_struct_shapes}.cml`、`compile/enum/gc_stress.cml`、`compile/semantic/{recursive_struct_array,
  recursive_struct_array_gc}.cml`、`consistency/operator_overloads/basic.cml`、
  `consistency/declared_type/same_type.cml`、`consistency/faults/element_assigned_out_of_range.cml`、
  `linear/byref/{captured_write_order,captured_write_graph,element_assigned,element_assigned_append,
  element_assigned_copy,field_assigned,inplace_write_then_read,path_assigned_allocs,var_alias_captured,
  var_alias_two_cells,var_param_slot_array}.cml`、`linear/runtime/{var_read_before_write,
  var_read_before_region}.cml`、`linear/nvm/nested_array_type.cml`、`modules/nn/{lrit,lr}.cml`、
  `modules/autodiff/{var_array_append,var_tensor_local}.cml`、`modules/onnx/{export_var_append,export_var_local,
  refusals/var_arm}.cml`、`modules/optim/{agreement,dtype_exact,named_args,native_dtype_exact,training,
  written_dtype_exact}.cml`、`modules/tensor/static_types.cml`、`modules/tiles/resident_cpu_block.cml`、
  `opt/generic/written_variable.cml`、`trans/cml/{arm_write,arm_read}.cml`、
  `compile/array_alias/var_widening.cml`、`compile/function_values/value_positions.cml`、
  `compile/lambda_inference/positions.cml`、`compile/operators/{struct_add_assign,tensor_add_assign}.cml`、
  `compile/tagged/combined.cml`、`compile/transforms/cml_var_holding_a_lambda.cml`。
- **D（51）**：`linear/byref/var_param_reference.cml`（11）、`compile/var/var_parameter_write_at_exit.cml`（5）、
  `linear/byref/var_rule_subpasses.cml`（5），其余：`linear/byref/{var_writes_through_recursion,
  var_read_before_callee_write,order_before_after,order_direct_user,var_alias_read_then_param,
  var_alias_through_param,var_argument_declared,var_argument_reads_the_nearest_write,
  var_argument_statement_place,var_param_jit,var_param_slot_array,var_struct_argument}.cml`、
  `linear/nvm/{var_params,var}.cml`、`linear/runtime/{var_read_before_write,var_between_statements}.cml`、
  `linear/toplevel_var/argument_order.cml`、`linear/operators/string_plus_assign.cml`、
  `compile/generics/features.cml`、`compile/nullable_argument.cml`、`compile/diag_json/{dropped_effects,
  warnings_visible}.cml`、`compile/state_escape/ok_read_escape_{array,field}.cml`、
  `compile/var_closure/ok_update_reads_captured.cml`、`consistency/entering/arm_cell.cml`、
  `consistency/faults/fault_in_sync_call.cml`、`consistency/values/sync_body_order.cml`、
  `effects/{declared,var_parameter_write_outside_sync}.cml`、`modules/autodiff/{var_tensor_read,
  var_through_with_grad}.cml`、`modules/nn/{lr,lrit}.cml`、`modules/onnx/{export_var_parameter,
  refusals/var_param}.cml`、`modules/tensor/{compound_assign,var_tensor_reference}.cml`、
  `modules/tensor/shape_inference/{var_argument_plain,var_argument_with_parameter}.cml`、
  `opt/inline/loop_wait_after_inline.cml`、`opt/progen/findings/{simplify_effect_order,
  simplify_recursion_never_ends}.cml`、`opt/progen/judgement/bare_arm_var_call_warning.cml`、
  `para/core/sync_order_with_tasks.cml`、`trans/cml/var_param.cml`、`trans/cpp/{exec/variables,
  var_argument_cell}.cml`。
- **E（42）**：`consistency/values/int_compound_wrap.cml`（9）、`linear/operators/compound_assign.cml`（6）、
  `modules/stdlib/reduced_compound_assign.cml`（6）、`compile/nullable_assign.cml`（4），其余：
  `trans/cpp/exec/{variables,composites}.cml`、`compile/array_shape/{rank_zero_slot_var,
  rank_zero_var_slot}.cml`、`linear/runtime/var_between_statements.cml`、`modules/onnx/export_var_compound.cml`、
  `modules/optim/native_dtype_exact.cml`、`trans/canon_order_run.cml`、
  `compile/comprehension_context/annotated.cml`、`compile/literals/comprehension_in_position.cml`、
  `compile/nullable_argument.cml`、`compile/var/unused_assigned_value_both_bodies.cml`、
  `consistency/logical/short_circuit.cml`、`consistency/surface/minus.cml`、
  `consistency/values/{closure_var,ref_takes_the_value}.cml`、`linear/byref/{captured_write_order,
  var_alias_two_cells,var_param_reference,var_rule_subpasses}.cml`、`linear/match/let_multi_binding.cml`、
  `linear/operators/{mod_assign_by_zero,string_plus_assign}.cml`、`modules/autodiff/{grad_tuple_read,
  var_tensor_read_after_call,var_tensor_read_after_write,var_tensor_read_in_callee}.cml`、
  `modules/graph_pass/neutral_var_target.cml`、`modules/nn/mnist.cml`、`modules/tensor/static_types.cml`、
  `opt/camel_passes/cse_subject.cml`、`opt/generic/{control_chain,written_variable}.cml`、
  `opt/progen/findings/{mod_assign,string_plus_assign}.cml`、`tools/debugger/fixtures/values.cml`、
  `trans/cml/linear.cml`。

## 这一页不做的

不提议、不实施任何代码改动。判不了的部分（4 个 E 的 CALL、`:var/take` 的原地用途、CAST 逃逸）写在
`docs/handover/questions/2026-11-16-how-many-units-does-a-direct-ssa-build-remove.cn.md`，一页一问。