# One end-to-end example: effects, a closure value, a loop, and a program that reads and edits its own graph

The lane the four reviewers asked for: the distinctive combination in one place. A function holds a
closure that captures a parameter, a loop calls it once per round, the closure's own line is an
effect, the program hands the closure to a self-written higher-order function as a value, and a pass
written in Camel reads that program's graph, edits it, and commits. The program then runs from the
function the commit made, and a second, read-only pass reads the committed graph and prints its
canonical form. A third reading rewrites the program another way -- two independent statements
swapped, three names different -- and the canonical form has to come out the same, and does.

Everything here is Camel code and this report. No file under `src/` was read for a change, no
expected value moved, and the edit-block syntax does not appear anywhere (it is not implemented:
what is used is `open` / edit / `commit`).

* run on: `02939ffb7a1cc23e65ffad7c09f35b7cf3509859` (`origin/develop`, clean), Release build
  `out/latest`, footprint `261119_155347_02939ffb7a`, fingerprint `1d03db6a8fdd`, options
  `09afb0dfe7d8`, `Camel v0.2.45`, 1347 source files. The `grad` library fix was not available, so
  this lane ran on current `develop`, as the task allows; the hash above is the one to quote.
* a different build than the one in `test/plans`' recent readings is not involved: no plan is
  touched (see "What was not done").

## The program

`program.cml`, 53 lines. `run(n, base)` holds `label`, computes two independent values `twice` and
`thrice`, makes the closure `f` -- `(x: int): int => sync { let weight = base + 1; let value =
weight + x; '{label} {x} -> {value}'->println; return value }` -- captures the parameter `base` and
the local `label`, and prints one line per call. The loop

    let (_, total) = loop i = 0, total = twice + thrice while i < n do sync { (i + 1, total + apply(f, i)) }

calls it once per round through `apply`, a self-written higher-order function that takes the closure
as a value and calls it. `let tail = apply(f, n)` calls it once more, so the marker count to expect
is four.

    step 0 -> 11 / step 1 -> 12 / step 2 -> 13 / step 3 -> 14 / total 86 tail 14   (exit 0)

The closure prints its own line and the printed value is written twice in it (`weight + x` for the
print, `value` for the return): that is the redundancy the dedupe pass takes away, and it is written
once, deliberately, because a pass needs something to find.

`program-writing-b.cml` is the same program, written differently: `twice` and `thrice` are swapped
(they read nothing of each other, write nothing, and are both read by the loop's initial state, so
their order is not part of the program) and `label`, `f`, `weight` are named `mark`, `tally`, `bias`.
It prints the same five lines.

## The pass

`passes.cml`, an ordinary library, no privileged access: it reads the program through `graph`'s
public reads (`graphs_of`, `nodes`, `op`/`op_name`, `type_of`, `inputs`, `is_anchor`,
`may_be_rearranged`, `is_constant`, `region_of`/`region_kind`, `exit_of`, `callee`, `id`) and edits
it through `open` / `inline` / `order` / `replace` / `commit`. It is found beside the program, which
is why every command below is run from this directory.

The three things it has to find are found structurally, never by taking a list's first element:

* **the closure body** -- the one graph the program reaches that prints and that no `Func` node of
  the program names. A closure is called through the value it is, so no node names its graph; a
  function called by name is named by the `Func` node that calls it. `callee` answers that, and it
  answers `null` for the closure.
* **the print** -- the one `:io/println` call of that graph.
* **the duplicate** -- the node whose computation (`op`, type, canonical input numbers) equals the
  computation of the node the graph hands its value out of (`exit_of`, then `inputs`).

`passes::instrument` splices `tick` -- `() => sync { return println('# tick') }`, which captures
nothing -- at the print. Because `at` is a statement of its block's chain, the body takes that
statement's place and its order edges, and what the statement held is read from the body's value:
`tick` hands the printed string on, so nothing that read the print sees a different value.

`passes::dedupe` replaces the duplicate with the node the graph hands out, so the printed value and
the returned value are computed once.

`passes::report` is read-only: it counts the nodes of every graph the function reaches and prints
`sha256` of the canonical text of those graphs -- every node in `nodes`' canonical order, in one
line, by operator, type, whether it is a constant, and the canonical numbers of the nodes it reads.
No graph name, no creation id, no source origin is in that text, so a rename and a swap of two
independent statements cannot move it.

