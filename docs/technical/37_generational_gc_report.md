# The Generational Collector as an Option: Report

Run-time values can be reclaimed by one of two collectors, chosen per process: `mark-sweep`
(the default, objects keep their address) and `generational` (`--gc generational` or
`CAMEL_GC=generational`: young objects are copied). This note records what had to be fixed
before the generational collector could be offered, what was tested, what was measured against
mark-sweep, what it costs, and what is known to be wrong. Its last section is for the language
author: whether the generational collector should become the default is his decision.

How to use the switch and its settings is in `docs/cli.md` ("The collector"); what C++ code
must observe is in `36_gc_handle_rules.md`; the allocation domains are described in
`23_gc_runtime_lifetime.md`, the verification modes in `24_gc_verification_guide.md`.

Everything in sections 3 to 6 was run for this report on one machine: Linux x86-64, 18
hardware threads, release build, in a container that other builds and test runs shared. **No
other platform was built or run.** The load of the machine is given with every measurement;
it was never below 11 runnable tasks on 18 threads, so the times are those of a busy machine
and only the comparison between the two collectors within one row means anything.

## 1. Summary

- Every test of the default set (3,754 tests, 10 skipped) passes with each collector; in
  single runs on the loaded machine three tests failed for reasons that do not depend on the
  collector (section 4.1). The default set runs 1,221 tests of the other plans a second time
  under safepoint poisoning, and they pass.
- Programs that make many short-lived small values run in 0.2 to 0.8 of the time and a fifth
  to two thirds of the memory with the generational collector on `std::nvm`, `std::fvm` and
  `std::jit`; the gain is smaller on `std::prl` (0.35 to 0.95 of the time).
- Programs that build a large structure and keep it are slower with it: 1.1 to 1.45 times the
  time, the same memory.
- Tensor programs and programs that do not allocate show no difference outside the noise.
- With the generational collector `std::jit` does not compile a graph that holds an object
  constant. Of the 93 graphs it compiles in the test programs examined, 9 are lost; a hot
  function that is lost runs at interpreter speed (21 times slower in the worst case built
  for this report).
- Two defects that exist with both collectors were met while measuring and are not fixed: a
  reference held in a slot typed `any` is not seen by the collector, and allocation in the
  mark-sweep collector can stall after a sweep (both in section 6.4). The first is
  far easier to hit with the generational collector, the second does not occur with it.

## 2. What the option is

Read from `src/core/mm/mm.cpp` and `src/core/mm/gen.cpp`:

| | mark-sweep | generational |
|---|---|---|
| Small objects | free list in the elder space | bump allocation in a 4 MB birth space (`CAMEL_GC_NURSERY_MB`) |
| Large objects (own block, never move) | above 4 KB | above 64 KB, at most an eighth of the birth space (`CAMEL_GC_LARGE_THRESHOLD_KB`) |
| Minor collection | none | when the birth space is 90 % full: survivors are copied to a survivor space (a quarter of the birth space), to the elder space after 4 survivals (`CAMEL_GC_PROMOTION_AGE`) or when the survivor space is full |
| Major collection | mark and sweep of the elder and large-object spaces | the same, preceded by a minor collection |
| Elder space | segments of 32 MB, doubling up to 256 MB per segment, no limit of its own | the same |
| Heap limit | `CAMEL_GC_HEAP_LIMIT_MB` over the elder and large-object spaces; none by default | the same |

Collections happen only at safepoints. The collector is selected before the first allocation
(`mm::setCollectorMode`, `tools/camel-cli/config.cpp`); `CAMEL_GC_POISON=1` selects the
generational collector as well.

No table or stack in the collector's sources (`src/core/mm`, `include/camel/core/mm`) is sized
by the recursion limit; a `RootHandle` is a node of an intrusive list. The pending change
of the default limit on calls that are not tail calls from 256 to 1000 (design principles,
rulings of 2026-10-12) therefore needs no change here. Checked by running a recursion 990
calls deep that allocates at every level with `--max-recursion-depth 1000` on `std::nvm`,
`std::fvm`, `std::jit` and `std::prl` with both collectors (300 rounds, same result
everywhere) and with `CAMEL_GC_POISON=1 CAMEL_GC_VERIFY=both` on the first, second and fourth
(2 rounds). The only `256` in the collector is the cap on the size of one elder segment.

