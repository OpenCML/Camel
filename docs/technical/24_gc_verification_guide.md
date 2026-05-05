# GC Verification Guide

This document is the operator-facing verification guide for Camel's runtime GC work. The design
contract is documented in `23_gc_runtime_lifetime.md`; this file explains how to prove the contract
still holds after future changes.

## Test Organization

All GC/MM-specific automated test-plan entries live under:

```text
test/plans/feat/mm/
```

GC-specific `.cml` case files live under:

```text
test/cases/feat/mm/gc/
```

Do not add new GC-specific plan entries to `smoke`, `modules`, `linear`, or scheduler plans. Those
plans may still exercise GC indirectly because normal runtime execution allocates objects, but
explicit GC verifier, stress, remembered-set, snapshot, or foreign-resource tests belong in the
GC/MM plan and case directories above.

The current centralized plan file is:

```text
test/plans/feat/mm/gc.plan.toml
```

Run it with:

```powershell
node scripts\test.js test\plans\feat\mm
```

`npm run test` also runs this plan because the default developer set includes `smoke` plus all
`feat/**` plans.

## What `std::gc::*` Passes Are

Camel's command line executes named graph passes with:

```powershell
camel file.cml pass1 pass2 ...
```

The `std::gc::*` entries are runtime diagnostic/self-test passes, not optimization passes. They are
registered in the same pass registry so they can run in the normal CLI pipeline, after graph
materialization and before or between schedulers. This makes them useful for GC validation because
they execute inside the real runtime process with the real global allocators, root registry, graph
roots, frame roots, and module state.

Current GC diagnostic passes:

- `std::gc::verify`: runs heap verification and leaves the graph available for later passes.
- `std::gc::snapshot` / `std::gcsnap`: prints JSON memory and GC diagnostics, then consumes the
  graph.
- `std::gc::remembered_set`: runs a white-box old-to-young remembered-set self-test and leaves the
  graph available for later passes.
- `std::gc::foreign_resource`: runs a white-box foreign-resource and handle-lifetime self-test and
  leaves the graph available for later passes.

## Environment Controls

Use these controls for deterministic GC validation:

| Variable | Purpose |
|----------|---------|
| `CAMEL_GC_VERIFY=before|after|both|1` | Run heap verification around requested collections. |
| `CAMEL_GC_STRESS_ALLOC=N` | Request collection every `N` auto-space allocations. |
| `CAMEL_GC_STRESS_SAFEPOINT=N` | Request collection every `N` runtime safepoints. |
| `CAMEL_GC_STRESS_MODE=minor|major|both` | Select the requested stress collection kind. |
| `CAMEL_GC_LOG_MOVES=1` | Log object movement in the young-copying path. |
| `CAMEL_GC_ENABLE_YOUNG_COPYING=1` | Enable the experimental young-generation copying path. |

Production defaults keep young copying disabled. Tests that assert movement, remembered-set
behavior, or foreign wrapper relocation must opt in with `CAMEL_GC_ENABLE_YOUNG_COPYING=1`.

## Standard Verification Commands

Run the build first so `out/latest` is synchronized:

```powershell
npm run build
```

Run the centralized GC/MM plan:

```powershell
node scripts\test.js test\plans\feat\mm
```

Run the full default developer suite:

```powershell
npm run test
```

When manually invoking `camel.exe`, set the runtime environment first:

