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
| `npm run clean` | Remove build trees, installs, test scratch and polluted source files |
| `npm run pypi:rollup` | Build the PyPI wheel bundle |

## Cleaning (`npm run clean`)

`scripts/clean.js` removes the trees a build, an install and a test run leave behind. It never
builds and never needs an install to exist.

| Tier | Paths | Flag | Ignored by |
|---|---|---|---|
| Build trees | `build/` (all three configurations) and `cmake-build-*` at the repository root | `--build` | `.gitignore` `build/`, `cmake-build-*/` |
| Installed artifacts | every child of `out/`: `out/latest`, `out/debug`, and any release tag mirror | `--out` | `.gitignore` `out/` |
| Test scratch | `tmp/test/run-*` and `test/results/log` | `--tmp` | `.gitignore` `tmp/`, `test/.gitignore` `results/` |
| Source-tree pollution | the `.cmo` under `stdlib/` or `modules/`, and linker output under the directories a build reads, that `scripts/check-source-artifacts.mjs` reports (vendored Python SDKs excluded) | `--source` | none: they are untracked build output |

The default selects all four tiers: `npm run clean` is the same set as `npm run clean -- --all`.
A build tree, an install, a test scratch directory and a polluted source file are all ignored by
git and reproduced by the next build or test run, so the default deletes instead of defaulting to
`--dry-run`; the safety rules below are enforced, not advisory. The test scratch tier is a full
reset, not the ordinary way to keep the tree small: the runner keeps the newest 20 run directories
of `test/results/log` by itself and prints what that cost in its `[RETAIN]` line (docs/test-plans.md,
"Runner Output").