## 3. What was fixed

The copying path existed before this work behind `CAMEL_GC_ENABLE_YOUNG_COPYING=1` and was
not sound. The defects, in the order of the commits (`git log origin/fsh-dev..lane/gen-gc`):

| Commit | Defect and cause |
|---|---|
| `fix(gc): report a stale reference in the heap verifier instead of walking into it` | The verifier followed a reference into an evacuated young space and crashed there instead of reporting the path to it. |
| `fix(gc): do not copy an object twice in one minor collection` | The static area of a graph is a root and is reached again through every active frame of the graph. A slot visited twice had its already copied object copied again; the first copy stayed behind with a forwarding pointer into the space that the next collection evacuated, so roots ended up pointing at freed memory. Early promotion had hidden it. |
| `fix(macro): apply the rewrites of a round after its last evaluation` | A graph draft holds plain copies of static slots and of evaluated values. The macro pass opened drafts while later candidates of the same round were still being evaluated, and evaluation reaches safepoints, so the copies went stale and the committed graph carried constants that pointed at moved objects. Now all candidates are evaluated, then rewritten, then committed. |
| `fix(gc): make the moving collector sound for objects born old and let the elder space grow` | Four defects. (a) Promotion threw `std::bad_alloc` when the fixed elder space was full; the space now grows by segments. (b) An object born old (large, or small while the birth space was full) is filled by its creator with plain stores and could point at young objects without being in the remembered set; such objects are now tracked until the next minor collection and scanned by it. (c) The static area of a graph reached only through a function value was not traced by a minor collection. (d) A moved object without a static layout (function values, module object types) was not scanned. The same commit makes the elder space ask for a major collection by volume, with both collectors. |
| `fix(jit): compile only reference-free graphs when the collector moves objects`, narrowed by `fix(jit): refuse object constants, not object slots, and forbid safepoints under native frames` | Compiled code has no stack maps and bakes a static slot it reads into the code as an immediate: for an object constant that is the address the object had at compile time. A graph whose static area holds a reference is not compiled when objects move (`describeFramelessSelfFuncFailure` in `x64_backend.cpp`). Frames of compiled code may hold references because nothing collects while one exists; the allocator now refuses a safepoint reached under a native frame, so that a later lowering which calls out cannot break this silently. |
| `fix(gc): run foreign releases outside the collector, visit only live young objects, add NoCollectionScope` | The deleter of a dead foreign wrapper ran inside the collection with the allocator locked: one that allocates deadlocked, and the Python one takes the GIL, which a Python operator holds while it allocates. Releases are queued and run after the safepoint has unlocked (both collectors). A minor collection walked every dead young object to call a finalizer that is almost always empty; finalizable objects now register themselves. `onnx.export_model` runs passes while it holds raw slots and uses them as table keys; it runs under `mm::NoCollectionScope`. |
| `perf(gc): constant-time RootHandle, lock-free write barrier fast path; fix a verifier self-deadlock` | The remembered-set check of the verifier asked the large-object space a question while holding that space's lock, which hung every verified run with typed large objects. |
| `feat(gc): 64 KB large-object threshold for the generational collector; nursery and threshold settings` | Not a soundness defect: with the 4 KB threshold an array of a few thousand elements was born old and, dying young, kept every young element it pointed at alive until the next major collection, so a map/filter pipeline promoted nearly its whole birth space at every minor collection. |
| `fix(gc): enforce the heap limit when promotion exceeds it and say so in the help text` | Promotion could pass `CAMEL_GC_HEAP_LIMIT_MB` without the program being stopped. |
| `fix(gc): do not trace a released graph reached through a dead function value` | A minor collection also follows what dead, not yet swept old objects refer to. A dead function value can name a graph that a rewrite has replaced and released; tracing it read freed graph memory. A graph is traced only while the graph space still holds its block. |

The audit of the C++ sources against the handle rules (about 480 sites, 24 findings) is in
`36_gc_handle_rules.md` section 7; it was not repeated for this report.

