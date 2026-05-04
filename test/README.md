# Test Layout

- `cases/` contains source assets grouped by feature area and test purpose: `smoke`, `parse`, `compile`, `opt`, `trans`, `linear`, `para`, `modules`, `perf`.
- `plans/` contains suite-level plans. Main feature coverage lives under `plans/feat/`, while smoke and perf keep dedicated top-level entries.
- `tools/` contains the runner and helpers.
- `results/` stores execution logs.
- `vars.toml` defines reusable command fragments.