| Option | Effect |
|---|---|
| `--build` / `--out` / `--tmp` / `--source` | Select one tier; repeat to select several |
| `--all` | All four tiers, spelled out (the same set as the default) |
| `--dry-run` | Print `would remove <path>` for every path and the skips; delete nothing |
| `--root <dir>` | Clean that checkout instead of this one (used by the guard's `--clean`) |

There is no keep flag for `out/latest`: a caller who wants the install to survive asks for
`--build --tmp`, which is narrower and explicit.

A path is removed only when it resolves inside `--root` and belongs to a tier above; a symlink
whose target is outside is left alone. `.git` is never deleted: a directory candidate that
contains a `.git` entry is left in place and named. `stdlib/*.cml`, the tracked fixture
`test/cases/modules/pass_not_found/unloadable.cmo`, and any path `git ls-files` reports are never
deleted; a tracked path inside a tier is reported as `clean: skip <path>: tracked by git` and makes
the command exit 1. The delete is `fs.rmSync` after resolving and checking the path, never a shell
`rm -rf`.

`node scripts/check-source-artifacts.mjs --clean` delegates to `node scripts/clean.js --source`
(`--clean --dry-run` to `--source --dry-run`), so the H191 guard and `npm run clean` share one
implementation of the delete. The build's guard and `feat/tools/portability` still run the scan
without deleting; `feat/tools/clean` pins the tiers, the dry run and the tracked-file rule.

## Build Stamp (`out/<tag>/build-info.json`)

`npm run build` (and `debug` / `profile`) ends with `scripts/collect-out.js`, which writes
`out/<tag>/build-info.json`: the sources the install was produced from.

| Field | Content |
|---|---|
| `revision`, `dirty`, `dirtyCount`, `dirtyPaths` | `git rev-parse HEAD`, whether the tree had uncommitted changes, and up to 40 of those paths. `null` when git is unavailable: the stamp then says so, and the content fingerprint still guards the run. |
| `builtAt`, `config`, `tag` | When, which configuration, and which install directory (`latest`, `debug`, or a release mirror tag). |
| `fingerprint` | SHA-256 over the content of every path a build reads — `src/`, `include/`, `modules/`, `tools/`, `stdlib/`, `third_party/`, `cmake/`, `CMakeLists.txt`, `conanfile.txt` — with the file count and a per-file hash map (used to name the files that moved). |
| `schema`, `fingerprint.version` | The shape of the stamp, so a stamp written by an older build is refused instead of misread. |

The build writes it, no runner does, so a local build, a host build and a release tarball all carry
it; the PyPI rollup copies `out/<tag>/` whole and ships the file.

`node scripts/build-info.js check --tag latest` prints nothing when the stamp matches the current
tree, and exits 4 when it does not, naming both revisions (HEAD plus dirty state), both
fingerprints, and the source files that changed. `show` prints the stamp. `camel-remote.ps1` runs
`check` before a `-NoBuild` plan: an install is reused only while its fingerprint still matches, and
`-AllowStale` is the explicit override. Changes under `docs/`, `test/`, `scripts/`, `agents/`,
`editors/`, `pypi/`, `assets/`, `examples/` and `benchmarks/` do not move the fingerprint (a test
case is read at plan time); changes under the paths above do — including `stdlib/`, which
`collect-out` copies verbatim into `out/<tag>/stdlib`.

## Test Harness

The repository uses the plan-based runner under `test/`.

- runner: `node test/runner.mjs`
- executor: `node test/tools/run-tests.mjs`
- plans: `test/plans/**/*.plan.toml`
- cases: `test/cases/**`
- shared args templates: `test/vars.toml`
- plan field reference: `docs/test-plans.md`

Plans may use `verify_script = """..."""` for inline JavaScript assertions over
the Camel process result, or `verify_script_path = "relative/script.mjs"` for a
reusable external verifier. Full field constraints and examples live in
`docs/test-plans.md`.

## Main Test Scripts

| Script | Description |
|------|------|
| `npm run test` | Main developer set: the **fast** layer of `smoke` and `feat/**`, in parallel |
| `npm run test:all` | Full suite: every plan under `test/plans/`, fast and slow |
| `npm run release-gate` | Release evidence: fast layer + the runnable slow plans by file + the static checks |
| `npm run precommit:staged` | Format/update only files already staged for the current commit |

## The Release Gate

`npm run release-gate` (`scripts/release-gate.mjs`) owns the release evidence a release decision
needs, because `npm run test` is only the fast layer: a directory target drops every `tier =
"slow"` test, and `gpu/**` / `perf/**` are in no gate target at all.

It runs, in order:

1. the fast layer -- `smoke`, `feat/**`, and the fast-tier `gpu/profile` plan at `--tier fast`;
2. the slow layer -- eleven explicit plan files (the runnable set), named one by one:
   naming a file is what makes its slow tests run. It never takes a directory or `--tier all`;
3. the seven static checks: `stdlib-reference --verify-ops`, `--verify-doc`, `diagnostics-catalog
   --check`, `check-doc-links --all`, `check-doc-snippets`, `stdlib-declarations --check`,
   `check-std-includes`;

then exits non-zero when any phase failed. It does not build: build once first, and it checks the
install it finds (`CAMEL_HOME`, else `out/latest`) against that build's stamp — the rule that a
run must not measure another tree's binary — refusing (exit 2) when the stamp is missing or does
not match this tree's sources.

The two standard-library checkers among those static checks (`stdlib-reference.mjs --verify-ops`,
`stdlib-declarations.mjs --check`) apply the same rule themselves, before they read a registry: an
install that is not this tree's build is refused (exit 2, naming the install and its stamp) instead
of being reported as differences between this tree's declarations and another install's operators.
`--camel <path>` names the install such a run measures, passing the same check; the environment
switch `CAMEL_EXE` is gone.

It also never runs the eighteen plan files `agents/test.md` forbids on a shared host (GC stress
and poison, progen, the PyTorch comparisons, the benchmarks): it prints them by name with the
reason on every run, and refuses with a message naming the policy when asked for one --
`--tier all`, `--all`, `--suite`, or an `--include` / positional target that is, or contains, one
of them. A skipped plan is unknown, not green. The debug layer (`--tier debug`) is out of scope
for the same reason: it is never part of a Release gate.

`--jobs N` (default 8), `--only fast|slow|static` (a PARTIAL run, for re-checking one layer),
and `--dry-run` (print the commands and the skip list) are the only options.

The staged pre-commit script delegates file selection to `scripts/format.js` and
`scripts/update.js`; it only constrains auto-staging to the original staged path
set. It rejects partially staged files before re-adding paths, so unstaged hunks
are not pulled into the commit by the hook.

Use `npm run test -- <target>` for everything else instead of adding more fixed aliases.

## Direct Runner Examples

```powershell
node scripts/test.js
node scripts/test.js test/plans/feat/modules --tier all --jobs 8
node scripts/test.js test/plans/feat/linear/recursion_and_calls.plan.toml
node scripts/test.js --changed
node scripts/test.js --jobs 1 test/plans/smoke
npm run test -- perf
node test/tools/run-tests.mjs --update-golden test/plans/feat/parse/core.plan.toml
```

`--jobs N` sets the number of tests run at once (default: the processor count). `--tier
fast|slow|all` selects a layer, orthogonal to the target; the default is `fast` for a directory
target and for the whole plan tree, which is the `all` target (`all`, `--all` or `test/plans`)
and not `.`. A plan **file** named as the target runs in full whatever `--tier` says.
`--changed` selects the plans the working tree touches relative to `origin/develop` (override
with `CAMEL_TEST_BASE`): directory rules in `test/plan-selection.json` first, file rules second.

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
prefix when unambiguous, so `trans.macro` resolves to the same macro plan. A
bare `.` is the same rule with nothing left after the leading dots are stripped,
and the empty name resolves to `feat`: `.` is `test/plans/feat`, feat only, never
`smoke`. The whole plan tree is the `all` target (`all`, `--all` or `test/plans`).

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
