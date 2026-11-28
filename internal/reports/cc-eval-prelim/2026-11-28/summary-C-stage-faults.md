# RQ3 · stage faults (`tab:stage-faults`) — preliminary, partial

`tip 353d36e3d1` (`lane/cc-eval-prelim-2`) · build host `zjwei@camel-work` · `[load] 1m 27.43` during the last run ·
2026-11-28 · **preliminary, not final**.

## 1. The archive harness, and what it expects

`CC2027/eval/run.sh` is a host-side driver: it copies `scripts/` into the **docker container `camel-lanes`**, runs one
script there, and copies `results/<PIN>/` back. `common.py` fixes the rest: `PIN` (default `adb67e081`) selects the
install at `/work/pins/<PIN>`, `SRC` the read-only source tree (`/work/lanes/semfront`), `CAMEL =
$PINDIR/bin/camel`, never more than 4 jobs. `e9_difftest.py` adds two assumptions of its own: it imports
`SRC/test/tools/progen/{generate,model,runner}.py`, and it spells each pipeline stage as **`std::opt::<stage>`**
over `[devirtualize specialize inline fold cse algebra dce order]`, plus `std::macro` and `std::opt`.

What the current tip gives:

| the archive expects | this machine / this tip |
|---|---|
| docker container `camel-lanes`, pin at `/work/pins/<PIN>` | **not available**: `docker ps` answers `failed to connect to the docker API at npipe:////./pipe/dockerDesktopLinuxEngine: the system cannot find the file specified` |
| `SRC/test/tools/progen/{generate,model,runner}.py` | present at `353d36e3d1` (with `progen.py`, `reduce.py`, `triage.py`) |
| stages as `std::opt::<stage>` | **refused**: the isolation of the C++ passes removed the pre-isolation spelling. Read as a verdict: `C_pfx.archive_stage_name.refused` PASSED — `std::opt::fold` was refused and the name echoed |

So the archive's matrix cannot be reproduced as written on this tip: the container is not here, and even with it the
stage spelling no longer resolves. That is the first reading of this item, and it is a **version** finding, not a defect.

## 2. What was run instead — the half that does reproduce

The archive's differential test has two halves per program: the program under a pipeline **prefix**, and the same
program with **no pass** as the reference; the first prefix that disagrees localises the stage. That half needs only the
compiler, so it was reproduced with the current tip's own pass names, as a plan of `compare_to` entries
(`C-prefix.plan.toml`, instrument): 4 programs × 3 prefixes.

```
[SUMMARY] total: 15, pass: 14, fail: 1     (the 1 fail is the deliberate negative below, not a red)
4 programs x {no pass, opt::fold, opt::cse|opt::dce} = 12 cells, all 12 identical to their own no-pass run
```

The four programs are `fold_subject`, `cse_subject`, `dce_subject`, `cse_after_inline` from `test/cases/opt/camel_passes/`;
each prefix's `stdout_normalized` equals the no-pass cell's, and the exit code too. **Equivalent to** the archive's step
"run each prefix, compare with the reference"; **not equivalent to** it in three ways: the programs are four from the
corpus, not progen-generated ones (so `n=100` is **not reached**, recorded as "—"); the prefixes are three, not the
eleven of the archive; and each cell ran on `std::nvm` only, not on four back ends × two collectors.

## 3. Fault injection

Two classes are asked for, five shapes each. **Class 2 (a legal graph whose meaning changed) — one shape, run:**

`internal/reports/cc-eval-prelim/2026-11-28/C-semantic-fault.cml` declares `rule add_becomes_sub(a: int, b: int) { a + b => a - b }`
and applies it with `f->rewrite<rules> !! f`. The commit check accepts it — the graph is well formed, every port has its
type, the order of conflicting accesses is untouched — and the run prints both values:

```
[stdout] 6
[stdout] 4
```

`f(5)` is 6, the rewritten `f` is 4, exit 0, no diagnostic. **Found by: neither the commit check nor any diagnostic;
only a differential comparison against the unmodified program shows it.** That is what `DESIGN.md` §14 predicts, and it
is the one cell of the table this run fills with a real observation rather than a plan.

**Class 1 (an illegal graph) — not attempted in this window, recorded as "—".** The five shapes (an edge to a port that
does not exist, a node referenced across graphs, a region ownership violation, a port of the wrong type, deleting the
only order edge between two conflicting accesses) each need a hand-built graph through the edit API, and each attempt
costs a build against an API that cannot be exercised locally. With the archive's own generator unavailable (no
container), I did not get to them. **This is a gap in this reading, not a result.** Likewise the remaining four class-2
shapes (dropping an effect-order edge, flipping a comparison, folding a constant to the wrong value, widening an
integer type) were not run.

## 4. Where each number came from

- The prefix matrix and the two fault entries: **run by this lane** in this session, `[SUMMARY] total: 15, pass: 14,
  fail: 1`; the single fail is the negative entry `C_pfx.semantic_fault.differs`, whose expectation (stdout without
  `6`) is exactly what the injected fault contradicts — it is the reading, recorded as a fail on purpose.
- Environment facts: `docker ps` here, and `git ls-files` at `353d36e3d1`.
- Timings: none are quoted; `[load]` 27.4 (1m) makes them useless, and this item asks for none.