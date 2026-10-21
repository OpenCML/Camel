# GS-REPORT identity (package A, steps 1 of design 7)

Branch `gs/identity` (worktree `Camel-gs-identity`), base `fsh-dev` 79060dd7. Not pushed, not merged.

## Commits

1. `486b11ef` fix(build): `state->template get<...>` in `include/camel/execute/loop_driver.h:212`. fsh-dev 79060dd7 does not
   build with the container's Clang 19 without it. wide and scans need the same one-line change.
2. `f2301184` GraphId. `include/camel/runtime/graph_id.h`: `enum class GraphId : uint32_t` (0 invalid), `GraphIdAllocator`
   (atomic, ids never reused), allocator on `Context::graphIds()`. `GCGraph::id()`; `DraftGraphBuilder::graphId()` (the
   front end takes an id when it creates a compile graph); `GraphDraft::id()/setId()`: decode takes the id of the graph it
   decodes, encode builds the graph with it, so a commit's copy keeps the id. New graphs (specializations, draft group
   members, autodiff rules, tiles regions/dispatch) take a new id.
3. `c708e1e2` Program snapshot and function values.
   - `include/camel/runtime/program.h`: `Program` = table `GraphId -> GCGraph*` (+ root, + `original()`), bound while the
     encoder / a commit / a draft group builds it. `GCGraphManager` (src/runtime/program.cpp) now owns Programs and keeps the
     graphs of the installed and the detached ones alive and rooted; the rooting/freeing logic is the old one (graph-space
     sweeping and GC-managed snapshots are untouched, as asked).
   - The pipeline holds Programs: `GraphIRPass::apply(const Program*) -> const Program*`, `applyPasses*`, all passes (opt, macro,
     sched, trans, test, tensor, onnx, tiles), CLI, timeit, debugger driver, `bindEntryArguments`, `lowerLoopsToTailRecursion`.
     `Context` has `runtimeProgram()/installRuntimeProgram()/...` instead of the `GCGraph*` root API.
     `RuntimeGraphDraftSession` is opened over a Program, `commit()` returns the new Program (installed, or detached+tracked).
   - `::Function` holds `(const Program*, GraphId)`, `graph()` resolves, `equals` compares the GraphId, `print` uses the
     resolved name, `rebind()` replaces `setRuntimeGraph`. All 19 `Function::create` sites updated; commits clone static function
     values by id into the new Program; `FunctionData` likewise.
   - `GCGraph::originalGraph_` is replaced by `Program::original()` (a detached commit's Program remembers the Program it copied);
     `:graph/original` reads it.
4. `dc0e5911` Retire `stableId`, `lineage`, `serial`.
   - Parsed meaning is now explicit: `GCGraph::typeSpecializationRoot()` (std::opt's recursion bound; session counts keyed by
     GraphId; derived graphs inherit it, a `tspec` specialization sets it) and `kGraphFlagCarriesRule` (autodiff `carriesRule`).
     Both live in `GCGraphDebugRecord` and travel through `GraphDraft` (decode/encode).
   - Keys: fact store keys/stamps `(GraphId, revision)`; `DebugMap`, `girGraphs_`, `SemanticRecorder` (functionBuilt,
     functionReferenced, callResolved, valueBound), service analysis, `registerRuntimeGraphDebugInfo`, `node_attrs::graphRef`,
     compile-graph debug JSON, the debugger (JSON `graphId` = decimal text of the GraphId; node JSON field `stableId` renamed
     `nodeId`); `ExecutionSite.stableId` removed (nothing ever wrote it); the entry function's origin has its own DebugMap slot
     (`registerEntryOrigin`) instead of the string key `__entry__`.
   - Display names: anonymous graph `__g_<id>__`; derived graphs keep their `$spec3`/`$tspec3`/`.rec` name suffixes (display only).
   - Persisted facts: `PortableFact` anchors are the graph's name in the program + node; a repeated name gets `~<ordinal>` in
     GraphId order (`qualifiedNames` in fact_persist.cpp); the content hash uses names instead of stable ids.
   - `lineage` is the GraphId; `serial` became `GCGraph::revision()` (see question 1).
5. `33cfffd2` docs touch-up (39_loops).
   Docs updated in 4: technical 05, 10, 23_graphdraft, 27, 40 (graph facts), debugger API.md.

## Tests run (container lane, Release build)

Baseline (fsh-dev + commit 1) vs now, same selection:
- smoke, feat/modules/graph_facts, feat/compile, feat/opt, feat/trans, feat/tools (debugger + LSP), feat/modules/autodiff
  (+ inspection), tiles, definitions, resources, operator_semantics, index_semantics: baseline 1695 total / 1 fail (tiles
  `plan_validation`, environment/flaky); with the retirement commit: all pass (except the items under "Failures").
- `--tier fast` over smoke, feat/modules (all: autodiff, onnx, tensor, tiles, nn, ...), feat/compile, feat/opt, feat/trans,
  feat/tools, feat/linear: 2396 total, 3 fail (below). `--tier fast` over feat/linear, para, mm, consistency, parse, golden: 1868/1868.
- Last full re-run of smoke+tools+graph_facts+resources+trans+opt after the final edits: 624 pass, 0 fail, 10 skip.

Failures seen, all not caused by this branch:
- `registry.signatures_match` (stdlib_registry) and `fs.remove_permissions` (std): fail identically on the baseline build.
- `debugger.dap_pipeline` failed once under load ("stdout does not start with ok"), passes standalone (36 checks) and in reruns.
- `resnet_gradient_relu_kinks_vs_torch64` (autodiff plan run as a file, so its slow tier ran) failed once in 4 s under load;
  standalone it prints `gradient referee: PASS`. (I did not run the PyTorch tests on purpose afterwards.)
- tiles `plan_validation` fails on the baseline build, passed on mine: flaky/environment.

## Test expectations changed (all by design)

- `graph_facts.plan.toml`: `lineage_main kept` -> `id_main/id_f/id_root kept` (GraphId stable across commits);
  `serial_*` lines -> `revision_*` (same values: main new, f kept, root new); `unchanged_serial_main` -> `unchanged_revision_main`;
  `shared lineage` assertion -> `shared id`. The test pass prints the new lines.
- Goldens `smoke/golden/rgir.dot`, `feat/trans/golden/boundaries.{agir,argir}.dot`: the anonymous graph `__g_6__` is now `__g_5__`
  (the old counter was shared with the retired stable ids; ids are now 1, 2, 3, ... in creation order). Nothing else in them changed.
- `resources.plan.toml`: `__g_9__` -> `__g_7__`, `FUNC __g_7__` -> `FUNC __g_6__` (same reason).
- `test/cases/tools/debugger/http_gateway.test.mjs`: it asserted that `graphId` starts with `main`/`double` (stable ids began with
  the name); it now compares GraphIds (compiled graph id vs paused node graphId, `funcGraphId` of the call).
- Left as history: `test/cases/opt/progen/findings/simplify_recursion_never_ends.cml` and progen README F19 mention
  `__g_10__` in a recorded diagnostic.

## Questions / decisions that need the author

1. **`serial` could not simply disappear.** Facts must detect staleness per graph, and a commit still re-encodes every graph, so the
   stamp needs a per-graph revision (design 4.4 "version number"). I kept the mechanism (new number for a changed graph, kept for an
   unchanged one, transitive through references) and renamed it `GCGraph::revision()`. Keys use GraphId. If the intent was to drop
   revisions until step 3 (incremental commit) the facts would have to treat every commit as invalidating everything.
2. **Programs are never freed before the context** (`GCGraphManager::programs_`). A function value made in a replaced Program (a macro
   result kept across a round, a stale object the collector still visits) must not read a freed table; the old code relied on
   `graphSpace().isAllocated` for the graph, now the Program stays and the same check guards the graph. Cost: one small table per
   commit. GC-managed snapshots are the later step.
3. **Graphs created while the program runs are Programs of their own** (`makeProgram`: autodiff gradients, `attachRule`, tiles regions
   and `@gpu` dispatch; draft groups have `GraphDraftGroup::program()`), not part of the installed Program. They resolve their
   Function, but a pass that asks "the Program's graph for this GraphId" will not see them. The clean form is a commit that adds
   them to the program; I did not do it (it changes who owns those graphs and their lifetime). Also, `Function::graph()` is a table
   lookup on the call path now (vector index; no measurement was asked for).
4. **The same compile graph can be encoded twice** (operator definition graphs: `definitions.cpp`), which would put two graphs of one
   GraphId in two Programs of one context and collide in the fact store. `encodeToRuntimeProgram(..., &ctx.graphIds())` gives that
   encoding fresh ids. Consequence: the definition graphs' source origins are not found by GraphId (they were not registered for the
   pinned program before either). If "stable within a program" should hold for them, the allocation of ids at encode needs to move.
