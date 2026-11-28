# RQ3 · stage faults (`tab:stage-faults`) — preliminary, partial

`tip 353d36e3d1` (`lane/cc-eval-prelim-2`) · build host `zjwei@camel-work` ·
`[load]` 21–31（其中约 3 核非 Camel：qemu/sidekiq/llm_server）during these runs · 2026-11-28 · **preliminary, not final**.

**Not reached, first line: n=100 is not reached** — the tree's generator produces programs this tip refuses, so the
100-program matrix ran to 300 cells and 0 pass (§2). **Of the ten injection shapes, five (class 1) are covered by the
repository's own injection passes and were read here (§3); three (class 2) I injected myself (§4); two class-2 shapes are
not attempted and are recorded as "—", with structural reasons.**
**The one red of the class-1 plan is not fixed here: what differs is not only a drifted constant (see §3).** It has since
been re-pinned in its own commit (only the exit code and the reader's rendering), so this reading's "red" is the state
before that re-pin, and the reading is kept as measured.

**The hundred programs are not in the repository, on purpose, and nothing is missing: they are regenerated
deterministically from their seeds by one command** — `python internal/reports/cc-eval-prelim/2026-11-28/make-C-gen.py 100`
— which rewrites `gen/prog_001.cml` … `gen/prog_100.cml` and the instrument plan from the same inputs. §7 states the
version dependence of that command.

## 1. The archive harness, and what it expects

`CC2027/eval/run.sh` copies `scripts/` into the **docker container `camel-lanes`**, runs one script there, copies
`results/<PIN>/` back. `common.py` fixes the rest: `PIN` (default `adb67e081`) selects `/work/pins/<PIN>`, `SRC` the
read-only tree (`/work/lanes/semfront`), `CAMEL = $PINDIR/bin/camel`, at most 4 jobs. `e9_difftest.py` adds two
assumptions: it imports `SRC/test/tools/progen/{generate,model,runner}.py`, and it spells each stage as
**`std::opt::<stage>`** over `[devirtualize specialize inline fold cse algebra dce order]`, plus `std::macro`, `std::opt`.

| the archive expects | on this machine / this tip |
|---|---|
| docker container `camel-lanes`, pin at `/work/pins/<PIN>` | **not available**: `failed to connect to the docker API at npipe:////./pipe/dockerDesktopLinuxEngine: the system cannot find the file specified` |
| `SRC/test/tools/progen/{generate,model,runner}.py` | present at `353d36e3d1` (with `progen.py`, `reduce.py`, `triage.py`) |
| stages as `std::opt::<stage>` | **refused**, and no alias: `std::opt::fold` is rejected and named. Read as a verdict in its own instrument (`C-archive-stage-refused.plan.toml`, entry `archive_stage_name.refused`, PASS = the refusal) |

## 2. The n=100 attempt, and why it stops at 0

The generator is in the tree, so the matrix need not go through docker: `generate(seed, DEFAULT_FEATURES, size=1)` for
seeds 1–100, each program exported to `gen/prog_<seed>.cml` with the reference output `model.py` computes, then three
cells per program (no pass / `opt::fold` / `opt::cse`) asserting that reference (`C-gen-prefix.plan.toml`, 300 entries).

```
[SUMMARY] total: 300, pass: 0, fail: 300      BUILD_EXIT=0, RUN_EXIT=1
96 of the 100 programs produced a diagnostic; 374 stderr lines in the log
  201 lines: 'v27' is assigned inside a closure, and a closure holds the value the name had when it was
             made, not the name
  114 lines: Cannot convert the string '' to int. The whole string must be an integer
```

**n=100 is not reached, and the reason is not that the script cannot run: the tree's `progen@353d36e3d1` generates
programs with its default feature set that the current rules refuse. That is a difference between the pin and the tip,
and it is now evidence rather than a failure** — the two dominant messages are the evidence: `'v27' is assigned inside a
closure, and a closure holds the value the name had when it was made, not the name` (201 lines — a shape the withdrawn
state rule allowed and this tip refuses) and `Cannot convert the string '' to int. The whole string must be an integer`
(114 lines — a program reading an integer the run does not supply). So the archive's matrix needs its pin; §5's prefix
differential therefore uses corpus programs instead, and §7 says how to replay this run.

## 3. Class 1 — an illegal graph. Read from the repository's own injection passes

`src/stdlib/test/commit_checks.cpp` already ships passes that damage a draft graph on purpose, and
`test/plans/feat/opt/commit_refusal.plan.toml` runs them and asserts the refusal text:

```
[SUMMARY] total: 14, pass: 13, fail: 1      BUILD_EXIT=0
```

| shape (as `DESIGN.md` §14 lists it) | injection | layer that finds it | verbatim line |
|---|---|---|---|
| conflicting accesses unordered | `test::commit_order` — splices two writes, orders neither | commit check | `commit_order: refused, check=order` · `committed a graph whose conflicting effects are not ordered` |
| effect beyond the declared type | `test::commit_effect` | commit check | `commit_effect: refused, check=effect` · `committed a graph that reaches past the effect its own type declares` |
| a node reading itself | `test::commit_cycle` | commit check | `commit_cycle: refused at the commit:` · `breaks its edge invariants` · `lies on a cycle of inputs` |
| a value read outside its region (exit) | `test::commit_exit_region` | commit check | `commit_exit_region: refused at the commit:` · `has its exit at node` … `instead of the function body` |
| the body's write re-sequenced out of its chain | `test::commit_ancestor` | commit check (`RuntimeDiag::PassEffectOrder`) | `commit_ancestor: refused, check=order` |
| a read of another arm | `test::commit_sibling_arm` | commit check | `refused at the commit: Runtime graph … breaks the branch-arm invariant` · `belongs to two arms` |
| a port of the wrong type | `test::commit_port_type` | commit check | `refused at the commit: … publishes a signature of another type than the ports it holds` |
| a made graph whose ports disagree with its declaration | `test::commit_made_graph_ports` | commit check | `… declares parameter 0 as 'i32', and its port node 0 carries a 'f32'` |
| a read across a loop body boundary | `test::commit_loop_body_read` | commit check — **the entry is RED** | expected exit 60, got exit 0; the refusal it printed says `OPER 25 (:op/copy) runs in the body (region 0) and reads OPER 14 (:op/add_l)` where the entry asserts `COPY 24` |

**The one red, recorded as red, and why it stays red for now:** the invariant *is* refused, and the refusal prints the
whole sentence; what differs is (a) the reader's name — the run says `OPER 25 (:op/copy)` where the entry asserts
`COPY 24` — and (b) the exit code, 0 where the entry asserts 60. The refusal's content (invariant, reader, region 0,
the value `OPER 14 (:op/add_l)`, region 2 of `LOOP 17`, the citation of `17_graph_rewrite_api_contracts.md` 4.1) is
character-for-character what the entry asserts. So it is not a semantic change, but it is not only a drifted constant
either: one of the two diffs is the node's own rendering (`COPY` became `OPER … (:op/copy)`). I therefore did not edit
the plan — the license was "numbers only", and this needs its owner's word.