One test was adjusted while closing this work: `attempt.compiled.jit_log` asserts the reason
`std::jit` logs for not compiling the graphs that hold the `attempt` node ("contains OPER").
With the generational collector those graphs are refused one check earlier, for their object
constants; `quot`, the function the test is about, is compiled in both modes. The test now
names mark-sweep, as `gc_jit_constants.mark_sweep` does. It was not a defect.

## 4. Test results

### 4.1 The default test set

`node scripts/test.js` (the `smoke` and `feat` plans), once with no collector setting and once
with `CAMEL_GC=generational` in the environment, the two runs side by side on the loaded
machine. The pair was run twice: before this file existed, and on the final state of the
branch.

| Run | Tests | Passed | Skipped | Failed |
|---|---|---|---|---|
| first pair, no setting (mark-sweep) | 3,754 | 3,743 | 10 | 1: `docs.links.reader_facing` |
| first pair, `CAMEL_GC=generational` | 3,754 | 3,742 | 10 | 2: `docs.links.reader_facing`, `gc_pacing.growth_25_prl` |
| final pair, no setting (mark-sweep) | 3,754 | 3,743 | 10 | 1: `unordered_foreach_smoke.nvm`, by its time limit |
| final pair, `CAMEL_GC=generational` | 3,754 | 3,744 | 10 | 0 |

`docs.links.reader_facing` failed because two documents link to this file. The ten skipped
tests are the `grammar.scopes.*` tests of the editor grammar. The other two failures come
from the load of the machine, not from a collector:

- `unordered_foreach_smoke.nvm` has a time limit of 4 s and was stopped by it while the load
  was 41 on 18 threads. Its plan directory, run three times afterwards with the default
  collector at a load of 13, passed each time (31 tests; this test in 1.1 to 1.2 s).
- `gc_pacing.growth_25_prl`: see below. Its plan, run again in both modes, passed.

So every test has passed with each collector, but the run with the default collector was not
free of failures in a single pass on this machine.

`gc_pacing.growth_25_prl` is a test of the main branch that expects exactly 16 major
collections of a program under `std::prl`. Under that scheduler a collection that has been
asked for runs when the workers reach the gate; on a loaded machine the statistics show more
requests deferred and one collection fewer. Repeated 30 times with each collector at a load of 35: 16
collections in 27 runs with mark-sweep and in 29 with the generational collector, 15 in the
others (the run above saw 14). It depends on the load, not on the collector, and was left as
it is; an assertion on a range would remove it.

The environment variable does not reach every test: tests that state their own collector
(`CAMEL_GC=...`, `CAMEL_GC_ENABLE_YOUNG_COPYING=...` in `envs`) keep it. Those are the tests of
`mm.gc`, `mm.gc_soak`, `mm.gc_elder_exhaustion`, the white-box self-tests, and the two JIT
tests named above.

### 4.2 Safepoint poisoning

With `CAMEL_GC_POISON=1` every safepoint moves every young object and overwrites where it was
with the address of an unreadable page, so a C++ pointer kept across a safepoint faults with a
report. The plan `mm.gc_poison_sweep` of the default set runs the tests of the `smoke`,
`modules` (without `autodiff` and `onnx`), `compile`, `linear`, `para` and `trans` plans and
of five `consistency` plans again in this mode with all their assertions: **1,221 tests, all
passed** in all four runs of 4.1.

Not covered by the sweep: the model tests of `autodiff` and `onnx`, the `opt` plans, tests
that choose their own collector settings, and tests marked `sweep = false` (wall-clock
assertions, assertions on when a foreign resource is released or on what the JIT compiles).
The whole test set was not run under poisoning for this report.

### 4.3 Heap verification under forced collections

The `smoke`, `linear` and `para` plans (517 tests) with a minor collection forced at every
safepoint and the heap verified before and after each, time limits multiplied by 20:

| Setting | Passed | Failed |
|---|---|---|
| `CAMEL_GC=generational CAMEL_GC_VERIFY=both CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=minor` | 515 | 2, both by time limit: `perf_helper_smoke.jit` (80 s), `recursion_depth_hot.fvm` (160 s) |
| the same with `CAMEL_GC_PROMOTION_AGE=1` (everything is promoted at once: shows a missing write barrier) | 514 | 3, all by time limit: `var_params.nvm`, `var_params.fvm` (200 s), `perf_helper_smoke.jit` (80 s) |