5. `GCGraphDebugRecord` now also carries `typeSpecializationRoot` and `flags` (semantic data in the "cold debug" struct, because it
   already travels draft -> graph on every encode). A header field would be the alternative; say if you want it.
6. The debugger keeps string-typed ids internally (decimal text of the GraphId). Converting its sets to `GraphId` is mechanical if wanted.
7. Pass signature change is source-breaking for out-of-tree modules (`apply(const Program*) -> const Program*`).

## Not done / left for the next steps

- Frozen reference tables and the 8 graph-reference accessors still hold `GCGraph*` (step A2); backends' `GCGraph*`-keyed tables, `Anchor`
  (`const GCGraph*, node`), draft references to other graphs (placeholder keys in `GraphDraftGroup`) are unchanged.
- No GC sweeping of graph space, no incremental commit; commit still re-encodes the whole closure.
- No performance measurement (not requested for this package).

## After acceptance (follow-up commits)

- `4c09cad1`: `typeSpecializationRoot` and the rule flag are header fields of `GCGraph` (`GCGraphDerivation` passed to create/constructInPlace), no longer in the debug record. `revision()` is per graph (a commit gives a new one only to an edited graph whose content changed); fact stamps and the footprint cache use `closureRevision(root)` (hash of root id + (GraphId, revision) of everything reachable). Pass signature paragraph in `docs/technical/04_compilation_pipeline.md`.
- `4089a188`: merge of fsh-dev (34558b7a). Conflicts in builder.cpp, debug_json.cpp, pass.cpp, test/fault.*, draft_session.cpp resolved keeping both sides (the overgrow test pass is gone, node entity ids of the debug JSON are gone: `nodeId` is `{graphId}:{ref}`).
- A2 items: graphs created at run time (gradients, rules, tiles regions) become a transaction on the current snapshot instead of a Program of their own; Programs are freed by the GC; the operator-definition double encoding disappears when the front end builds in the transaction.
- Merge test results: see the message to the lead (smoke, compile, opt, trans, linear, modules fast, tools, para: 3018/3032; the three failures are `fs.remove_permissions` (also on the old base), `declared_math_numeric_semantics` (math:abs_c not in the test's list) and `grammar.tables`, both from fsh-dev content).

# A2 part 1: graph references by GraphId

Commits after the merge `4089a188`: `57190ef7` (code; atomic, the type of the reference table changes under ~100 files), `c96b57c6` (docs technical 16, 22). Not pushed.

## What changed

- `GCGraph::refs()` is `span<const GraphId>`; `GCFuncBody::calleeGraph`, `GCLoopBody::condGraph/bodyGraph`, `outerGraphId()` are `GraphId`. Accessors: `calleeIdOf/loopCondIdOf/loopBodyIdOf/refs/dependencies/subGraphs/staticGraphRefs/outerGraphId`; resolution in `Program` (`graph`, `calleeOf`, `loopCondOf`, `loopBodyOf`, `outerOf`, `dependencies`, `subGraphs`, `staticGraphRefs`, `traceGraphs`).
- Form chosen: callers take the Program (no back pointer in the graph: part 2 shares a version between snapshots). reachable, effects, loops, resources, facts (`Anchor{program, graph, node}`), validation, drafts (`GraphDraft::decode(program, g)`, `draft.program()`), FastVM `compile*(ctx, program, ...)`, printers, debugger, ONNX, Tiles, autodiff all take a Program.
- C1b, deviation on purpose: draft FUNC/LOOP payloads hold `GraphId`, not table indices. A draft has no table; it is built at encode from the ids the nodes and static values name (`collectDraftDependencyGraphs/StaticGraphRefs`). Indices would need a table kept in step with every addFuncNode/rewriteNode/clone/inline/specialize and the front-end builder's placeholders. Frozen graphs still store the index (`GCFuncRef`, `GCLoopRef`). The node record (`GCNode`, `GCFuncRef`, `GCLoopRef`) is untouched; only the draft payload structs and the draft API changed.
- `RuntimeGraphDraftSession` has a working Program `work()` (copy + `adoptProgram`/`adoptGraph` for function values of other programs and for specializations); `commit()` binds the closure into the new Program; static-function clone rule: `committed has the id ? committed : the function's own program`; `sameContent` compares ids. `GraphDraftGroup(context, baseProgram)` reserves `GraphId`s, `adopt()` takes what a session made on the way. This is the minimal `draft_session.cpp` change; the commit/closure walk is the same with ids (mechanical merge with scans).
- GC: a graph no longer traces its callees; reachability is by Program. Programs made outside a commit (`makeProgram`, group programs) are merged copies (base + new graphs) and their graphs are `GCGraphManager::retain`ed for the context's life.
- Back ends: `execute::RunPrograms` = program of the run + programs of the function values called through (`graphOf(function)` notes it); `of(graph)` throws for a graph none of them holds. NodeVM, Taskflow, FastVM/JIT, macro evaluator (`program_`), C++ emitter (`ProgramParts::program`), ONNX, Tiles (`Compiler::runtimeProgram`, switched per function value), autodiff (`Engine::graphOf` adopts the program of a function value, e.g. a nested grad).
- `Context::programOfRuntimeGraph(graph)` = the newest program holding that exact version; used only where a tool gets a bare graph and shows the latest state (JSON gir, stats pass-dump hook, `dumpDot`, debugger index, ONNX export). Execution and rewriting always have the program explicitly.

## Backend caches keyed by graph version: what invalidates them

A version is shared by the program before and after a commit only when the commit found it unchanged (`sameContent`); it then resolves its callee ids in the new program. Today sound because:
- NodeVM `graphCaches_` (and its extra slot): cleared at `apply`, built per run with `programs_.of(graph)`; valid for one run = one main program.
- FastVM `offsetMap_`/`jitFnToGraph_`/entry-pc slots: `precompile` recompiles every graph reachable from the program given to `apply` and overwrites the slots; `compileLate` uses `programs_.of(graph)`; bytecode embeds callee `GCGraph*` resolved at compile time, valid for that program only.
- Taskflow `plans_`: cleared per `apply` (`planEpoch_`); lowered copies note their Program.
- Hazard for part 2: the `extraSlot`s on graph objects hold data derived for one program but live on a version two snapshots can share. Part 2 must key them by (version, Program) or, recommended, move them into a per-run table of the back end indexed by (activation program, GraphId). No dangling hazard from this commit: versions die only with the context or by `releaseDetached`, as before.

## Tests

`--tier fast`, container lane, after the final commit: smoke, feat/compile, opt, trans, linear, modules, tools, para, mm, consistency, golden, gpu: 4136 total, 3 fail (`fs.remove_permissions`, `declared_math_numeric_semantics`, `grammar.tables`: known), 11 skip. No test expectation changed. feat/examples + feat/exemplars: 6 failures that also fail with the gs-planbase binary (current fsh-dev): `examples.parallel.parallel_map.fvm/.jit`, exemplars 01, 02, 05, 08. Cause (not this change): in FastVM/JIT a tuple-valued `reduce` whose function compares fields of both arguments, `xs->reduce<(a: (int,int), b: (int,int)): (int,int) => if b.1 > a.1 then b else a, (0, 5)>` over `[(1,30),(2,6),(3,20)]`, returns `(3,20)`; NodeVM and Taskflow give `(1,30)`. Same bytecode as an older build that is right, so it is in the marked-reduce / element-view path of the interpreter.

## Decisions / open points

1. Draft payload ids instead of table indices (above).
2. `RunPrograms::of` is strict: a path that turns a function value into a graph without `graphOf(function)` fails with "belongs to none of the programs the back end knows". All NodeVM/Taskflow/FastVM/JIT sites are covered.
3. Debugger/JSON/stats use the newest-program lookup; part 2 should give them the snapshot they inspect.
4. Not done (as agreed): incremental commit, program-level reference index, GC-managed snapshots, graph-space sweeping, runtime-created graphs as a transaction (today merged-copy Programs retained for the context's life), operator-definition double encoding (`DefinitionGraph::program` carries the pinned Program; the expansion adopts it into the session).

# A2 part 2: incremental commit

After merging fsh-dev (cbfd88be; wide's C1b made draft payloads reference-table indices, `21d2ff31` re-bases that on GraphIds: `GraphDraft::payloadGraphs()` holds `GraphId`s, `calleeIdOf`/`calleeGraphOf`/`setCalleeGraph(GraphId)`/`loopGraphsOf`/`setLoopGraphs`; `GCFuncBody` is gone, so my C1b deviation is moot). Commits `87b3b3b5` (code), `80b6cbc0` (docs 16, 17). Not pushed.

## What a commit does now

- New Program = every graph of the closure at its current version; a graph gets a new version only if (a) it was changed (`GraphDraft::mutations() > 0`: every non-const member counts; dissolving gates counts only when it removes something), (b) the program did not hold it (specializations, adopted function values), or (c) it holds a function value of a graph that reaches a new version. Unchanged edited graphs and graphs encoded back to the same content (and holding no function values) are left as they were (fresh copy freed with `GCGraphManager::release`).
- (c) is the cost of `Function(Program, GraphId)`: a function value runs what its program holds of everything the function calls, and the old program's versions are freed when it is replaced. Found by a crash (grad of a function whose callee changed in a later commit resolved the callee in the old program). Computed backwards over the callee and static roles of the closure's reference tables (CSR arrays by id, one pass per commit that replaced something); only graphs with static refs are ever re-encoded for this.
- `kGraphFlagGatesDissolved` (graph flag): a commit that looked at a graph's gates marks it (in place if it did not re-encode); unmarked/drafted graphs are decoded for the gate pass, marked ones are not.
- Satellite programs (`makeProgram`, draft groups) own only what they add (`Program::owns`, `bindOwn`, `adoptMissing`); a session adopts a foreign program's own graphs by table index. Adopting by `graph->id()` read freed graphs of stale base copies (a stale pointer whose block had been reused showed the id of a new graph).
- `releaseDetached` frees a graph shared by several programs once (a double free in ONNX export).
- Closure walk (`collectCommitClosure`): marks by id, reads the frozen `refs()` directly, drafted graphs through the cached `draftRefs`; the last closure is reused while the session's refs epoch (advanced by every `touchRefs()` of any draft), the work table version and the draft count are unchanged.

## Measured (container, `ab.py`, A = merge before this change, B = after; 3 pairs)

| program | mode | A ms | B ms | B/A | noise |
|---|---|---|---|---|---|
| scale.1k | opt | 75 | 67 | 0.89 | 1% |
| scale.10k | optinline | 217 | 117 | 0.54 | 9.5% |
| scale.10k | opt | 316 | 184 | 0.58 | 6% |
| scale.50k | optinline | 1229 | 457 | 0.37 | 5% |
| scale.50k | opt | 1776 | 664 | 0.37 | 5% |

compile (front end only) is unchanged (570 ms at 50k; 10k reads 1.15 once, within what that row shows between runs). Output and `std::stats` of the 10k and 50k programs are identical before and after. In the 50k optinline run: 7 commits took 513 ms before and 125 ms after (they re-encoded 7108 graphs twice, once for the final pass that asks to edit every graph, to find them unchanged); the 1408 closure collections took 300 ms, now 147 ms.

## Tests

fast tier, same selection as before plus feat/examples and feat/exemplars: 4379 total, 8 fail = the baseline set (`fs.remove_permissions`, `grammar.tables`, the FastVM/JIT tuple-reduce bug: parallel_map fvm/jit and exemplars 01/02/05/08). No expectation changed.

## What is left (size-proportional costs)

- The closure walk is still ~30% of `std::opt::inline` at 50k (about 0.1 ms per collection, 1408 collections): the engine collects it after every inline, and an inline changes a draft's references, so the cached closure is rarely valid. The next step is a reverse reference index kept with the session (callers per graph, updated from the drafts that changed) so that additions extend the closure and a removal checks reachability backwards from the dropped graph; the BFS order the engine sees would change with it, which needs to be checked against the goldens (inline decisions and budget use depend on order).
- `GCGraphManager::replaceRoot` builds the keep set from `program->graphs()` (O(graphs) per install).
- `RuntimeGraphDraftSession`'s `work_` is copied from the program after each commit (O(table)).

Follow-up (`99bfd58c`): the closure walk keeps a queue of ids and a cached id list per frozen graph; order is exactly the BFS order (sorting the closure by id changes std::gir: specialization names, inline decisions), `std::gir` byte-identical on the opt cases and the 10k/50k scale programs. replaceRoot / work_ copy: 14 ms of 125 ms over 7 commits at 50k, left alone.

# A2 part 3: the graph space is garbage collected (`0fa7bffb`)

- `Program` is an `Object` of the graph space (`Program::updateRefs` traces its graphs and the program it copies); `Function::updateRefs` traces its Program (slot `program`; only if the Program block is still allocated); `FramePool` traces the graph a frame runs. `GCGraphManager` roots: installed program, detached programs (`trackDetached`), pinned programs, programs made since the last `settle(keep)` (the pass driver settles after each pass, keeping the program the pass handed on), the program of a running back end (`RunPrograms::begin` / `RunScope` in NodeVM, FastVM, Taskflow, tfdump; roots it and switches the explicit collection off). Registry: `programs_`, `graphs_` (graphs bound into a manager-owned Program). `owned_`, `retained_`, the eager frees in `replaceRoot` / `releaseDetached` / `clear` and `Context::retainRuntimeGraphs` are gone.
- Two collections, one sweep (`freeDead`): `collect()` between passes (marks from the roots over Programs and the static values of the graphs; the heap is not looked at, correct while no program runs; runs on install / `releaseDetached` / `settle` once the graph space grew by a quarter and 1 MB, always on `clear()`), and the run-time collector: `GenerationalAllocatorWithGC::setGraphSweeper` (one hook after `markPhase` in `majorGCUnlocked`; the manager frees unmarked Programs and registered graphs) plus `requestMajorCollection` when the graph space holds as much again as after the last sweep (`CAMEL_GRAPH_GC_MB`, default 64). gen.cpp edits are those two hooks and the `program` slot name in `relocateForMinor`; the data-block accounting and deferred-release code are untouched; freeing a graph runs no foreign callbacks (C++ destructors of the graph, its debug record and its use index, and block frees).
- Runtime graph creation (grad, rules, tiles regions): `makeProgram` / `GraphDraftGroup` build a Program on top of the program they were made over (merged copy, ids resolve), rooted by its function values only. Not provisional while a program runs. The 4000-gradient loop (`feat/mm/graph_space.plan.toml`): graph space high-water mark 12.9 MB -> 1.0 MB with `CAMEL_GRAPH_GC_MB=1`.
- Commits: a graph left as it was gets its function values retargeted to the new program (`retargetFunctions`, in place on the Function objects when the new program holds the same version), otherwise every program of a commit chain stayed reachable through shared graphs' static values and kept its stale versions.
- **Chunks keep `shares`** (answer to the question): `GCNodeChunk` is a plain block without a virtual table, no tracer visits it, and it is reachable exactly when a live graph version holds it; freeing a graph gives up its share, the last share frees the block. Making chunks objects would add a vtable word per chunk and a trace of every chunk of every live graph on each collection for the same result. Documented in technical 23.
- Limits worth knowing: (1) the explicit collection ignores heap function values, so a pass must not keep a function value of a replaced program on the heap across a commit (as before: it would find its graph freed); (2) graphs a session encoded and no commit bound (specializations replaced by a commit's copy) are not registered and live until the context ends; (3) FastVM's bytecode vector keeps the bytecode of a freed late-compiled graph (its capacity is reserved up front), (4) `RunPrograms::of` skips noted programs the collector freed.
- Measured (container, A = `99bfd58c` build): fib.deep 0.990x, alloc_small 0.968x, map_chain 1.018x (noise 9%), build_tree 0.940x (CPU time, 7 pairs, noise 1.5-9%); std::opt on 50k 729 -> 543 ms and std::opt::inline 476 -> 411 ms (4.3-8.6% noise); 10k within noise. Tests: fast tier over smoke, compile, opt, trans, linear, modules, tools, para, mm, consistency, golden, gpu, examples, exemplars 4380 total, 8 fail = the known set. Slow GC plans (gc, gc_collections, gc_pacing, gc_poison_sweep, gc_elder_exhaustion, gc_soak) + autodiff + tiles with `--jobs 6`: 16 fail, all failing on the `99bfd58c` binary too (the 26 failures seen at default jobs are 60 s timeouts under load).

## Part 3 follow-up: the two limits closed, fsh-dev 9cfb379e merged

- Explicit collection (`GCGraphManager::collect`) now has the heap marked (`collectGraphSpace`: markPhase + graph sweeper, no sweep or move of the auto space), so a function value keeps its Program between passes. Test `graph_space.function_value_keeps_program` (pass `test::keepfn`: program, function value of its graph, replacing program, collect, read through the function value).
- FastVM bytecode of run-time graphs: the plan of a version holds a `CodeLease`; `CodeSpace` keeps free ranges; `compileLate` places new code in a free range (measured by a probe compile when one exists). Test `graph_space.grad_in_a_loop_fvm`: `fvm_code_bytecodes` 11269 for 4000 gradients (all kept would be above 140000).
- Merge conflicts: graph_runtime_support.cpp (both includes), nodevm.cpp (RunScope replaces setMain). Fast tier: the same 8 known failures (4 exemplars, parallel_map fvm/jit, fs.remove_permissions, grammar.tables); graph_space plan 3/3.
- Debt left: encoded-but-unbound graphs live to context end; `jitFnToGraph_` entries of freed graphs stay.

## Step 7: values with several writings (plan: scratchpad gs-egraph-plan.md; doc: docs/technical/44)

Commits (7.0 to 7.8): 1c86bcac state flag and gates; 6c8cb9be WritingStore; 48223419 congruence;
e68680bc graph column, encode/decode, sameContent, validator, mutator guard; bbff687e extraction;
7465291d driver extracts before consumers; 70e1e7d3 rules, e-matching, saturation; 10823f9e
classFact; 18a5ac9b docs; 25e571e6 merge of fsh-dev 416c4779.
Tests: test/plans/feat/mm/writings.plan.toml, 16 tests (union-find, congruence, chain, apart,
commit round trip, chunk sharing, extract, driver, algebra saturate default/safe/deterministic/
budget, facts). Fast tier on the head: 4518 tests, 3 fail (fs.remove_permissions, grammar.tables,
frontend.ast_snapshot; the last appeared with the fsh-dev merge b29afa66 and is not touched by
this work).
Perf: fib.deep, alloc_small, map_chain, build_tree against the build of 681ac65a: flat within
noise (load average 13 during the runs; alloc_small min 320 vs 344 ms, medians equal);
std::opt::inline on the 50k program 1.85 s vs 1.91 s.
Known differences / open points: nodes with control inputs take no part (shared_product keeps its
sum); matching scans all nodes per rule per round (a head index is the next step); only count
budgets; the driver extracts with cost model "weighted"; a pass that does not declare
acceptsWritings gets PassInternalError text (a dedicated RuntimeDiag is the lead's call).
Debts still listed: encoded graphs bound to no program live to the context end;
jitFnToGraph_ entries of freed graphs; node-level fact stamps.

## Step 8: rules (plan: scratchpad gs-rules-plan.md; doc: docs/technical/45)

Commits: d1269f9f compileRule; 8ec60cb5 greedy rewrite; ccfe66e4 syntax (`<=>`, ruleDecl, formatter);
30e07b70 rule = pair of functions + the function that makes the Rule (module rewrite);
8ac4dc6f/b831790e rewrite(p, rules); 7ce67fbb saturate/extract; d4cc5ca5 check; dc03e236 docs;
b6af63a7 standard rules (stdlib/rules.cml, 26 rules, all pass check).
Tests: writings.plan.toml 20, parse/rules.plan.toml 7, modules/rules.plan.toml 14. Fast tier: only
the 3 known failures (ast_snapshot, fs.remove_permissions, grammar.tables; checked on gs-scans).
Decisions: a rule is a function, not a module `let`: library programs (Camel passes) do not
evaluate module-level values (`let two = 2` reads 0 in a pass module; camel_pass.cpp not touched).
Open: `when` (needs Node handles over drafts; waiting for the lead), `#requires` (H2), analyses
(8.7), the std::opt::algebra replacement (needs the two above), Windows export list for the module
rewrite (symbols: NodeVMSchedPass::callGraph, compileRule/RuleCompileError, DraftWritings,
findCostModel, extractWritings, SymbolTable::instance).
Debts (unchanged): encoded graphs bound to no program; jitFnToGraph_ entries of freed graphs;
node-level fact stamps.

# HAND-OVER BRIEF (for the implementation team)

Branch gs/identity, merged with origin/fsh-dev 7d08510e (head 103176f7; this file is not committed).
Fast tier on that head: 4568 tests, 3 failures, all pre-existing on a base without my changes
(checked on the gs-scans lane): frontend.ast_snapshot (two AST hashes, convert_fits.cml and
number_text.cml, from the number-grammar change H15e: `check.py snapshot --update` once that lane
accepts the new reading), grammar.tables (editor grammar lacks the types c64 and c128; the `<=>`
token I added is in it), fs.remove_permissions. Two debugger plans (dap_pipeline, dap_memory) failed
once under load and pass alone.

Note: after the lead's "stop" message I had already committed the exact-rules library (b6af63a7);
it is finished and tested, keep or drop it as you like.

## A. Step 8 (rules): where it is

Read first: scratchpad gs-rules-plan.md (the plan), docs/technical/45_rules.md (what is built),
docs/technical/44_values_with_several_writings.md (the engines underneath).

Done (files):
- runtime/rules.{h,cpp}: `compileRule(RuleSource)` turns the two functions of a rule into the
  engine's `Rule`s (`const` params, computed constants, refusals with names).
- runtime/writings.{h,cpp}: `DraftWritings::rewrite` (greedy), `saturate`, `extract`, cost models,
  `classFact`.
- Front end: lexer `<=>` (Equiv); parser_rule.cpp (`rule` / `when` are contextual names, tested:
  `let rule = 3` still works); node_kinds.def RuleDecl/RuleContext/RuleBody; AST builder
  `visitRuleDecl` desugars to `name__lhs`, `name__rhs`, and `func name() => __rule(...)`, with the
  implicit import of module rewrite; tools/format/fmt.cpp prints rules; check-grammar, the language
  reference (02, 08, both languages) and the editor grammar know `<=>`.
- modules/rewrite (new): type `Rule`, `RuleObject`, operators `__rule`, `rewrite`, `saturate`,
  `extract`, `check`, readers `rule_name/two_way/needs/consts`.
- stdlib/rules.cml: 26 exact rules + passes `simplify`, `saturate_all`.
- Tests: test/plans/feat/mm/writings.plan.toml (rules.*, saturate.*), parse/rules.plan.toml,
  modules/rules.plan.toml; cases under test/cases/{parse,modules}/rules and feat/mm/writings.

Not done (8.4 to 8.9 of the plan), with the lead's answers:
1. `when` (8.4): variables are `graph::Node` handles (explicit; use `type_of`). Option (a): a
   draft-backed Node handle evaluated inside module rewrite; you extend the graph module's `Handle`
   with a draft reference. Pitfall: during `rewrite` the matched node may be one an earlier rewrite
   of the same pass created, which is not in the base Program; and a Camel call can move objects
   (see pitfalls below). The grammar already parses `when`; `visitRuleDecl` refuses it with
   ConstructNotSupported. `Rule::when` (std::function<bool(const Match&)>) is the engine hook.
2. Permissions (`#requires<...>`): no interim `@requires`. H2 (tags) is being moved forward. Until
   then every rule has needs = 0 (`__rule`'s third argument is 0 in visitRuleDecl).
3. Generic rules (`typeas T` + `where`) wait for `where` (ruling 2.18) and generics phase 1; v1 is
   monomorphic (the library has int = i64 and float = f64 only; i32 / f32 not yet).
4. Analyses (8.7): `analysis name: T { initial, join, widen, on(...) }`; plain-data fact types
   first. Plan section 5. `classFact` is the join over writings; the fact store (runtime/facts.h)
   needs a kind per analysis. Not started.
5. Standard library (8.8): the exact part exists; the permission and `when` parts wait for 1 and 2.
   At parity, retire src/passes/opt/generic/algebra.cpp (and the std::opt::algebra parameters);
   neutral.cml stays as a graph-API test. Parity target: the opt.algebra plan and the saturate.*
   tests (they hold the expected operator counts per corpus function, default and safe policy).
6. Strategies module (seq, fixpoint, try, first, within) and the generic form `rewrite<rules>`: the
   natives take (p, rules[, rounds]) today. `within` needs a node-set filter in the engine (the
   candidate list in DraftWritings::rewrite / saturate is the place).
7. Numeric tolerances for `check`: from each operator's declared numeric contract (OperatorTable
   NumericSemantics), not my bound `64 (steps+1) eps (|a|+|b|+scale)`.
8. Provenance attribute `rule` on nodes a rule makes (node_attrs.h key, merge rule: keep the
   matched node's). Not done.
9. `<=>` given to greedy rewrite is a runtime error value `RuleTwoWay` (done); a compile-time error
   when the rule list is statically known is not done.
10. Windows: module rewrite uses libcamel symbols that need export entries (callGraph, compileRule,
    DraftWritings, findCostModel, extractWritings, SymbolTable::instance); your automation is
    expected to cover them.

### The silent wrong value (file as a top-priority defect, H37)
In a library / pass program a module-level value reads 0. Reproduction, run as
`camel tgt.cml gl::apply std::nvm`:

    // gl.cml
    module gl
    import { Program } from graph
    let two = 2
    func ten(): int { return 10 }
    let twelve = two + ten()
    export func apply(p: Program): Program! sync { println(two, twelve); return p }

    // tgt.cml
    module tgt
    func main(): int sync { println(1) return 0 }

It prints `0 0` (and garbage such as `130` after `std::opt`). Cause: DefinitionStore::libraryFunction
(src/runtime/definitions.cpp) encodes only the function graph and its callees
(encodeToRuntimeProgram(function)); the module's root graph, where buildTopLevelValue puts the
`let`s, is not part of the library program, and camel_pass (src/execute/camel_pass.cpp) never runs
it. Calling `callGraph(tool->program, tool->program->root(), {})` first crashes (the root of a
library program is the function, not `__root__`). That is why a rule is a function, not a `let`.

### Pitfalls found in step 8
- A kernel that calls Camel code (NodeVMSchedPass::callGraph) passes safepoints: read arrays and
  objects from the ArgsView again after each call and copy what you need from an object at once;
  CAMEL_GC_POISON found raw pointers (the poison sweep tests will catch it).
- `__rule` is declared impure in the operator table: folding it built the rule at compile time in a
  static area where the functions it holds were not traced.
- A list of rules is `(() => Rule)[]`; the resolver checks the element is a FunctionType with no
  parameters returning RuleType. The Program type is told by name (`toString() == "Program"`) to
  avoid linking the graph module.
- `rewrite` and `saturate` skip graphs named `*__lhs` / `*__rhs`; changing the naming breaks that.
- visitDecl may return `std::vector<node_ptr_t>` (several statements); visitRuleDecl uses it.
- camel-format is CST-based (tools/format/fmt.cpp); the AST generator under src/service/codegen is
  legacy for this purpose.

## B. Store debts: what to know

General invariants of the graph store as built (steps 1 to 7):
- Function value = (Program, GraphId). A Function runs what ITS program holds of everything it
  calls. After a commit that leaves a graph as it was, `retargetFunctions(graph, committed)`
  (runtime/program.cpp) rebinds the function values in that graph's static area to the new program,
  otherwise old programs stay reachable through them (found as a leak that kept whole commit
  chains). Graphs that hold function values of graphs that reach a new version must themselves get a
  fresh version in the commit (draft_session.cpp commit, reverse reachability over Callee|Static).
- The closure walk of a session is a BFS over ids and its ORDER matters: rewrite results depend on
  it (verified by std::gir diffs); do not sort by id.
- Every non-const GraphDraft member bumps `mutations_`; commit only re-encodes graphs with
  mutations_ > 0 and drops edited-but-sameContent copies. A new mutator MUST bump it, and while a
  draft has several writings it must be guarded by requireOneWriting (draft.cpp, draft_construct.cpp).
- Never adopt a satellite program's entries through `graph->id()` of possibly stale base pointers:
  use table indices (`Program::owns / adoptMissing`); a stale pointer read freed memory once.
- Graph versions shared by several detached programs must be freed once (the kept set in release).
- Graph-space GC: Program is an Object in graph space; roots are current_, detached_, pinned_,
  provisional_ and run roots; explicit `collect()` marks through the heap
  (GenerationalAllocatorWithGC::collectGraphSpace), `sweepMarked()` runs in the major GC. Node
  chunks keep `shares` counts (plain blocks, no virtual table). Graph-space growth asks for a major
  GC (CAMEL_GRAPH_GC_MB floor, default 64; the test plan sets 1).
- FastVM bytecode of freed graph versions is returned through a CodeLease held in the ExecPlan
  (CodeSpace in fastvm.h); compileLate reuses a free range that fits.

Debts, each with where to start:
1. Merging non-conflicting stale commits into the newest snapshot (design 4.3.1): today a commit
   whose base is stale gets GraphCommitConflict from RuntimeGraphDraftSession::commitChecked
   (draft_session.cpp). Needed: per-graph read/write sets recorded by the session, and on commit
   "replace the changed graphs in the newest snapshot" (compare-and-swap on the installed program),
   re-validating callers whose callee signatures changed. Pitfalls: function-value re-versioning
   (above); draftedIds_ / closureCache_ validity (keyed on refs epoch, work_ version, draft count,
   root); the committed Program must bind all closure graphs at their current versions.
2. Encoded-but-unbound graphs live to the end of the context (a graph a session encoded and a
   commit did not bind is not registered with GCGraphManager and is never freed). Fix: register at
   encode (noteGraph) and let the sweep free what no Program binds. Pitfall: encode happens before
   Program::bind, and releaseRuntimeGraph is called for dropped copies (the sameContent path).
3. `jitFnToGraph_` (fastvm.h) keeps entries of freed graphs (JIT function pointer -> GCGraph*);
   clear them when the version is freed (the CodeLease destructor is the hook point) and beware of
   address reuse of freed graphs (see the comment in compileLate).
4. Node-level fact stamps: facts are stamped with the graph `revision()` (facts.h), so any change to
   a graph, including an added equivalence, invalidates all its facts. Needed: "each node records
   the version it was last changed in" (design 4.4) and stamps by node; early cutoff.
5. Chunk-backed draft storage for proportional decode: GraphDraft::decode copies every node of a
   graph (O(n)) even for a one-node edit; nodes live in 256-node chunks shared between versions
   (GCNodeChunk, `shares`). Make the draft hold the base's chunks lazily (copy on write per chunk)
   so decode and encode are proportional to what changed. Pitfalls: draft ids vs chunk numbering
   (keepsNodeIds, tombstones); user lists (normUsersOf) are built per decode; the encoder renumbers
   when ids are not kept; the writings log maps node ids.

Measured references: std::opt::inline on the 50k program 1.85 s; saturation of a 3n-node function
(scale tool wide_program) n = 10000 / 40000 / 80000: 89 / 471 / 1008 ms; mm.graph_space: 4000
gradients keep graph_peak < 6 MB and FastVM bytecode < 40000 entries.