So class 1 answers the design's question the way §14 predicts: **the commit check refuses every one of these shapes, and
names the shape.** What the table adds beyond the design's claim is the red: one injected case's expected text no longer
matches the code.

## 4. Class 2 — a legal graph whose meaning changed

`internal/reports/cc-eval-prelim/2026-11-28/C-semantic-fault.cml`: `rule add_becomes_sub(a: int, b: int) { a + b => a - b }`,
applied with `f->rewrite<rules> !! f`. The graph stays well formed — ports keep their types, nothing becomes unordered,
nothing leaves its region — so the commit accepts it and the run prints both values:

```
[stdout] 6
[stdout] 4
```

`f(5)` is 6, the rewritten `f` is 4, exit 0, no diagnostic. **Found by: nothing at the commit, no diagnostic; only a
differential comparison against the unmodified program.** The instrument's `C_pfx.semantic_fault.differs` entry is that
comparison and it fails on the difference — that fail is the reading, by design.

Two more shapes, same instrument mode, same finding (`C-semantic-faults2.cml` + `C-semantic-faults2.plan.toml`): a rule
`a < b => a > b` applied to `less`, and a rule `a * 2 => a + 3` applied to `dbl` — a comparison flipped and a constant
folded to the wrong value. One run, one entry, `[PASS]` with `expect_stdout_exact`:

