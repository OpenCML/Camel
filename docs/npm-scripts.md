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
| `npm run test` | Main set: `smoke`, `regression`, `functional/**` |
| `npm run test:all` | Full suite including `perf/**` |
| `npm run test:smoke` | Smoke-only suite |
| `npm run test:regression` | Alias of the main regression entry |
| `npm run test:functional` | All functional suites |
| `npm run test:parse` | Parse suite only |
| `npm run test:compile` | Compile/semantic suite only |
| `npm run test:opt` | Optimization-pass suite only |
| `npm run test:linear` | Linear scheduler / VM suites |
| `npm run test:linear:nvm` | NVM/default linear suite only |
| `npm run test:linear:jit` | JIT linear suite only |
| `npm run test:para` | Parallel runtime suite only |
| `npm run test:modules` | Module suite only |
| `npm run test:modules:std` | Stdlib module suite only |
| `npm run test:modules:nn` | NN module suite only |
| `npm run test:perf` | Benchmark/performance suite only |
| `npm run test:trans` | Translation/export suite only |

## Direct Runner Examples

```powershell
node test/tools/run-tests.mjs
node test/tools/run-tests.mjs test/plans/functional/modules
node test/tools/run-tests.mjs test/plans/functional/linear/nvm.plan.toml
node test/tools/run-tests.mjs --tier benchmark test/plans/perf/fib.plan.toml
node test/tools/run-tests.mjs --update-golden test/plans/functional/parse/core.plan.toml
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
- Negative cases live in their owning functional plans and assert diagnostic name/code/line/column directly.
- Environment-sensitive or known-bad cases still fail the suite; optional `failure_note` text is only explanatory output.
