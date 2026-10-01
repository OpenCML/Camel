# Exemplars

Eight small programs, one per directory, that each show one property of Camel's
graph-based ML stack end to end. Every directory holds:

- `program.cml` - the program, with a header comment saying what it shows;
- `exemplar.toml` - the runs to make (passes, environment) and what each must
  produce: exit status, output, diagnostic name and source position, the
  operators and inputs/outputs of an exported ONNX model;
- `expected/` - expected outputs compared exactly (program output, annotated
  graph dumps from `std::argir`);
- `run.log` - the full record of the last recorded run.

`node exemplars/check.mjs` re-runs every exemplar and checks it (the test plan
`test/plans/feat/exemplars.plan.toml` runs it per exemplar);
`node exemplars/check.mjs --update [name ...]` re-records `expected/` and
`run.log` from the current build.

| # | Exemplar | Shows |
|---|---|---|
| 01 | [Scalar boundary](01_scalar_boundary) | Tensor elements read back to the host decide a branch, host numbers re-enter tensor math; the annotated dump marks each crossing. |
| 02 | [Branch shape contract](02_branch_shape_contract) | Arms of different static shapes join to `Tensor<float32, [?, ?]>`; the operator that needs a shape fails at run time at its own column, with both extents. |
| 03 | [Export capability](03_export_capability) | `Relu` on int64 is valid ONNX but ONNX Runtime's CPU provider has no kernel: the export stops at the `relu` call ([PyTorch comparison](../benchmarks/capability_demo.py)). |
| 04 | [Staticness](04_staticness) | Recursion with shape-determined depth unrolls and exports; value-determined depth stops the export at the recursive call. |
| 05 | [Effect and callback order](05_effect_order) | Prints inside a callback keep their order under NodeVM, FastVM and after inlining. |
| 06 | [Loop-carried graph state](06_loop_carried_state) | A training loop carries the parameter struct through recursion; `value_and_grad` + `sgd` make the next state. |
| 07 | [Export graph integrity](07_export_integrity) | The training step exports whole (parameters in, loss and updates out); an effect stops the export at the effect. |
| 08 | [Structured control flow](08_structured_control_flow) | A branch on an input-dependent condition exports as an ONNX `If`. |
