# What to do next, taken from the graph-editing draft

Baseline `develop = 33d18ae3e`. No code and no build: this page is the only file the lane wrote.

The draft and Principle 9.12 are **not on `develop`**. Both are read from `origin/fsh-dev` (`f414001f6`): the
draft at `docs/design/graph-editing/README.cn.md` (325 lines, `7ab485149`), the principle in `648002d2a`. Line
numbers below are the draft's own. The tier headings are the author's three tiers of 2026-11-14
(`internal/archive/batches-2026-10-31-72h.cn.md:305-309` at `origin/fsh-dev`).

## First tier — semantics and correctness (the paper's tier 1)

**【1】The commit must ask whether every input of a node is visible from the region it runs in, not only its ports.**
· Why now: the daily review of 2026-11-18 names this as one of the two missing of the six (`internal/reviews/questions-2026-10-29.md:4250` at `origin/fsh-dev`); the six are §8 of the draft (`README.cn.md:146-149`). Today `checkGraphCrossReferences` (`src/runtime/graph_edges.cpp:913`) asks visibility only of inputs whose kind is `Port` — the walk at `:1058-1077` `continue`s on every other input at `:1070-1072`, so a node in one arm that reads a value computed in the sibling arm passes. `CommitCheck` carries two values, `Effect` and `Order` (`include/camel/runtime/resources.h:378`), and `commitViolation` (`src/runtime/resources.cpp:1794-1813`) asks nothing else.
· Judgement: one new `[SUMMARY]` line in `test/plans/feat/opt/commit_refusal.plan.toml` — `commit_refusal.scope` — over a fixture that splices into one arm a node reading the other arm's value: refused, naming the node and the region. Read that the same program commits clean today before writing the check; do not assume it.
· Landing: `src/runtime/resources.cpp` (`commitViolation`, beside `effectBeyondDeclaration:1492` and `orderIncomplete:1656`), `include/camel/runtime/resources.h` (`CommitCheck:378`), `src/runtime/draft_session.cpp:1933` (the one call site), a test pass registered beside `test::commit_exit_region`.
· Depends: nothing.
· Size: medium (one module, one test pass, one plan line).

**【2】The commit must check a graph's ports and exit against the type the function declares.**
· Why now: the other missing one of the six (`questions-2026-10-29.md:4250`, "端口与声明的类型一致"); draft §8 "signature and declaration agree" (`README.cn.md:147`); and `edit` cannot change a signature without it (`README.cn.md:122`, `:287` `add_param`, `:281` `replace(exit(d), v)`). Today the seal asks only that the exit, the output and the returned value sit in region 0 (`38cc52241`; `validateRuntimeGraphPayloadImpl` in `src/runtime/graph.cpp`).
· Judgement: `commit_refusal.port_type` in the same plan: a draft whose FUNC port list differs from the opened function's type is refused with that diagnostic, and one that matches commits.
· Landing: `src/runtime/resources.cpp#commitViolation`; a new `CommitCheck` value in `include/camel/runtime/resources.h:378`. The declared type is already reachable in that function — `effectBeyondDeclaration` reads `type->resources()` at `:1499`.
· Depends: write it to take the declared type as an argument, because when a block head writes a new type (`README.cn.md:261`) the caller must pass that instead of the opened function's type.
· Size: medium.

**【3】A commit's tier must be "which of the six this edit can break", not "did it make a node".**
· Why now: the daily review's second item (`questions-2026-10-29.md:4251`) — `src/runtime/resources.cpp:1800` `if (made.empty()) return std::nullopt;` means an edit whose only change is a new sync input on an existing node (turning it into a write), or a change of its operator to a writing one, is asked nothing about effect or order; `:1807` skips the rest for the same reason. The rule the draft writes is per operator: `replace`/`erase`/`eval`/`outline` are theorems, `inline`/`order` are checked (`README.cn.md:153`).
· Judgement: a new `commit_refusal.changed_node` in `test/plans/feat/opt/commit_refusal.plan.toml`: a commit whose only edit is one new sync input on an existing node, leaving a conflicting pair unordered, is refused; today it commits. Mechanical side of it: `grep -c 'made.empty()' src/runtime/resources.cpp` from 2 to 0.
· Landing: `src/runtime/resources.cpp:1794-1825` (`commitViolation`, `commitAddsAccess`), `src/runtime/draft_session.cpp:1933`.
· Depends: on a per-operator breakage table existing as a table. Only three entries are written anywhere (the review names them); see the last section.
· Size: medium.