```powershell
$env:CAMEL_HOME = "D:\Projects\Camel\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

## Phase-by-Phase Checks

### Phase 1: Observability And Verification

Objective: prove that GC behavior is observable and heap invariants can be checked without changing
language-level cases.

Automated tests in `test/plans/feat/mm/gc.plan.toml`:

- `gc.verify_stress_nvm`
- `gc.verify_stress_fvm`
- `gc.verify_stress_prl`
- `gc.snapshot_json`

Manual examples:

```powershell
$env:CAMEL_GC_VERIFY = "both"
$env:CAMEL_GC_STRESS_ALLOC = "5"
$env:CAMEL_GC_STRESS_SAFEPOINT = "1"
$env:CAMEL_GC_STRESS_MODE = "major"
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_fib10.cml std::gc::verify std::nvm
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_fib10.cml std::gc::verify std::fvm
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_prl_scheduler.cml std::gc::verify std::prl
```

Expected evidence:

- Output contains `{"ok":true,"kind":"gc.verify"}`.
- Program output remains correct.
- `std::gcsnap` JSON has `regions`, `gc`, `summary`, root-source data, remembered-set size, and
  `foreignResources`.

### Phase 2: Safepoint-Only Moving-GC Foundation

Objective: prove that moving-capable collection is requested and serviced at explicit safepoints,
not inside arbitrary allocation paths, and that runtime roots survive stress.

Automated tests:

- `gc.verify_stress_nvm`
- `gc.verify_stress_fvm`
- `gc.verify_stress_prl`

Manual stress command:

```powershell
$env:CAMEL_GC_VERIFY = "both"
$env:CAMEL_GC_STRESS_ALLOC = "5"
$env:CAMEL_GC_STRESS_SAFEPOINT = "1"
$env:CAMEL_GC_STRESS_MODE = "major"
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_fib10.cml std::gc::verify std::nvm
```

Expected evidence:

- The same source program runs correctly under NVM and FVM with safepoint stress.
- The PRL scheduler case runs correctly under safepoint stress.
- Heap verification reports named root paths if a root is missed.

### Phase 3: Write Barriers And Remembered Set

Objective: prove that old-to-young references created after promotion are retained by the
remembered set and survive repeated minor copying.

Automated test:

- `gc.remembered_set_minor_copying`

Manual command:

```powershell
$env:CAMEL_GC_ENABLE_YOUNG_COPYING = "1"
$env:CAMEL_GC_VERIFY = "both"
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_fib10.cml std::gc::remembered_set std::nvm
```

Expected evidence:

- Output contains `{"ok":true,"kind":"gc.remembered_set"}`.
- The program still prints the expected Fibonacci result.
- Missing barriers fail through the remembered-set verifier with owner, slot, slot type, region, and
  young target information.

### Phase 4: Foreign Resources And Native Handles

Objective: prove that foreign/native resources have explicit lifetime control and remain correct
when wrappers move.

Automated test:

- `gc.foreign_resource_lifetime`

Manual command:

```powershell
$env:CAMEL_GC_ENABLE_YOUNG_COPYING = "1"
$env:CAMEL_GC_VERIFY = "both"
out\latest\bin\camel.exe test\cases\feat\mm\gc\gc_fib10.cml std::gc::foreign_resource std::nvm
```

Expected evidence:

- Output contains `{"ok":true,"kind":"gc.foreign_resource"}`.
- The self-test verifies rooted handle survival across GC, pinned address stability after promotion,
  idempotent disposal, access failure after disposal, finalization after disposal, and handle
  counters returning to zero.
- `std::gcsnap` exposes `foreignResources.createdControlBlocks`,
  `foreignResources.disposedResources`, `foreignResources.finalizedWrappers`,
  `foreignResources.liveControlBlocks`, `foreignResources.activeRootedHandles`, and
  `foreignResources.activePinnedHandles`.

The Python module also has a language-level FFI smoke case in
`test/cases/modules/std/python/basic.cml` that calls `py_dispose` twice. That case belongs to the
Python module plan because it verifies the public Python API, while the GC/MM lifetime assertions
remain centralized in `feat/mm`.

## Adding New GC Tests

Use this rule when extending GC coverage:

- Put plan entries in `test/plans/feat/mm`.
- Put GC-specific `.cml` cases in `test/cases/feat/mm/gc`.
- Reuse a small `.cml` program only when the test is intentionally not GC-specific.
- For GC internals that require direct allocator control, add a `std::gc::*` diagnostic pass or a
  C++ unit-style self-test invoked by that pass.
- Document the new verification path here and update `23_gc_runtime_lifetime.md` only when the
  runtime contract itself changes.
- Manually run the new command first and inspect stdout/stderr before adding `verify_script` or
  plan assertions.

## Failure Triage

- Verifier failures before collection usually indicate a bad existing root, stale pointer, invalid
  header, or broken allocator metadata.
- Verifier failures after collection usually indicate missed root relocation, missed write barrier,
  stale remembered-set entry, or finalizer/reclamation ordering problems.
- A remembered-set self-test failure usually points to a container write path that bypassed typed
  setters or forgot to record the actual old owner.
- A foreign-resource self-test failure usually points to incorrect dispose idempotency, wrapper
  finalization, rooted handle tracing, or an unsafe pinned-handle assumption.
