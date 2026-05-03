# Test Layout

- `cases/` contains source assets grouped by test purpose: `smoke`, `regression`, `parse`, `compile`, `opt`, `trans`, `linear`, `para`, `modules`, `perf`.
- `plans/` contains suite-level plans grouped the same way. Functional coverage lives under `plans/functional/`.
- `tools/` contains the runner and helpers.
- `results/` stores execution logs.
- `vars.toml` defines reusable command fragments.