```
[stdout] true      less(1, 3)               [stdout] false     the rewritten less(1, 3)
[stdout] 10        dbl(5)                   [stdout] 8         the rewritten dbl(5), 5 + 3 instead of 5 * 2
```

Both graphs stay legal: types match, nothing is unordered, no region is crossed; exit 0, no diagnostic, no refusal.
**Found by: nothing at the commit; the value alone — and a differential comparison is what reads a value.**

Two class-2 shapes are **not attempted, recorded as "—"**, and the reasons are structural, not budgetary:
**a dropped effect-order edge** cannot be expressed by a rule (a rule replaces a value; removing the order edge between
two effects is a graph edit, so the injection would have to be a pass inside the library, which this lane does not
write); **a widened integer type** expressed as "rewrite an int operation to a float one" is class 1 by construction —
the node would disagree with the graph's signature and the commit would refuse it — while a *legal* widening means
changing a declared signature, which a rule cannot express either.

## 5. The half of the differential test that does reproduce

Prefix differential over corpus programs, since the generator half is unavailable: 4 programs (`fold_subject`,
`cse_subject`, `dce_subject`, `cse_after_inline` from `test/cases/opt/camel_passes/`) × 3 prefixes (no pass /
`opt::fold` / `opt::cse` or `opt::dce`), each compared with its own no-pass cell (`C-prefix.plan.toml`):

```
12 cells, 12 identical (stdout_normalized and exit code) · [SUMMARY] total 15, pass 14, fail 1
```

**Equivalent to** the archive's "run each prefix, compare with the reference"; **not equivalent** in three ways: the
programs are four from the corpus rather than generated, the prefixes are three rather than eleven, and every cell ran
on `std::nvm` only, not on four back ends × two collectors. This is a demonstration that the mechanism is intact, not
the archive's matrix.

## 6. Where each number came from

- §2, §3, §5 and §4: **run by this lane** in this session; the run ids are in `C-stage-faults.csv`, one row per reading.
- §1: `docker ps` here, and `git ls-files` at `353d36e3d1`.
- The counts of the form "201 stderr lines" and "114 lines" in §2 are **lines of the run's own log**: counted with
  `Select-String -Path <log> -Pattern '\[stderr\]'` and then grouped by the message they carry (PowerShell's
  `Group-Object`), so they are log lines, not source lines — a source-line count in this batch always states its
  instrument (`wc -l`, `Measure-Object -Line`, `(Get-Content).Count`), because the three disagree.
- No timings are quoted: `[load]` was 21–31 on a shared host, and this item asks for none.

## 7. How to replay §2, and what it is pinned to

The hundred programs are **not** in the repository (they are 520 KB of generated source, and they are reproducible), but
the generator script, the instrument plan and the reference outputs are. From the repository root, on a machine with
`python3`:

```
python internal/reports/cc-eval-prelim/2026-11-28/make-C-gen.py 100
```

writes `gen/prog_001.cml` … `gen/prog_100.cml` and regenerates `C-gen-prefix.plan.toml` (1,836 lines, three cells per
program). `gen/index.json` is kept as the reading: it holds, per seed, the source path, the reference lines `model.py`
computed and the number of attempts generation needed.

**Version dependence, stated plainly:** the script imports `test/tools/progen/{generate,model}.py` **from this tree**
and calls `generate(seed, DEFAULT_FEATURES, size=1)`; `DEFAULT_FEATURES` is `frozenset(FEATURES) - {"modules"}` in
`generate.py`. So the replay is pinned to `progen@353d36e3d1` — the same source the archive's harness uses, but the
*defaults* travel with the tree, not with the pin. A different tip may generate different programs from the same seed,
and that is exactly what §2 measured. No third-party imports (no numpy): `random`, `typing` and the tree's own modules
only.
