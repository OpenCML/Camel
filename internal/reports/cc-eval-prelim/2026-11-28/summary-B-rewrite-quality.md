# RQ3 · rewrite quality (`fig:rewrite-quality`) — preliminary

`tip 353d36e3d1` (`lane/cc-eval-prelim-2`, based on `origin/develop fd950804d6`) · build host `zjwei@camel-work` ·
load 24.8 → 30 (1m) while these runs went · 2026-11-28 · **preliminary, not final**.

## What was read

The corpus `test/plans/feat/opt/` already puts each rewrite against its C++ counterpart, so the reading is
the verdict of those entries, not a new comparer. Three plans, run in one command on the build host:

| plan | cases | pass |
|---|---|---|
| `camel_passes.plan.toml` | 49 | 49 |
| `cxx_isolation.plan.toml` | 27 | 27 |
| `figure1_pipeline.plan.toml` | 6 | 6 |
| **total** | **82** | **82** |

`BUILD_EXIT=0`, `RUN_EXIT=0`, `[SUMMARY] total: 82, pass: 82, fail: 0`, `[provenance] tip (dirty: 3 files) |
binary built 2026-11-28T14:40:32+08:00 | tier fast | jobs 3`.

The dirty tree that provenance line reports is **not this reading's**: the run itself printed
`[TREE] 3 paths were already uncommitted when this run started, not this run's:
internal/collab/task-420-handover.md, probe_oob.plan.toml, test/cases/probe/draft_ref_oob.cml` — three
other lanes' untracked probe files, none of which this reading touches.

**82 is the number of cases executed, not the denominator of the comparison.** `camel_passes.plan.toml` has 35 `[[tests]]` blocks; five of them carry a
`backends = [...]` list (four with four back ends, one with three) and each such block expands into one case
per back end: 35 + (4−1)·4 + (3−1)·1 = **49**. The 27 and 6 need no expansion. So 82 is the number of cases
the three plan files run, and only part of them compare the two implementations.

**The pairs themselves: 19 comparisons, 19 identical.** Every entry whose name carries `by_camel`
(`camel_passes` 6, `cxx_isolation` 12, `figure1_pipeline` 1) compares the Camel rewrite's output with the
C++ rewrite's output. Two more entries compare the same two implementations as normalized text including the
warning (`camel_passes.fold.same_text`, `camel_passes.fold_fault.same_text`), so **21 of 21 comparisons agree**.
Form of the comparison, as the plans declare it: `std::jgir canonical=true` for the graph (the canonical
document drops names, node ids and source origins and derives the node order from the graph, so two runs that
produced the same graph print the same bytes), and `compare_fields = ["exit_code", "stdout_normalized",
"stderr_normalized"]` for the two differential entries. `std::gir`'s DOT is **not** the comparison form here:
it carries layout and per-run ids.

## Node counts

Measured with `std::stats`'s `GRAPH_STATS` `nodes` (summed over every graph the run reports), not from the
canonical document, whose node list expands graph references. Instrument: a scratch plan kept beside this
reading (`B-nodes.plan.toml`), one program per rewrite, both implementations:

| rewrite | case | C++ nodes / graphs | Camel nodes / graphs |
|---|---|---|---|
| fold | `cases/opt/cxx_isolation/subject.cml` | 34 / 5 | 34 / 5 |
| cse | `cases/opt/cxx_isolation/subject.cml` | 30 / 5 | 30 / 5 |
| inline | `cases/opt/cxx_isolation/inline_figure1.cml` | 30 / 4 | 30 / 4 |
| dce | `cases/opt/camel_passes/dce_subject.cml` (after `std::cxx::inline`) | 20 / 5 | 20 / 5 |

The two implementations leave graphs of the same size on the same program, for all four rewrites.

## Wall time (magnitude only)

Per entry, from the runner's own `[PASS] <name> <n> ms`, so it includes process start, the front end and, for
the Camel side, compiling the pass's own library at run time. C++ side 86–280 ms, Camel side 218–482 ms; the
Camel rewrite's run is consistently the longer one (roughly 1.5–2.5× on the same program), which is the cost
of the pass being a library loaded and compiled by the run rather than code already in the binary. **These are
magnitudes on a shared host, not timings for the paper.**

## Code sizes (`git ls-files` + physical lines, 2026-11-28)

| side | what | files | lines |
|---|---|---|---|
| Camel | `src/stdlib/opt.cml` (`fold`, `cse`, `inline`, `dce`) | 1 | 552 |
| C++ | `src/stdlib/std/cxx/opt/**` | 27 | 8771 |
| C++ | of which the four rewrites' implementation surface (`generic/*`, `inline/engine.cpp`, `constant_bytes.cpp`) | 7 | 5504 |

## Against the design

Consistent with `DESIGN.md` §8.2 item 3: the Lang-written optimizers are the same optimizers on this corpus —
every paired entry compares equal byte for byte, and the graphs they leave have the same node count. The two
things the design asked to also report are the cost of writing them (552 lines against 8771/5504) and the
optimizer's own time, where the Camel side pays for loading and compiling its own library.

**Unexpected.** (a) The Camel-side run is not just slightly slower but 1.5–2.5× the C++ one on every pair,
which is the library's own compile time dominating a 100 ms-scale program — the timings here say nothing
about the rewrites' cost once the library is loaded; (b) `cxx_isolation` keeps two entries that are not pairs
(`by_old_name`, the refusal of the pre-isolation name, and the default-pipeline name pair), so "all 27 pass"
is not "27 comparisons" — 12 of them are.

## Where each number came from

- 82/82 and the per-entry verdicts and times: **run by this lane** in this session
  (`[SUMMARY] total: 82, pass: 82, fail: 0`, binary built 14:40:32).
- Node counts: **run by this lane**, instrumented plan described above (`B_nodes.*`, 8 runs).
- Code lines: counted in the tree at `353d36e3d1`.