`passes::dump` prints the reading the choice is argued from: one line per node with its canonical
number, operator, type, region kind, the two judgements (`is_anchor`, `may_be_rearranged`) and the
computation key.

## The commands and the raw output

`capture.sh` is the run; `raw-output.txt` is its output, verbatim, 301 lines. From the worktree root:

    sh docs/handover/reports/eval-round10/e2e/capture.sh > docs/handover/reports/eval-round10/e2e/raw-output.txt 2>&1

That is the record **before** the two defects at the bottom of this page were fixed (`02939ffb7a`, and
the build the paper cites; both before `3fd76e564`). `raw-output-after-inline-gate-and-order-lift.txt`
is the same script's whole output on `932ee03d0`, the commit that fixes them (`modules/graph/txn.cpp`:
the body spliced at a pure node hangs its writes on the exit `GATE` of the region it lands in, and
`order` lifts its two ends to the region they share). So the two files are one script's output in one
order, 301 lines against 316, and `diff raw-output.txt raw-output-after-inline-gate-and-order-lift.txt`
is the whole of what the fix changed: seven hunks, 37 lines in and 22 out -- the build header; the
ordered run, refused before and accepted after; the reversed run's four markers and the canonical hash
of the graph it commits; and the `passes::dump` of that graph, 13 nodes against 12. The unordered run
does not appear in the diff at all: its refusal is the same bytes. Each reading is in "The readings"
below. The fix landed as `3fd76e564`; this file was captured on `932ee03d0` with the tree clean
(`dirty: false`), and its `footprint` field still names an earlier dirty configure because that string
is set when cmake reconfigures, not when a file changes.

and one command of it by hand:

    cd docs/handover/reports/eval-round10/e2e
    export CAMEL_HOME=~/camel-work/e2e/out/latest
    export LD_LIBRARY_PATH=~/camel-eval/conda/envs/clang/lib:$CAMEL_HOME/libs:$CAMEL_HOME/lib
    $CAMEL_HOME/bin/camel program.cml passes::instrument passes::report std::nvm

## The readings

**After the fix, the three readings are these -- and the three subsections that follow are the record
of what the lane found before it, the defect report the author's 00:55 answer of 2026-11-20 ruled on,
kept as it was written:**

* **ordered** -- `camel program.cml passes::instrument_ordered std::nvm`, exit 0: the original five
  lines with `# tick` after each of the four. Accepted, and the marker runs.
* **unordered** -- `camel program.cml passes::instrument_unordered std::nvm`, exit 1, the refusal
  quoted in "Item 4" below **character for character** (same two nodes, 9 and 13, same source
  position `passes.cml:146:12`). The check was not widened to accept this one.
* **reversed** -- `camel program.cml passes::instrument_reversed std::nvm`, exit 0, `# tick`
  **before** each original line: the marker runs, where before the fix the pass exited 0 and the
  marker never printed.