No run reported a verification failure. The tests that ran out of time are loops of tens of
thousands of calls, each of which verifies the whole heap twice. `var_params`, run alone on
`std::fvm` under the second setting without a time limit, finished after 133 s with the
expected output; the other two were not run to the end.

### 4.4 Soak and scale

Part of the default set (`mm.gc_soak`, 22 tests): a steady live set
under continuous allocation on three back ends, whose old spaces must not depend on the length
of the run; tens of megabytes of live small objects; a parallel map with one worker asleep in
an operator; programs that exceed a heap limit and must end with `MemoryExhausted`. Passed in
all four runs.

Not part of the default set (`test/plans/perf/gc.plan.toml`, 4 tests, passed). The numbers
are of one run each with `CAMEL_GC_STATS=1`, `std::fvm`, load 17 to 28:

| Program | | mark-sweep | generational |
|---|---|---|---|
| `gc_soak`, 2,000,000 steps: 13.2 GB allocated around a live table of 20,000 rows | wall time | 267 s | 128 s |
| | peak resident size | 125 MB | 24 MB |
| | collections | 412 major | 5,236 minor, no major |
| | time stopped (longest stop) | 105 s (792 ms) | 0.43 s (18 ms) |
| | elder space reserved | 64 MB | 32 MB |
| | objects promoted | | 20,372 |
| `gc_live_large`, 1.5 GB of small objects kept alive | wall time | 10.6 s | 15.4 s |
| | peak resident size | 1,746 MB | 1,728 MB |
| | collections | 9 major | 500 minor, 12 major |
| | time stopped (longest stop) | 4.7 s (1.80 s) | 11.4 s (1.56 s) |

The wall times of the soak varied with the load by more than a factor of two between runs
(the same two tests took 238 s and 43 s inside the test runner a few minutes earlier); the
counts do not vary.

## 5. Measurements

### 5.1 Method

`python3 test/tools/gcbench/gcbench.py --rounds 7` runs each benchmark seven times with each
collector, alternating between the two so that both see the same drift of the machine. A row
gives the **median** wall time of the seven runs of the whole process (start-up and
compilation included) with the fastest and slowest in brackets, the median peak resident size
(`ru_maxrss` of the child, the number `/usr/bin/time -v` reports; the container has no
`/usr/bin/time`), the median number of collections and of milliseconds stopped and the longest
single stop of all runs, as the allocator reports them with `CAMEL_GC_STATS=1`, and the
one-minute load average before and after the benchmark. "ratio" is generational over
mark-sweep on the medians. A minor collection that precedes a major one is counted in the stop
of the major one.

`std::jit` is `std::inline std::jit`; the tensor programs run after `std::macro`. `std::prl`
uses all 18 threads, so the load after a `prl` row is in part its own. Rows marked (9) were
measured a second time with nine rounds at a lower load, and that measurement is shown.

### 5.2 Short-lived small values

`alloc_small`: 1,000,000 steps, each making an array, a tuple, a struct, a string and a
closure and dropping them. `map_chain`: 300 chains of `map`/`filter`/`reduce` over 2,000 boxed
values. `soak`: 20,000 steps of allocation around a live table of 20,000 rows.

