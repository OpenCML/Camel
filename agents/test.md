# Test Rules

Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first. Do not change semantics to make a test pass.

## Running tests

Do not build Camel on this workstation. Use the remote Linux build host through the repository's only gate entry:

```powershell
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan <plan-file-or-directory> -Jobs 8
```

The wrapper transports the current branch and uncommitted changes, builds, runs the selected plans and returns their exit code. Do not invoke another copy of the wrapper or a hand-written build command.

During development, run the plans touched by the change plus `smoke`. Before finishing, run `smoke` and `feat/**` on the host. A plan file named explicitly runs in full, regardless of the selected tier; a directory selects its `fast` layer by default.

Useful local, non-build checks:

```powershell
node scripts/check-doc-links.mjs --all
node scripts/check-std-includes.mjs
node scripts/conflict-markers.mjs
node scripts/diagnostics-catalog.mjs --check
```

## Test layers

- `fast`: `smoke` and `feat/**`, the ordinary acceptance layer.
- `slow`: explicit slow plans and slow entries, used when the change warrants them or before release.
- `milestone`: full regression, GC poison/stress, random programs, PyTorch comparisons and performance work. Run it on a quiet host at 09:00 Asia/Shanghai each day and before every release.
- `debug`: assertions that require a Debug build. Select it only with `--tier debug`; it is excluded from Release layers.
- `all`: every plan except the deliberate `debug` tier, not a synonym for fast.

Do not run full stress or performance suites to "be safe". Use the relevant named plan. See `docs/test-plans.md` for the authoritative selection rules.

## Evidence and provenance

- Record the test target, tier, total, pass, fail, skip and known-failure counts, and the tested commit. `skip` is not failure; an empty selection is not a green run.
- Every reading belongs to a tree and a build. Do not quote a SHA for a dirty tree; the runner's `[provenance]` line is the source of this information.
- Before treating a failure as new, check whether it exists on the base and whether another branch already fixed it. Do not diagnose from a truncated tail alone; read the full output named by the runner.
- `null` is absence of an exit code, not a timeout. A signal, timeout and failure to start are different outcomes.
- Keep logs and probe artifacts outside the worktree. Never kill a shared-host process by a broad program-name pattern.
- Build with Release for routine verification. Use Debug only for debug-only behavior; `GUARD` must remain active in Release, while `ASSERT` may be compiled out.

## Writing and changing tests

- Manually run a new feature or case first and inspect stdout/stderr. Only then encode the observed contract in a plan.
- Test behavior and invariants, not counters that move with unrelated edits. Use exact output where formatting matters, and properties where implementation may vary.
- Cover every affected backend, or require a located refusal on unsupported ones. A rule shared by several backends should have one shared case, not separate implementations.
- Put generated files and fixtures under `test/`; keep temporary probes outside it. Do not create test or report scaffolding when the project has no need for it.
- Changes to syntax or `test/cases/parse` require the AST snapshot update on the host. Changes to diagnostics require `diagnostics/registry.tsv` followed by `node scripts/diagnostics-catalog.mjs --write`.
- Do not weaken an assertion, delete a case or mark a failure merely to make the suite green. Explain every changed expectation in the change report.

## `known_fail`

A known failure is a precisely described open defect, not a budget or an exemption:

- Use `known_fail = ["backend"]` with a `backends` list, and `known_fail = true` without one.
- Include `known_fail_owner`, a concrete `known_fail_reason`, and `known_fail_msg` containing the expected diagnostic fragment.
- The runner rejects a marked case that starts passing, a missing expected message, or a failure whose reason differs from the pinned fragment. Remove a stale mark in the same commit that fixes the case.
- Real compiler internal errors on runnable programs are not marked known. They are ordinary failures requiring rework.

## Selection and generated checks

- `--changed` selects plans from the changed paths and their declared cases. An unreadable base or selection is an error, not a reason to run the whole fast layer. An empty selection means no tests ran; name the intended plans explicitly.
- `CAMEL_TEST_BASE` overrides the comparison base when necessary.
- A new plan needs a selection rule or a deliberate exclusion, so it cannot silently disappear from `--changed` and layer accounting.
- Regenerate rather than hand-edit generated indexes, diagnostics, standard-library references and snapshots. Checks that examine zero items fail rather than pass.
