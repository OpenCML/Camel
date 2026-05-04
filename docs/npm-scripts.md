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

The repository uses the plan-based runner under `test/`.

- runner: `node test/runner.mjs`
- executor: `node test/tools/run-tests.mjs`
- plans: `test/plans/**/*.plan.toml`
- cases: `test/cases/**`
- shared args templates: `test/vars.toml`

## Main Test Scripts

| Script | Description |
|------|------|
| `npm run test` | Main set: `smoke`, `feat/**` |
| `npm run test:all` | Full suite (`test/plans/`) |

Use `npm run test -- <target>` for everything else instead of adding more fixed aliases.

## Direct Runner Examples

```powershell
node scripts/test.js
node scripts/test.js test/plans/feat/modules
node scripts/test.js test/plans/feat/linear/recursion_and_calls.plan.toml
npm run test -- perf
node test/tools/run-tests.mjs --update-golden test/plans/feat/parse/core.plan.toml
```

## Target Shorthand Examples

```powershell
npm run test -- feat.trans.macro
npm run test -- feat.trans
npm run test -- trans.macro
npm run test -- perf.fib
npm run test -- linear.collections_and_structs
```

Dot targets resolve against `test/plans/`: `feat.trans.macro` maps to
`test/plans/feat/trans/macro.plan.toml`, while `feat.trans` maps to the whole
`test/plans/feat/trans/` directory. Feature-suite names may omit the `feat.`
prefix when unambiguous, so `trans.macro` resolves to the same macro plan.

## Performance Helper Scripts

| Script | Description |
|------|------|
| `npm run perf:samply` | Record `test/cases/perf/fib_recursive.cml` with `samply` |
| `npm run perf:record` | Record the same case with `perf` |
| `npm run perf:report` | Open the latest `perf` report |
| `npm run perf:render` | Record and open the profile with `hotspot` |

## Notes

- Build first: `npm run build`.
- Perf plans under `test/plans/perf/` drive pass-level measurements through test `args`, for example `["--timeit", "std::nvm/1", "--output-format", "json", "${case}", "${run_nvm}"]`, instead of relying only on outer process wall time.
- Negative cases and regression guards both live in their owning feature plans and assert diagnostics or outputs directly.
- Environment-sensitive or known-bad cases still fail the suite; optional `failure_note` text is only explanatory output.