| Benchmark | Back end | mark-sweep s | generational s | ratio | RSS MB | Collections (minor/major) | Stopped ms (longest) | Load |
|---|---|---|---|---|---|---|---|---|
| alloc_small | nvm | 3.32 [2.47, 4.45] | 2.13 [1.72, 2.52] | 0.64 | 111 / 18 | 0/8, 95/0 | 790 (219), 2.1 (0.6) | 15 / 16 |
| | fvm | 1.85 [1.70, 2.46] | 0.94 [0.84, 1.34] | 0.51 | 111 / 18 | 0/8, 95/0 | 703 (162), 2.3 (0.3) | 16 / 17 |
| | jit | 1.64 [1.61, 1.69] | 0.78 [0.76, 0.85] | 0.47 | 111 / 18 | 0/8, 95/0 | 659 (103), 2.5 (0.2) | 17 / 15 |
| | prl | 4.93 [4.88, 8.00] | 4.08 [3.89, 6.99] | 0.83 | 111 / 18 | 0/8, 95/0 | 673 (212), 2.4 (0.4) | 15 / 13 |
| map_chain | nvm | 0.69 [0.64, 0.77] | 0.53 [0.48, 0.55] | 0.77 | 43 / 18 | 0/6, 36/0 | 142 (36), 7.2 (1.1) | 13 / 13 |
| | fvm | 0.54 [0.48, 0.56] | 0.33 [0.31, 0.38] | 0.61 | 43 / 18 | 0/6, 33/0 | 148 (37), 0.8 (0.2) | 13 / 12 |
| | jit | 0.50 [0.49, 0.55] | 0.31 [0.28, 0.33] | 0.62 | 44 / 19 | 0/6, 33/0 | 150 (34), 11 (1.2) | 12 / 12 |
| | prl | 6.91 [6.61, 7.29] | 6.56 [5.77, 7.58] | 0.95 | 68 / 28 | 0/6, 36/0 | 141 (31), 15 (2.6) | 12 / 22 |
| soak | nvm | 1.42 [1.29, 2.04] | 0.70 [0.64, 0.74] | 0.49 | 124 / 24 | 0/3, 49/0 | 356 (223), 10 (8.4) | 11 / 12 |
| | fvm | 1.24 [1.04, 1.28] | 0.48 [0.39, 0.56] | 0.38 | 125 / 24 | 0/3, 49/0 | 399 (211), 11 (6.9) | 12 / 12 |
| | jit | 1.15 [0.93, 1.34] | 0.40 [0.37, 0.50] | 0.35 | 125 / 24 | 0/3, 49/0 | 420 (197), 11 (6.0) | 12 / 12 |
| | prl | 13.77 [9.52, 13.97] | 9.06 [7.57, 10.78] | 0.66 | 150 / 30 | 0/3, 49/0 | 340 (212), 10 (7.4) | 12 / 26 |

In the RSS, collections and stopped columns the first value is mark-sweep, the second
generational.

### 5.3 A structure built by recursion, used once and dropped

`build_tree`: a nested array with 8,192 leaves built by recursive `concat`, 40 times, each
dying as the next is built.

| Back end | mark-sweep s | generational s | ratio | RSS MB | Collections (minor/major) | Stopped ms (longest) | Load |
|---|---|---|---|---|---|---|---|
| nvm | 3.15 [3.03, 3.50] | 0.66 [0.65, 0.72] | 0.21 | 92 / 66 | 0/3, 40/1 | 130 (55), 157 (53) | 22 / 17 |
| fvm | 1.16 [1.10, 1.51] | 0.35 [0.28, 0.44] | 0.30 | 92 / 66 | 0/3, 40/1 | 109 (58), 121 (47) | 17 / 16 |
| jit | 1.04 [1.00, 1.11] | 0.26 [0.25, 0.31] | 0.25 | 92 / 66 | 0/3, 41/2 | 103 (52), 101 (44) | 16 / 14 |
| prl | 3.60 [3.17, 4.18] | 1.26 [1.16, 1.95] | 0.35 | 122 / 78 | 0/3, 39/2 | 114 (47), 156 (48) | 14 / 11 |

The time stopped is about the same with both collectors, so the difference is in allocation,
not in collection. Where exactly mark-sweep spends it was not profiled.

### 5.4 Long-lived data

