# Attribution of the fast-layer red (flip7, 489 failing, generated)

Every failing test bucketed by its first diagnostic, with the internal errors and the NoMatchingFunction holes listed one by one, as asked.

## Buckets

- **162** -- kernel-refusal: grad_library, grad_library.safe, grad_library.eight_threads, var_grad.default, var_grad.fvm, var_grad.jit
- **92** -- exit-code: library_gemm.tolerance, library_gemm.tolerance.safe, library_gemm.tolerance.eight_threads, library_gemm.tolerance.safe.eight_threads, catalog.doc_is_generated, entry_args.struct_directory_and_result
- **47** -- diag:SemanticDiag::NoMatchingFunction: boundaries.agir, boundaries.argir, boundaries.tooltip_slots, module_import_builtins, arms_laid_out_whole_tensor.default, arms_laid_out_whole_tensor.simplify_nvm
- **43** -- INTERNAL: nn.lr, var_tensor.default, var_tensor.fvm, var_tensor.jit, var_tensor.tf, var_tensor.inline
- **33** -- text: [INFO] verification exited 1 |  [INFO] verif: shape_unroll.simplify, effect_order_folded.nvm, effect_order_folded.simplify.nvm, effect_order_folded.simplify.fvm, effect_order_folded.simplify.jit, effect_order_folded.inline.fvm
- **18** -- diag:RuntimeDiag::RuntimeError: softmax_classifier, softmax_classifier_fused, grad_through_as.runtime.nvm, grad_through_as.macro.nvm, grad_through_as.simplified.nvm, ort_grad_loop
- **14** -- text: [INFO] expect_stdout_exact differs: first di: native.tensor_show_declaration_import, grad_scalar_width.runtime.nvm, grad_scalar_width.macro.fvm, int64_sum_exact.nvm, int64_sum_exact.fvm, argmax_ieee.nvm
- **11** -- text: [INFO] output does not contain 'conversion t: fold_cast.failing_fails_at_run_time, as_runtime_mismatch, si_as_fails, si_as_fails_fvm, si_as_fails_jit, si_as_fails_prl
- **8** -- text: [INFO] stdout does not contain '8., 16., 24.: tensor.execution.nvm, tensor.execution.fvm, tensor.execution.jit, tensor.execution.prl, tensor.rules_simplify_execution.nvm, tensor.rules_simplify_execution.fvm
- **5** -- text: [INFO] output does not contain 'the tensor h: entry_args.tensor_wrong_shape, fault.as_shape.nvm, fault.as_shape.fvm, fault.as_shape.jit, fault.as_shape.prl
- **5** -- text: [INFO] snapshot differs from golden |  [stdo: algebra_rules.tensor.by_cpp, examples.ml.export_onnx.nvm, examples.ml.export_onnx.fvm, examples.ml.export_onnx.jit, examples.ml.export_onnx.prl
- **4** -- text: [INFO] output does not contain 'the example : example_refusal.tuple.pass, example_refusal.tuple.operator, example_refusal.shape.pass, example_refusal.shape.operator
- **4** -- text: [INFO] stdout does not contain '[ 2., 4., 6.: reshape_view_gc.fold_specialize.nvm, reshape_view_gc.nvm, reshape_view_gc.fvm, reshape_view_gc.jit
- **4** -- text: [INFO] stdout does not contain 'adam_update_: adam_update_bits.nvm, adam_update_bits.fvm, adam_update_bits.jit, adam_update_bits.prl
- **4** -- text: [INFO] stdout does not contain 'take runtime: take_runtime_out_of_range.nvm, take_runtime_out_of_range.fvm, take_runtime_out_of_range.jit, take_runtime_out_of_range.prl
- **4** -- text: [INFO] output does not contain 'this convers: as_refusal.always_fails.pass, as_refusal.always_fails.operator, as_refusal.constant_fails.pass, as_refusal.constant_fails.operator
- **3** -- text: [INFO] stdout does not contain '0.5546' |  [: entry_args.tensor_npy, entry_args.tensor_npy_jit, entry_args.tensor_npy_tf
- **3** -- text: [INFO] stdout does not contain 'tensor([ 8.,: rules.tensor_laws_execute.nvm, rules.tensor_laws_execute.fvm, rules.tensor_laws_execute.prl
- **2** -- text: [INFO] output does not contain 'iter: a rank: loops.fault_tensor_rank0.nvm, loops.fault_tensor_rank0.fvm
- **2** -- text: [INFO] output does not contain 'tensor(' |  : regions.survive_commit, regions.survive_optimization
- **2** -- text: [INFO] output does not contain 'the conversi: as_refusal.unproven.pass, as_refusal.unproven.operator
- **1** -- text: [INFO] stdout does not contain 'tensor(' |  : regions.inline_runs
- **1** -- text: [INFO] stdout does not contain 'tensor([ -na: rules.tensor_guards_are_real
- **1** -- text: [INFO] stdout does not contain 'tensor([ 0.,: rules.tensor_guards_hold
- **1** -- text: [INFO] stdout does not contain 'tensor([ 5.,: si_row_loop
- **1** -- text: [INFO] stdout does not contain 'dtype=float3: unknown_extent
- **1** -- text: [INFO] output does not contain 'Tensor<float: as_static_mismatch
- **1** -- text: [INFO] output does not contain 'the value (': si_branch_join
- **1** -- text: [INFO] output does not contain 'the element : string_refused
- **1** -- text: [INFO] output does not contain 'slice bounds: slice_const_bounds
- **1** -- text: [INFO] output does not contain 'rank 2 or mo: slice_rank2
- **1** -- text: [INFO] output does not contain 'index 5 is o: index_declared_shape
- **1** -- text: [INFO] output does not contain 'idx: tensor : unknown_runtime
- **1** -- text: [INFO] output does not contain 'element .0 o: si_tuple_return
- **1** -- text: [INFO] output does not contain 'does not mat: tensor_shape
- **1** -- text: [INFO] output does not contain 'cannot diffe: refuse_cast_array
- **1** -- text: [INFO] expected diagnostic SemanticDiag::Loo: loops.err_tensor_rank0
- **1** -- diag:RuntimeD: gru_lite_large_bytecode
- **1** -- text: [INFO] output does not contain 'Translation : refuse_tensor
- **1** -- diag:Runti: index_const

## The internal errors, one name each (43)

- nn.lr -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | [stdout] graph 'main' node 28 (10): passe
- var_tensor.default -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.fvm -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.tf -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.inline -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.specialize -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.devirtualize -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.fold -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.cse -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.dce -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.opt -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.simplify -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_inline -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_specialize -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_devirtualize -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_fold -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_cse -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_dce -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_opt -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_simplify -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.all_6 -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.all_macro_9 -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.opt_fvm -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.opt_jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.opt_tf -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.inline_fvm -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.inline_jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.inline_tf -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.specialize_jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.simplify_jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_opt_jit -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor.macro_opt_tf -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32
- var_tensor_read.default -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | graph 'main' node 14 (10): passes a var fl
- var_tensor_read.macro_fvm -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | graph 'main' node 14 (10): passes a var fl
- var_tensor_read.macro_simplify_jit -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | graph 'main' node 14 (10): passes a var fl
- var_tensor_read.opt_macro_nvm -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | graph 'main' node 14 (10): passes a var fl
- lr -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | [stdout] graph 'main' node 28 (10): passe
- grad_effects_kept.macro.simplify -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | graph 'main' node 40 (10): passes a float
- grad_function_values_arms.macro.simplify -- Runtime graph '__root__' breaks its edge invariants (1 violation(s)): | graph '__root__' node 40 (10): passes
- si_var_argument_plain -- Runtime graph 'main' breaks its edge invariants (1 violation(s)): | [stdout] graph 'main' node 6 (10): passes
- poison:linear.var_reference:var_tensor.default -- Runtime graph 'main' breaks its edge invariants (2 violation(s)): | graph 'main' node 5 (10): passes a var f32

## The NoMatchingFunction holes, per hole (0)


Generated mechanically from D:\Temp\h366-flip7.log; the per-hole verdict (defect in the flip or declaration to move) follows from whether the missing name is touched by the flip's diff.

## The 47 holes: the mechanical half of the verdict

For each hole, whether the missing name appears anywhere in this branch's diff against 221cfe18b. A name the flip touches is a defect in the flip (the flip renamed or removed a declaration a consumer still uses); a name the flip never touches is a declaration that has not caught up, to move in its own lane. The sentence per hole still has to be written; this is the split it starts from.

### The flip touches the name (0)


### The flip never touches the name (0)