`passes::instrument` is still the take-over form (the body stands where the closure's own line stood),
so writing the pass back as "`inline` at a pure node + `order`" is a commit of its own -- the author's
00:55 answer, item 3 -- and the section header `capture.sh` prints for the reversed run ("the one it
accepts and drops: the marker never runs") still says the old thing until that commit lands.

### The edit that runs: the marker

`camel program.cml passes::instrument std::nvm`, exit 0:

    # tick
    # tick
    # tick
    # tick
    total 86 tail 14

Four calls, four markers, in the round each call belongs to; the totals are unchanged. What the
marker replaced is the closure's own per-round line, and that is the one thing to be plain about:
see "What the graph model forced" below. The commit accepted the new function, and the run that
follows it is the run of the function the commit made -- `passes::report` after it reads a program
of 55 nodes, not 53.

Before and after, in full:

    before                        after the commit
    step 0 -> 11                  # tick
    step 1 -> 12                  # tick
    step 2 -> 13                  # tick
    step 3 -> 14                  # tick
    total 86 tail 14              total 86 tail 14

### The edit that runs: the dedupe

`camel program.cml passes::dedupe passes::report std::nvm`, exit 0:

    e2e graphs=5 nodes=52
    e2e canonical=034e7b20e073b2b0fbe418b2f2abced8dda2702ebf46913cfc29adf95606a3a9
    step 0 -> 11 ... total 86 tail 14

against 53 nodes before it: one node fewer, and the output byte for byte what it was. This is the
"output identical" reading the task allows, on a real transformation (a common-subexpression
elimination on the closure body the loop calls).

### The two writings

| reading | `program.cml` | `program-writing-b.cml` |
| --- | --- | --- |
| program output | `step 0 -> 11 … total 86 tail 14` | same, byte for byte |
| `std::jgir canonical=true` sha256, before the edit | `591ca3b92b609c1f63bd0e08cca3ab277211281d4e0b7ca5f82bdedb608386b5` | same |
| `passes::report` canonical hash, before | `558bd6979bbb970a44c235df84798dd90226a131d84deff7587a58f5b5712e19` | same |
| `std::jgir canonical=true` sha256, after `instrument` | `fe1e102cbe7a45778458b34023fa2c47c2cedd976f4010dd45b120d1ff8f4a71` | same |
| `passes::report` canonical hash, after `instrument` | `de98a8b6489a3c15a044b7995d85ee5eedcce81600c9ff398df13c6c176abcf3` | same |
| `std::jgir canonical=true` sha256, after `dedupe` | `6723e24b26ac1b189917772362f3336d7f1fbea3387ea5061e6fee6915bbf33f` | same |
| `passes::report` canonical hash, after `dedupe` | `034e7b20e073b2b0fbe418b2f2abced8dda2702ebf46913cfc29adf95606a3a9` | same |

Two independent readings agree, before the commit and after it: the graph module's canonical order
(`passes::report`) and the canonical document `std::jgir canonical=true` writes. The commit returns a
*new* function value, and the equality holds for that function value, which is what the task asks
for: the pass hands the returned function to a second, read-only pass, and the two writings of the
program reach one canonical form.

### The canonical size

| | graphs | nodes | per graph |
| --- | --- | --- | --- |
| before the edit | 5 | 53 | 2, 4, 31, 5, 11 |
| after `instrument` | 5 | 55 | 2, 4, 31, 5, 13 |
| after `dedupe` | 5 | 52 | 2, 4, 31, 5, 10 |

The entry graph is 2 nodes, `main` 5, `run` 31 (with its `loop_cond` and `loop_body` regions), the
closure body 11, `apply` 4. `apply` is 4 nodes and its call node has **no callee**: the closure goes
in as a value and is called through it, which is exactly the shape the reviewers said was missing.

## Item 4: the wrong version, and the refusal, verbatim

`camel program.cml passes::instrument_unordered std::nvm`, exit 1. This is the same pass with the
order edge left out: the body lands in a region of its own beside the print and nothing orders the
two writes to stdout. Verbatim, one line plus the help the diagnostic carries:

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 9 of graph '__g_441__' (:io/println), in one region of graph '__g_441__', has no order against node 13 of graph '__g_441__' (:io/println), and at least one of the two writes (write stdout / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
```

`camel program.cml passes::instrument_ordered std::nvm` -- the *same* edit with `order(t, before,
added)` added, the form the task specified -- gives **the same message, character for character**,
exit 1. The order edge changes nothing that the check reads. That is the finding of this lane, and it
is why the accepted pass uses the take-over form instead: see below.

The better counter-example the task suggests -- "the closure call's effect declaration is removed" --
was not written, and could not be: the effect of a function value is in its *type* (`(x: int) => int
uses var stdout`, which `passes::dump` shows on the two `Func` nodes of `run`), and the graph
interface has no operation that edits a type. `replace` and `inline` would carry the type with them,
and no pass can make a value's type claim less than the graph does. `PassEffectBeyondType` is
therefore reachable only by writing a program whose closure type lacks the effect, which is not an
edit of this program but a different program; the honest counter-example available today is the
`PassEffectOrder` one above, quoted verbatim.

## What the graph model forced, in one paragraph each

**The ordered splice is refused.** The specified form is a body spliced beside the print (`at` = a
node that stands in no chain) plus `order` to put the marker after the print. `commit` refuses it,
with the message above -- the same one the unordered splice gets, character for character. So the
edge is written and changes nothing that the check reads. The body lands in a region of its own
(`RegionKind::Inline`); the effect check looks through that region -- it judges both prints in the
one the Inline region is nested in (`judgedRegionOf`, `src/runtime/resources.cpp:1586`) -- and asks
for a path between them, so it asks for a path into a region the edge writer does not carry an edge
into. The whole reading, both directions, with the commands and the dump that shows the marker
sitting in an `Inline` region of the committed graph, is its own page:
`docs/handover/questions/2026-11-19-an-order-edge-across-an-inline-region-is-ignored-by-the-effect-check.cn.md`.

**The mirror image is worse than the refusal.** With the edge the other way round --
`order(t, added, before)` -- the commit is **accepted**, the program runs, and the marker never
prints: `passes::instrument_reversed` exits 0 with the five original lines, and `passes::report`
counts 56 nodes against 53, so the marker's nodes are in the committed graph. `passes::dump` on that
program shows them where they are: the closure body comes out as 14 nodes with the `# tick` string,
the `:io/println` and the body's exit all in `RegionKind::Inline`, and the closure's own print in
`RegionKind::Root`. A region that only records where code came from runs nothing, so the effect the
check counted is in the graph and not in the program. That is a silent half-truth where the ordered
form gives a refusal, and it is the worse of the two; the pass is shipped anyway, so that the page's
reading can be reproduced.

**So the edit that runs takes over the print's place.** `inline` at a statement of the block's chain
hands that place -- and its order edges -- over to the body, which is ordering by construction: the
marker runs where the closure's line ran, once per call, inside the round. The cost is that the
closure's line is gone: the body is what stands there now. Adding a line beside an existing effect
needs the ordered splice above, so today's interface cannot express "keep the line and add one after
it". That is the one thing this example cannot show, and it is a gap in the interface, not in the
program: nothing in `src/` was changed to work around it, and nothing could have been -- this lane
writes only Camel code and reports.

**A committed graph carries a flag a built one does not.** `passes::dedupe` on this program gives a
graph whose canonical *document* differs from the same program written with the value computed once
(`6723e24b26ac1b18…` after the commit, `9b68156b9f89f296…` for the program that never had the second
`weight + x`) in exactly one field per graph: `flags`, `0` against `2`. Bit 2 is
`kGraphFlagGatesDissolved` -- "A commit looked at the graph's gates"
(`include/camel/runtime/graph.h:339`). The structure is identical; the document is not. Since
`passes::report` hashes nodes and not flags, the two writings still agree there, and the canonical
order is the reading to quote for structure. Whoever reads `std::jgir canonical=true` bytes to
compare a program with an edited one should know that bit.

## What was not done, and why

* **No plan reading is attached.** `program.cml` and `passes.cml` are not in `test/cases/`, so they
  are not a plan case: no plan's case count can move, and the six plans quoted in the task
  (`compile 2638/2622/0`, `linear 869/864/0`, `opt 1620/1620/0`, `parse 126/126/0`, `para 104/104/0`,
  `tools 70/70/0`) are untouched by this lane. Nothing in this directory is discovered by the plan
  runner.
* **No edit-block syntax.** It is not implemented, so every edit is `open` / `inline` / `order` /
  `replace` / `commit` on the graph module's today interface, as asked.
* **No `src/` change, no semantics change, no expected value moved.** The two interface gaps above are
  reported, not patched.
* **`PassEffectBeyondType` counter-example**: not reachable, see above.
* **The marker does not add a line, it takes the line's place**: the interface cannot express the
  additive form, see above. Both accepted edits are shipped (`instrument`, `dedupe`), and both are
  reported with their before/after output.
* **`grad`**: the library fix this lane was told to wait for was not available, so the readings are
  from current `develop`, `02939ffb7a`, and the hash is quoted above.

## Files

| file | what it is |
| --- | --- |
| `program.cml` | the program |
| `program-writing-b.cml` | the same program, two statements swapped and three names different |
| `passes.cml` | the passes: `instrument`, `dedupe`, `report`, `dump`, and the two the commit refuses |
| `capture.sh` | the run, every command in one script |
| `raw-output.txt` | its output, verbatim -- the record **before** the two fixes (`02939ffb7a`) |
| `raw-output-after-inline-gate-and-order-lift.txt` | the same script's output after them, verbatim (`932ee03d0`) |
| `NOTES.md` | this file |