`live_table`: a table of 56,000 rows built and read 8 times. `live_large`: 256 MB (by the
program's count; 450 MB resident) of small objects built and kept, read 40 times. The 1.5 GB
variant is in section 4.4.

| Benchmark | mark-sweep s | generational s | ratio | RSS MB | Collections (minor/major) | Stopped ms (longest) | Load |
|---|---|---|---|---|---|---|---|
| live_table, fvm | 0.39 [0.36, 0.46] | 0.42 [0.37, 0.51] | 1.09 | 74 / 80 | 0/1, 7/2 | 50 (63), 149 (72) | 15 / 14 |
| live_large 256 MB, fvm (9) | 1.54 [1.34, 1.92] | 2.04 [1.84, 2.31] | 1.33 | 451 / 448 | 0/5, 131/8 | 538 (231), 1473 (236) | 12 / 13 |
| live_large 1.5 GB, fvm (one run) | 10.6 | 15.4 | 1.45 | 1,746 / 1,728 | 0/9, 500/12 | 4,678 (1,804), 11,449 (1,560) | 19 / 17 |

Every object that is kept is copied into a survivor space and then into the elder space, and
the minor collections that do it are counted as stops. Only `std::fvm` was measured in this
class.

### 5.5 Tensor programs

The training workload of `benchmarks/lenet/model.cml` and `benchmarks/mlp/model.cml`
(`CAMEL_BENCH_WORKLOAD=train`: loss, gradient and SGD update per step, 150 and 300 timed
steps, 4 kernel threads). Tensor data are large objects with both collectors.

| Benchmark | Back end | mark-sweep s | generational s | ratio | RSS MB | Collections (minor/major) | Stopped ms (longest) | Load |
|---|---|---|---|---|---|---|---|---|
| train_lenet (9) | nvm | 2.16 [1.83, 3.64] | 2.20 [1.73, 4.27] | 1.02 | 34 / 35 | 0/153, 152/152 | 13 (2.1), 20 (8.2) | 13 / 15 |
| | fvm | 4.02 [2.11, 5.42] | 3.22 [1.99, 5.41] | 0.80 | 35 / 35 | 0/153, 152/152 | 15 (13), 24 (4.2) | 15 / 18 |
| | jit (`std::macro std::jit`) | 2.89 [2.60, 3.10] | 2.92 [2.57, 3.79] | 1.01 | 35 / 35 | 0/153, 152/152 | 15 (1.9), 23 (5.0) | 18 / 16 |
| | prl | 2.08 [1.74, 3.28] | 2.00 [1.78, 3.31] | 0.96 | 35 / 35 | 0/153, 152/152 | 13 (1.8), 21 (6.6) | 16 / 15 |
| train_mlp | nvm | 0.77 [0.66, 0.81] | 0.78 [0.68, 0.90] | 1.01 | 32 / 32 | 0/151, 151/151 | 11 (0.5), 16 (1.3) | 14 / 14 |
| | fvm (9) | 0.72 [0.69, 1.12] | 0.77 [0.69, 1.34] | 1.08 | 32 / 32 | 0/151, 151/151 | 11 (1.5), 15 (2.7) | 15 / 14 |

The spread within a row (the kernels compete with the other work on the machine for their
four threads) is several times the difference between the collectors: **no difference was
measured.** What the statistics do show is that the generational collector runs a minor
collection before each of the major collections that the large-object budget asks for and is
stopped about half as long again in total (20 against 13 ms over 150 steps).

### 5.6 No allocation

`fib` (recursive Fibonacci) allocates nothing; the rows show what the presence of either
collector costs.

| Benchmark | mark-sweep s | generational s | ratio | RSS MB | Load |
|---|---|---|---|---|---|
| fib(32), nvm | 1.81 [1.67, 2.23] | 1.94 [1.59, 2.18] | 1.07 | 13 / 13 | 18 / 16 |
| fib(32), fvm | 0.43 [0.41, 0.72] | 0.40 [0.36, 0.57] | 0.92 | 13 / 13 | 16 / 16 |
| fib(30), prl | 2.45 [2.14, 3.20] | 2.41 [2.04, 2.67] | 0.98 | 13 / 13 | 16 / 15 |
| fib(40), jit | 0.72 [0.67, 0.80] | 0.73 [0.67, 0.92] | 1.02 | 13 / 13 | 15 / 16 |
| block_sort_merge, prl | 0.26 [0.24, 0.27] | 0.24 [0.23, 0.27] | 0.94 | 106 / 106 | 14 / 14 |
| concurrent_alloc, prl | 0.057 [0.049, 0.064] | 0.055 [0.045, 0.081] | 0.96 | 15 / 15 | 14 / 14 |

All within the spread. The last two are the parallel test programs; they run for too short a
time to say more than that nothing is wrong.

## 6. Costs and limits

### 6.1 `std::jit` compiles fewer graphs

With the generational collector a graph whose static area holds an object constant (a string
literal, a constant array) is left to the interpreter; the log says "the
graph has object constants (moving collector)". Frames of compiled code may hold references.

Counted over the 243 programs under `test/cases/linear`, `consistency`, `perf`, `para` and
`examples`, each run with `-vv` under both collectors (153 of them run to exit code 0 without
arguments; the others need arguments or are meant to fail):

| Passes | Programs with a compiled graph (mark-sweep / generational) | Graphs compiled | Graphs lost | Programs that lose one |
|---|---|---|---|---|
| `std::jit` | 38 / 36 | 93 / 84 | 9 | 4: `linear/nvm/lambda.cml`, `linear/nvm/render.cml`, `consistency/values/match_values.cml`, `para/core/unordered_foreach_smoke.cml` |
| `std::inline std::jit` | 26 / 26 | 38 / 38 | 0 | 0 |

The new check refuses 71 (14 after inlining) graphs in all, but all except the 9 are graphs
that the compiler refuses for another reason with mark-sweep too (an operator call, a call of
another graph). None of the four programs is a benchmark, and the tensor benchmark of 5.5
shows no difference under `std::jit`.

What a lost graph costs is the difference between compiled code and the interpreter for that
graph. `test/cases/perf/gc/jit_const.cml` is the worst case: a hot doubly recursive function
that returns a string constant.

| Benchmark | mark-sweep s | generational s | ratio | Load |
|---|---|---|---|---|
| jit_const(36), `std::inline std::jit` | 0.100 [0.096, 0.116] | 2.09 [2.02, 2.26] | 21 | 17 / 17 |
| jit_const(36), `std::inline std::fvm` | 2.50 [2.10, 4.75] | 2.53 [1.97, 3.09] | 1.01 | 16 / 17 |

Removing the restriction needs either stack maps or constants loaded through a table that the
collector updates; neither was attempted.

### 6.2 What C++ authors must observe

`36_gc_handle_rules.md` is the contract. In short: an operator may use raw pointers to objects
freely until it reaches a safepoint (a call into Camel code, the macro evaluator, a pass
boundary, an explicit `safepoint()`); across one it must hold the object in a `RootHandle` and
read every pointer again afterwards; a store of a reference into an object that already
existed before the call needs the write barrier; foreign memory is owned by a wrapper object
and never points into the movable heap; nothing static keeps a reference. Under mark-sweep a
violation goes unnoticed until the object is freed; under the generational collector it reads
moved memory. The modules in this repository were audited and are covered by the poison sweep;
a module outside it has been checked by nobody.

### 6.3 Sizes

- An object above 64 KB is born old with the generational collector. A temporary of that size
  (an array of more than about 8,000 elements) is reclaimed only by a major collection, and
  while it lives it keeps what it points at alive across minor collections.
- The birth space is 4 MB and the survivor spaces 1 MB each; a program that keeps more than
  the survivor space holds per minor collection has the rest promoted at once.
- The resident size of a program that does not allocate is the same with both collectors
  (13 MB).

### 6.4 Known problems, not fixed

**A reference in a slot typed `any` is invisible to the collector (both collectors).** Recorded
as an open question in `36_gc_handle_rules.md` section 9; reproduced for this report:

```camel
module anyref
import { decode } from json
func churn(n: int): int { return len(range(0, n)->map<(i: int): string => 'item-' + string(i)>) }
func main(): int sync {
    let d = decode('["alpha", "beta", "gamma"]')
    let c = churn(200000)
    let xs = d as string[]
    println(c, xs)
    return 0
}
```

| Setting | `std::nvm` | `std::fvm` |
|---|---|---|
| mark-sweep | `200000 [alpha, beta, gamma]` (no collection ran) | the same |
| generational | **`200000 157292`, exit code 0** (3 minor collections) | `200000 [alpha, beta, gamma]` (1 minor collection) |
| mark-sweep, `CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=major` | not run | the process dumps core |
| `CAMEL_GC_POISON=1` | not run | stopped with the poison report |

The defect is the same with both collectors, but mark-sweep does not collect small objects
before 32 MB of them have been allocated, and the generational collector collects every 4 MB:
with it this program
prints a wrong value and reports success. The full test set passes with the generational
collector, so no test holds an `any` value across enough allocation; programs outside the
test set that keep the result of `json.decode` in a variable were not surveyed.

**Allocation with mark-sweep can stall after a sweep (mark-sweep only).** Met while trying
the program above with a larger count:

```camel
module noany
func churn(n: int): int { return len(range(0, n)->map<(i: int): string => 'item-' + string(i)>) }
func main(): int sync {
    println(churn(1000000))
    return 0
}
```

| `n`, `std::nvm` | mark-sweep | generational |
|---|---|---|
| 500,000 | 1.0 s | 1.3 s |
| 600,000 | 0.8 s | not run |
| 700,000 | stopped by the time limit of 60 s | not run |
| 1,000,000 | stopped by the time limit of 200 s | 1.2 s |

With `CAMEL_GC_STATS=2` the program stalls right after its second major collection (cause
"elder space budget"); interrupted under a debugger it is in `FreeListAllocator::alloc`,
called from the string concatenation. The free list of an elder segment is searched first-fit
from its head (`include/camel/core/mm/alloc/free_list.h`); after a sweep that frees one small
block between every two live ones, every later allocation of a larger block walks past all of
them. With `std::fvm` the same program finishes (2,000,000 elements, one major collection).
The first-fit search is the same on the main branch; whether the main branch stalls on this
program was not run, and the pacing that asks for this collection ("elder space budget") was
added by this work, so it may be reached earlier here than there. Not fixed: it needs size
classes or a next-fit cursor in the free-list allocator, which is a change to the default
collector. The generational collector does not go through this path for young objects.

**`gc_pacing.growth_25_prl` depends on the load** (section 4.1), with both collectors.

**Left running by the previous work:** a run of the whole test set under `CAMEL_GC_POISON=1` on
a snapshot taken before the rebase. Its results were not used here.

### 6.5 Not measured, not verified

- Any platform other than Linux x86-64. The poison mode's fault handler is compiled only
  where `_WIN32` is not defined.
- `std::tf` on its own (it shares the scheduler with `std::prl`), the C++ exporter, GPU
  kernels, Python operators under load.
- The time of the whole test set with each collector: the two runs shared the machine with
  each other.
- Long-lived data on back ends other than `std::fvm`.
- A quiet machine. Every number above was taken at a load between 11 and 28 on 18 threads.

## 7. Should it be the default?

The facts, from the sections above:

1. Correctness on the test set is equal: both pass, and the generational collector passes the
   poisoned and the verified runs that were made.
2. For allocation of short-lived values the generational collector is clearly better on every
   back end: half the time on the interpreters, a fifth of the memory, and stops of
   milliseconds where mark-sweep stops for 100 to 800 ms.
3. For data that is built and kept it is 10 to 45 % slower and is stopped two to three times
   as long in total; the longest single stop is about the same.
4. For tensor work and for code that does not allocate there is no measured difference.
5. `std::jit` loses 9 of 93 compiled graphs in the test programs (none after `std::inline`),
   and a hot function that returns or uses an object constant can lose a factor of 20.
6. The `any` defect turns from "wrong only under stress" into "wrong output, exit code 0" in
   a ten-line program that uses `json.decode` (6.4).
7. Mark-sweep has a stall of its own (6.4) that the generational collector does not have.
8. C++ modules outside this repository have never run under a collector that moves objects.
9. Only one platform was run, on a loaded machine.

Recommendation: **not yet; keep mark-sweep as the default for now and make the switch after
two things are settled.**

- The `any` question (6.4, and section 9 of the handle rules). As long as a slot typed `any`
  can hold a reference that the collector neither traces nor updates, making the generational
  collector the default would turn a rare crash into silent wrong output for programs that
  keep a decoded JSON value. This is a language question (a run-time tag, or `any` replaced by
  generics), not a collector one, and it is the author's to decide.
- A run on the other platforms the project supports, Windows at least, with the default test
  set in both modes.

Neither the JIT restriction nor the cost for long-lived data is a reason to wait in itself:
the first costs nothing after `std::inline` in the programs examined and has a known remedy,
the second is the ordinary price of a copying young generation (a lower
`CAMEL_GC_PROMOTION_AGE` should reduce it; that was not measured). The measurements argue for the switch once the two points above are
closed, and the stall of the mark-sweep allocator argues for not staying with it indefinitely.
Until then the option is safe to recommend to users whose programs do not hold `any` values:
`--gc generational`.
