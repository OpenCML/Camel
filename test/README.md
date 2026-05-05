# Test Layout

- `cases/` contains source assets grouped by feature area and test purpose: `smoke`, `parse`, `compile`, `opt`, `trans`, `linear`, `para`, `modules`, `perf`.
- `plans/` contains suite-level plans. Main feature coverage lives under `plans/feat/`, while smoke and perf keep dedicated top-level entries.
- `tools/` contains the runner and helpers.
- `results/` stores execution logs.
- `vars.toml` defines reusable command fragments.

Plan entries run `camel` by default. Set `tool = "camel-format"` when a case must execute the
standalone formatter instead of the main compiler/runtime CLI.
