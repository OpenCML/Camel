# npm Script Reference

This document describes the `package.json` scripts in the repository root.

## Common Build Scripts

| Script | Description |
|------|------|
| `npm run build` | Release build |
| `npm run debug` | Debug build |
| `npm run profile` | RelWithDebInfo build |
| `npm run config` | Edit CMake options interactively |
| `npm run check` | Run repository checks |
| `npm run format` | Format C++ sources under `src/` and `include/` |
| `npm run clean` | Remove build artifacts |
| `npm run pypi:rollup` | Build the PyPI wheel bundle |

## Test Harness

The repository now uses the plan-based harness under `test/`.

- runner: `node test/tools/run-tests.mjs`
- plans: `test/plans/**/*.plan.toml`
- cases: `test/cases/**`
- shared args templates: `test/vars.toml`

## Main Test Scripts

| Script | Description |
|------|------|
| `npm run test` | Main regression set: `smoke`, `regression`, `errors`, `modules`, `run/ln`, `run/jit`, `run/nn`, `macro`, `trans` |
| `npm run test:all` | Full suite including `passes/inline`, `perf`, and `run/para` |
| `npm run test:smoke` | Smoke-only suite |
| `npm run test:regression` | Alias of the main regression entry |
| `npm run test:errors` | Diagnostic error cases |
| `npm run test:modules` | Module suite only |
| `npm run test:macro` | Macro suite only |
| `npm run test:passes` | Inline-pass suite only |
| `npm run test:perf` | Benchmark/performance suite only |
| `npm run test:trans` | Translation/export suite only |

## Run Suite Scripts

| Script | Description |
|------|------|
| `npm run test:run` | All `run/*` suites (`ln`, `jit`, `nn`, `para`) |
| `npm run test:run:ln` | `run/ln` only |
| `npm run test:run:jit` | `run/jit` only |
| `npm run test:run:nn` | `run/nn` only |
| `npm run test:run:para` | `run/para` only |

## Direct Runner Examples

```powershell
node test/tools/run-tests.mjs
node test/tools/run-tests.mjs test/plans/modules.plan.toml
node test/tools/run-tests.mjs test/plans/run/ln.plan.toml
node test/tools/run-tests.mjs --tier benchmark test/plans/perf/fib.plan.toml
node test/tools/run-tests.mjs --update-golden test/plans/passes/inline.plan.toml
```

## Performance Helper Scripts

| Script | Description |
|------|------|
| `npm run perf:samply` | Record `test/cases/perf/fib_recursive.cml` with `samply` |
| `npm run perf:record` | Record the same case with `perf` |
| `npm run perf:report` | Open the latest `perf` report |
| `npm run perf:render` | Record and open the profile with `hotspot` |

## Notes

- Build first: `npm run build`.
- `test:perf` plans drive pass-level measurements through test `args`, for example `["${timeit_nvm_10}", "${case}", "${run_nvm}"]`, instead of relying only on outer process wall time.
- Environment-sensitive suites may report `skip` or `xfail` instead of `pass`.
- `xpass` is treated as actionable because it usually means a known regression is fixed and the manifest should be promoted back to a normal passing test.