**【4】`inline` as an editing operator of `modules/graph`, in the two branches the 11-18 ruling gives it.**
· Why now: it is J's first step and lane L waits for it (`internal/plans/2026-11-18-lane-map.cn.md:16` and `:18`); the operator is §7 of the draft (`README.cn.md:119`) and the ruling is `e55cd6157` as read back in `internal/reviews/2026-11-18-the-at-of-inline-says-a-region-not-a-place.cn.md:45-62`: a statement `at` takes over its place in the sync and order chains, a pure `at` only picks the region and the operator writes no `order` (the caller does). `inline` is absent from `stdlib/graph.cml` (read in full: `replace:348`, `erase:354`, `eval:363`, `order:375`, `add_constant:382`, `exit_of:83`; no `inline`, no `outline`).
· Judgement: `std::gir` before and after a new `test/cases/modules/graph_pass/edits_inline.cml`. In a `sync` block whose two statements carry `order=0 sync=0` and `order=1 sync=1` (the `order_all` reading of `test/cases/modules/graph_pass/orderprobe.cml`), `inline(call, …)` leaves both attributes unchanged and puts the spliced statements between them; with a pure `at` the spliced write carries no `order` edge. One `[SUMMARY]` line per branch.
· Landing: `modules/graph/txn.cpp` (a kernel beside `orderKernel:409`), `modules/graph/module.cpp:126-134` (`kernels()` already folds `txnKernels()`), `stdlib/graph.cml` for the declaration. The primitive is `inlineCallableInDraft` (`include/camel/runtime/draft_inline.h:34`) answering `DraftInlineResult{callNode, valueExit, ctrlEntry, ctrlExit}` (`:20-27`); its only caller today is the C++ inline pass (`src/passes/opt/inline/engine.cpp:1308`).
· Depends: nothing on the block surface; lane L waits on this.
· Size: medium, **if** the statement branch is already inside `inlineCallableInDraft` — I did not read `src/runtime/draft_inline.cpp`, so I cannot say (see the last section).

**【5】`outline` needs a commit that delivers the graphs the draft made.**
· Why now: the draft lets a draft make new graphs (`README.cn.md:107`, `:120`, and the 11-18 ruling folded it into §6.5), and the measurement in `internal/reviews/2026-11-18-outline-needs-a-graph-that-was-not-there.cn.md:15` and `:60` says `commit()` delivers only what `collectCommitClosure()` reaches, while `GraphDraftGroup` (`include/camel/runtime/draft_session.h:361`, `reserve():370`, `encode():380`) builds its members in a program C++ holds.
· Judgement: after a commit that outlines one region, `len(graphs_of(g)) == len(graphs_of(f)) + 1` and `callee` of the call left at the original place answers the new graph; today that count is unchanged.
· Landing: `src/runtime/draft_session.cpp` (`collectCommitClosure():1341`, the `fresh` map at `:1715`, the delivery loop at `:1865`), `include/camel/runtime/draft_session.h:278` and `:361`.
· Depends: on nothing else, but it is the decision itself. Shape `graphs(d)`/`params(d)`/`exit(d)` (`README.cn.md:273`) in the same pass: `params` does not exist in `stdlib/graph.cml` at all.
· Size: large (cross-module).

