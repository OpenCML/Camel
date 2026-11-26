# Promotion 21 — the eight new reds, sorted by what they are

Reading: `[SUMMARY] total: 9753, pass: 9161, fail: 531, skip: 28, known failures: 33`,
`RUN_EXIT=1`, run `20261127_043925_54660_xy5d`, tree23, tip `065bfb16902b313201544a3793a4f52f2a31c52d`.
`[load] 68.18 74.35 73.94`, `[TOKEN] queued 5186, tokenWaitSeconds=1034.4`, `[SPLIT] build 14.9 s, test 620.5 s, total 635.4 s`.
Durations are not comparable this round (ten checkouts overlapped); only names and colours are read.

Against the promotion-20 red set — the 549 `[FAIL]` lines of the run that measured `28f74cab05`
(`20261127_031412_72348_udjm`; the record of that promotion keeps no separate table file, its reds are its own
`[FAIL]` lines):

| | count | arithmetic |
|---|---|---|
| turned green | 26 | |
| still red | 523 | |
| new | 8 | 523 + 8 = 531 = `fail` |

The count difference from the baseline run (550 -> 531 = 19) is **not** the number of new reds: the suite grew
from 9748 to 9753 entries (+5) and 26 reds turned green, 8 appeared. Twenty-six minus eight is not a
difference of entries, which is why the two numbers cannot be reconciled without names.

## The eight, by class

**Behaviour (4).** These are the ones the gate is about.

| red | first line | attributed to |
|---|---|---|
| `modules.rename_identity/rename.env_graph_identity` | `the graph changed beyond the module name: normalized sha256 019e3cb80d…` | `7c2411a50e` (lane/console-rescue, `c65c0bf6b6`) |
| `modules.rename_identity/rename.process_graph_identity` | `the graph changed beyond the module name: normalized sha256 75712dab0b…` | same |
| `tutorial.examples/tutorial.examples.10_uses_beyond` | `SemanticDiag::UsesPromiseViolated 0x030001F1: … does not keep its resource promise: console` | same |
| `modules.stdlib_registry/registry.no_env_switches` | `src/kernel/core/mm/block.cpp:54: reads 'CAMEL_MM_BLOCK_TRACE' … no ledger line allows it` | `5d9bba938b` (lane/backend-speed, `c68e90f784`) |

The two `rename_identity` tests compare a **recorded digest** of the `std::jgir` document (the file's own header
lists five earlier re-recordings); a graph change shifts it. Whether they are "a record that expired" or "a
regression" is the owner's call, and the record is what says which — that is why they are listed under
behaviour rather than under generated records: the test exists to notice exactly this.

**Dependent (1).** `mm.gc_poison_sweep/poison:modules.rename_identity:rename.env_graph_identity` is a sweep copy of
the first behaviour red above; it goes green with it and needs no separate work.

**Generated record (2).**

- `modules.stdlib_registry/registry.reference_is_generated`: `docs/stdlib-reference.md is out of date` — the
  console change altered the resource declarations the document is generated from.
- `docs.headers/docs.headers.canonical`: `test/tools/header-weight.mjs: the header is not followed by a blank line
  (line 4 is "//")` — introduced by the tip's own merge `065bfb1690` (`e6ad653b20`), a one-line fix.

**Instrument (1).** `probe.resnet_block_space/resnet_block_space` reported
`BLOCKSPACE train no limit in the ladder completed`: the block space a resnet batch needs changed, which is what
the probe is for. A measurement in the fast layer is a measurement in the wrong place; it moves to the debug tier.

## Verdict

**Behaviour regressions are not zero (4, of which one is a sweep copy), so `develop` does not move.**
The two-hour clock on `7c2411a50e` and `5d9bba938b` runs from 04:32 to 06:32; whichever is still red then is
reverted with `git revert -m 1` as a new commit, never a force-push.