**【6】`readonly` as a resource modifier, and with it the `graph` resource.**
· Why now: principle 9.12 (last bullet) makes reading a graph an effect, and §15.5 writes it (`README.cn.md:316-325`): `export resource graph readonly`, `spelling readonly`, and `uses var graph` is a type error. The only modifier today is `unordered` (`src/parse/syntax/parser.cpp#parseResourceDecl:1245`, the branch at `:1255`; grammar comment `src/parse/ast/builder.cpp:2546`, handling `:2566-2568`; the printed form `src/parse/ast/stmt.cpp:96` and `:111`).
· Judgement: `grep -c readonly src/parse/syntax/parser.cpp` from 0 to 1 plus its ast handling, and one negative case in which a declaration writing `uses var graph` is refused with a named diagnostic.
· Landing: `src/parse/syntax/parser.cpp:1245-1259`, `src/parse/ast/builder.cpp:2546-2568`, `src/parse/ast/stmt.cpp:96/111`, and the resource signature the effect check reads (`src/runtime/resources.cpp#effectBeyondDeclaration:1492-1499`).
· Depends: nothing; lane L needs it to write the effect at all.
· Size: medium-to-large (front end plus one semantic rule).

## What I read, and what I did not

Read in full: the draft (325 lines by `git show`), the lane map (27 lines), `docs/roadmap.cn.md` (77),
`internal/archive/handover-README.cn.md` (87), five question pages — `the-at-of-inline…`, `outline-needs-a-graph…`,
`commit-checks-which-pairs-a-commit-owns`, `the-state-slot-is-a-ruling-with-no-code`, `retract-rc23-two-orders`
— `stdlib/graph.cml` (428), `include/camel/runtime/draft_inline.h` (86), `resources.cpp:1780-1849`,
`graph_edges.cpp:1042-1091`, `parser.cpp:1236-1265`, `parser_expr.cpp:60-119`, the daily-review section appended
by `72c20847c`, and the diff of `648002d2a`.

Not read: `src/runtime/draft.h` (75 KB), `draft_inline.cpp` (125 KB), `draft.cpp` (117 KB), `draft_session.cpp`
in full (95 KB, grepped only), `modules/graph/read.cpp` in full (47 KB), `resources.cpp` before line 1400; the
other six question pages of 2026-11-18 and the pages of 11-16/11-17; `docs/design-principles.cn.md` on `develop`
(a grep finds no 9.12 there — the principle is only on `fsh-dev`).

What I could not settle, and would not guess:

- **Whether item 4 is medium or large.** `inlineCallableInDraft` is 125 KB of code I did not read; the ruling's
  statement branch ("takes over its place") may already be implemented there, in which case the operator is a
  wrapper like `order`. If it is not, item 4 grows by the splice itself.
- **The breakage table item 3 needs.** Draft §8 writes the per-step checks and the six commit checks, and the
  review names three entries of the mapping (`replace` breaks acyclicity, scope, ports; `inline` with accesses
  breaks the last five). No page in the tree holds the whole table. Item 3 can be written operator by operator,
  but "which operator can break which check" would then be the lane's invention, not a ruling.
- **Whether the draft is dispatchable at all.** Its own header (`README.cn.md:3`) says work does not start until
  the author and the paper side finish; `648002d2a` then wrote 9.12 into the principles and the lane map schedules
  lane L. I found no page retracting line 3, so the header is stale rather than repealed. This list is written on
  the reading that 9.12 is the ruling — if it is not, nothing here is dispatchable.
- **What the review's "作用域" covers.** I measured that ports are checked and non-port inputs are not; I did not
  find a page saying whether the review meant "ports are enough, general inputs are the hole" or "neither".
- **Where the two absent things should live.** `develop` carries neither the draft nor 9.12, so a lane based on
  `develop` cannot cite either. Whether they are merged into `develop` before lanes start is decided nowhere I read.
- **The keyword cost of `view`/`edit`.** `\bedit\b` occurs 3 times in `stdlib/*.cml` plus `test/cases/**/*.cml`
  and `\bview\b` 3 times in `stdlib/*.cml`, every one of them inside a comment, so the rename cost looks like zero
  — but I did not scan `examples/`, `benchmark/`, the tutorials or `docs/`.
- **Off this list on purpose.** The deletion of `graph_of`/`graphs_of`/`Txn`/`open`/`commit`/`abort`
  (`stdlib/graph.cml:52/57/42/341/388/392`) and the block surface itself (lane map `:18`, author's tier 2) both
  wait on items 4-6; dispatching them now would specify against an interface those items are about to rewrite.