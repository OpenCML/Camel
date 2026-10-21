# GS report: package "wide" (32-bit ids)

Branch `gs/wide` (worktree `Camel-gs-wide`, base `fsh-dev` 79060dd7). Builds and tests ran in the container `camel-lanes` (lane `gs-wide`, a mirror of the worktree at `lanes/gs-wide`, build tree `/work/lanes/gs-wide`).

## Commits

1. `bd7378c3` fix(build): name the dependent template call in the loop driver. The base does not build with Clang 19 without it (`state->template get<...>`). `gs/identity` has the same one-line fix (486b11ef), so the merge is trivial.
2. `075165d6` Frame slots and FastVM/JIT operands are 32 bits.
   - `data_idx_t` is `int32_t` in core operator, gir types and FastVM; `arr_size_t` (unused) removed.
   - FastVM bytecode: header is 16 bytes (opcode, reserved, `uint16 opsize`, 32-bit result, two 32-bit operands); operands 4 bytes each, padded to 16; extra words stay 8 bytes and end where the instruction ends (so FUNC/TAIL still reads the same words, with no wasted unit for odd counts); a pc counts 16-byte units, so jump tables, BRCH arm jumps and the optimizer keep working unchanged. A linked JUMP spreads its target over the two 32-bit operands. `as_index` limit is now the int32 range; `opsize` overflow is a checked error.
   - JIT: `VRegId`, `Mir::r0/r1` are 32 bits (`kInvalidVReg` = 0xFFFFFFFF). NodeVM and Taskflow only use `gc_data_idx_t`, so they needed no edit.
3. `b861b0d8` Runtime graph ids are 32 bits; the capacity limit is gone.
   - `gc_node_ref_t`, `gc_slot_idx_t`, `gc_off_t`, `gc_cnt_t` are 32 bits. `GCNode` is 28 bytes (`kind`, `flags`, explicit `uint16 reserved`, `dataIndex`, `typeId`, `inBegin`, `withBegin`, `ctrlBegin`, `payload`). `GCFillBody::slotCount` is 32 bits (`reserved1` takes the old slot), `GCJoinBody` lost its reserved word (8 bytes), `GCBrchBody` is 16 bytes. Draft `payloadBytes/capacityBytes` are 32 bits (they used to clamp at 65535 silently).
   - Layout: the node array is padded to 8 bytes in `planGraphArenaLayout` and `allocPayloadInArena` (28 x (n+1) is not a multiple of 8); `planDraftPayload` / `describeDraftPayload` / `emitDraftPayload` no longer narrow; the reserved ids (`kInvalidNodeRef`, `kNoPayload`) are an `ASSERT`, not a diagnostic.
   - Deleted: `include/camel/runtime/capacity.h` (`GraphCapacityError`), `RuntimeDiag::GraphTooLarge` and its catalogue entry, the catch sites in `pass.cpp`, `graph_encode.cpp`, `draft.cpp`, `draft_session.cpp` (`namedDraftGraphBytes`), the ONNX exporter's "does not fit one graph" refusal and `unendedUnrollingHint`, the test-only pass `test::overgrow_graph`, rows in `docs/cli.md` / `docs/cli.cn.md`, `docs/diagnostics.md` regenerated (`node scripts/diagnostics-catalog.mjs --write`), `docs/technical/22_gcgraph_native_layout.md` section 5 and the `GCNode` listing rewritten.
4. `009c7dcb` Test: `python test/tools/scale/scale.py wide` (plan `feat/mm/capacity`, test `capacity.wide_graph`): one function of 30000 terms `p<i%100> + i` summed as a balanced tree = 90000 nodes, 60000 frame slots; runs on nvm, fvm, jit and prints the expected sum. 0.8 s and 335 MB each.
5. `9c69c835` Zero the new `reserved` field of a frozen node. Found by `graph_facts` `rewrite_opt`: `sameContent` memcmp's the node array, and the uninitialized padding made unchanged graphs look edited.

## Tests (all in the container, Release build)

- smoke; `feat/compile`, `feat/opt`, `feat/linear` (includes the jit/fvm plans), `feat/trans`, `feat/para`, `feat/mm/capacity`, `feat/mm/buffers`: 2116 / 2116 pass (last run, after commit 5).
- `feat/modules` (everything: onnx, tensor, autodiff, graph_facts, nn, ...), `feat/consistency`, `feat/tools`: 1895 total, 1882 pass, 11 skip, 2 fail. The two failures, `fs.remove_permissions` and `registry.signatures_match`, fail identically on the base build (`A`), so they are environmental (container, running as root / signature files), not from this package.
- Not run (as instructed): slow tier, GC stress, poison sweep, PyTorch comparison.

## Changed test expectations

- Removed `graph_too_large.diagnostic`, `graph_too_large.diagnostic_json` (`feat/opt/generic`) and `pass_export_refusal_located.graph_too_large(_json)`, `export_model_graph_too_large` (`feat/modules/onnx`) with `test/cases/modules/onnx/refusals/graph_too_large.cml` and its line in `feat/parse/golden/ast_hashes.txt`. Design change: the limit they asserted no longer exists. `test/cases/opt/generic/broken_graph.cml` stays (other tests use it).
- Added `capacity.wide_graph`.
- No golden or other expectation changed.

## Performance (red line)

Method: three builds in one container, Release, `--timeit std::<backend>/5`, 7 rounds interleaved with the order rotated each round, median of the means. A = fsh-dev + the build fix; N = A plus a never-called function in `gotovm.cpp` (noise floor); B = this branch. The machine was loaded by other agents (load ~9), so individual samples are noisy. Peak memory is `ru_maxrss` of the process (Linux, not the Windows counter the baseline script uses).

Execution time, ms (median; samples differ by a few percent):

| program | backend | A | N | B |
|---|---|---|---|---|
| fib.deep | nvm | 392.5 | 387.9 | 395.6 |
| fib.deep | fvm | 93.7 | 91.9 | 92.4 |
| fib.deep | jit | 108.5 | 107.1 | 111.0 |
| alloc_small | nvm | 213.5 | 209.6 | 214.0 |
| alloc_small | fvm | 114.5 | 114.4 | 113.3 |
| alloc_small | jit | 220.7 | 254.8 | 275.3 |
| map_chain | nvm | 51.4 | 49.3 | 43.6 |
| map_chain | fvm | 30.4 | 30.9 | 29.1 |
| map_chain | jit | 28.2 | 29.8 | 23.5 |

`alloc_small jit` is bimodal on this machine (about 112 ms or 200+ ms). A 15-round rerun: medians A 119.9 / N 186.2 / B 171.4, minima 114.6 / 112.6 / 113.5. B is within the noise floor everywhere (N differs from A by up to the same amount as B does). `fib.deep` is the sensitive one: A to B is +0.8% nvm, -1.4% fvm, +2.3% jit, against a noise floor of -1.2% / -1.9% / -1.2%. So the FastVM header growing to 16 bytes did not show up.

Memory and size (`std::stats`):

| program | nodes | bytes/node A | bytes/node B | compile peak A / B | fvm peak A / B |
|---|---|---|---|---|---|
| fib.deep | 31 | 26.1 | 40.6 | 12.4 / 12.4 MB | 13.0 / 13.1 MB |
| alloc_small | 69 | 25.3 | 39.9 | 13.1 / 13.2 MB | 17.1 / 17.2 MB |
| map_chain | 75 | 24.2 | 38.5 | 12.7 / 12.7 MB | 16.8 / 16.9 MB |

Peak process memory is unchanged to 0.1 MB. **Bytes per node rise 55-60%, not the 20-25% in the design (4.1)**: the record grows from 16 to 28 bytes (+75%) and every edge from 2 to 4 bytes. See the question below.

## Findings and questions

1. **Bytes per node +55-60% against the design's 20-25%.** The estimate seems to count only the edges. If the 20-25% matters, the record can be 20-24 bytes: `withBegin`/`ctrlBegin` as 16-bit offsets from `inBegin` would cap a node at 65535 norm inputs, which contradicts "no limit"; a full-width alternative is to store only `inBegin` and the Norm/With counts in one 32-bit word each... Not done; the record layout is a design decision. My suggestion: accept 28 bytes now (execution time and process peak are unaffected) and revisit when the chunked columns replace the array-of-records layout (step 2, second half), where the three offsets can leave the record.
2. **A node with many users makes the draft quadratic in memory.** `appendUserRef` rebuilds the user tail block of the target on every new edge and does not reclaim the old one (survey section 4, package C). Measured with one parameter read n times: B takes 0.5 GB at n=10000, 1.9 GB at 20000, 6.5 GB at 40000 (A: 0.36 GB at 10000; it stops at the node limit before 20000). That is why the new test spreads the terms over 100 parameters. It is the user-index work of package "scans"/C, not this one.
3. The second half of step 2 (chunked columns, tombstones, building in a transaction) is not touched, as instructed. Node ids are still dense per graph.
4. Left alone on purpose: `FunctionType::withCount_/normCount_` are `uint16_t` (a function with more than 65535 parameters is rejected by an `ASSERT`); `AttrKey` is 16 bits; an instruction may take up to 65535 units of 16 bytes (about 262000 operands), a check that throws, not truncates.
5. `docs/design/graph-store/README.cn.md` was not edited (as instructed); its section 1 still lists the 16-bit limit as a current problem.

## Not done

- No Windows run, no ASAN run, no run of the full or slow tiers.
- `docs/performance/graph-store-baseline.md` not updated (the baseline script reports the Windows peak counter only; these numbers are from Linux).

## After merging fsh-dev (38c95120)

Merged without conflicts; rebuilt; smoke, compile, linear, opt, mm, trans: 2164 of 2164 pass. The quadratic growth of the user tail (finding 2) goes to the layout step.

## Later work on gs/wide (head 7a92895c)

Commits after the report above, in order:

- C0 (ea214fb7): the front end names a node by `DraftNodeRef` (draft and NodeId); builder tables are keyed by NodeId; three dead tables removed (80bac9d0).
- C1b (1c7c6a9b, 4c60c37e): draft FUNC/LOOP payloads are reference-table indices (`GraphDraft::payloadGraphs`).
- Chunked frozen graphs (ecfe01ca): the node columns are cut into `GCNodeChunk` blocks of 256 nodes.
- Readers of a draft node live in lists of their own (f6acde6f): 40000 readers of one parameter went from 8.5 s and 6.5 GB to 1.0 s and 420 MB (`capacity.fanout`). Above 32 readers, finding, adding and removing a reader are O(1) (22c862d1); the order of a long list depends only on the edits.
- Stable NodeIds (c795e4e9): a decoded draft encodes back with its ids; an erased node leaves a `Dead` record that `GCGraph::nodes()` skips. `graph_facts` rewrite_inline and rewrite_opt revalidate one more fact (intended).
- Shared chunks (e2b2887a): the encoder points at the old version's chunk when the new content equals it (`capacity.chunks_shared`: 35 of 36).

Results: smoke, compile, opt, linear, trans, mm, para, modules, tools, consistency pass 4248 of 4261 with `fs.remove_permissions` and `grammar.tables` failing (environmental; both fail on the base build as well).

Open: decode and encode are still O(n) (the draft's node storage is per node); NodeVM reads the chunk directory per node (+3% on fib.deep nvm; belongs to the execution plan); the 16-byte record split was dropped by agreement. Plans: gs-layout-plan.md, gs-regions-plan.md.

## Regions (R0, R1, R2: arms)

R0/R1 and the region clusters are merged. R2 on gs/wide after fsh-dev b29afa66:

- The front end still builds each arm as a graph called after the BRCH (`regionizeArmCalls` gives such an arm its region, the call alone in it). `Context::materializeRuntimeProgram` ends with `passes::opt::lowerBranchArms` (src/passes/opt/inline/arms.cpp): it inlines every arm call (`inlineCallInOrder(..., asArm)`); the splice puts the body in the Arm region of the arm (owner BRCH, index, result), and the graphs of the arms leave the program. Debugging (inspection mode) keeps the arms as graphs (their regions hold the call).
- The back ends read the regions. `computeExecutionOrder` lays each arm out as one piece from its region (src/execute/graph_runtime_support.cpp, class ArmRegions); `BranchArmEntries` gives the entry and the tail of each arm to the execution plan (`branchArmEntries`, `branchArmTails`); NodeVM, FastVM, JIT, taskflow, the macro evaluator and the C++ emitter's ranges use them; `collectRuntimeBranchArmRegion` (taskflow, ONNX export) takes the nodes of the region. The reckoning from the arm anchors (`resolveRuntimeBranchArmEntry`, the forward/backward walks, arm_members.h, test::arm_regions) is deleted.
- Passes keep the regions: a node added or rewired is in the deepest control region among its inputs (`GraphDraft::inheritRegion`, unless it is a JOIN); `rewriteNode` keeps the region; `eraseNode` of a BRCH/LOOP dissolves the regions it owns (`dissolveRegionsOf`); std::opt::fold's `pruneBranch` and CSE (`armRegions`) read the regions; the splice and the macro rewrite / tail-gate bridges put the nodes they add in the region they belong to.
- `branch_arms.cpp` is now the region validator: every arm has its region; what waits on a BRCH is in one of its arms; a node reads only nodes of its own region or the regions around it (a JOIN also the results of its arms).
- `modules/autodiff` differentiates arm by arm: `buildGradientGraph` outlines the Arm regions of the closure into graphs (`runtime::raiseBranchArms`, src/runtime/arm_raise.cpp) and lowers the gradient again.
- `loop_lowering` gives the arms of the BRCH it builds their regions. `reduceControlDependencies` drops control inputs from ports and constants; fold evaluates a pure node it cannot replace (an arm head) to give the ConstantExpressionFaults warning.

The arm anchors are deleted (`GCBranchArm`, the arm array and `defaultArm` of the BRCH payload, `branchArmsOf`, `isBranchArmAnchor`, `retargetBranchArmAnchors`, `setBranchArm`): the BRCH payload is `{join, armCount}`; an arm is its region and the JOIN's with input. `graph::is_anchor` is now exit, output and return only; the graph module passes stay byte-identical with std::opt::algebra/dce. An arm that is still the call of a graph is the node its region names as `callSite` (`isArmCall`). Transition debt: the front end still builds the arm graphs (buildArmGraph, captures) that lowerBranchArms inlines; the C++ emitter keeps its path for an arm that is a call; the debugger runs without the lowering; `raiseBranchArms` in autodiff; the layout of a deeply nested branch is O(nodes x depth) (unended_unrolling is 2x slower than before, 12 s against 6 s).

Test expectations changed by design (arms are no longer calls): recursion depth limits count one frame a level, not two (failed_depth, limit.macro.*, limit.map.*, limit.default.map_exceeded); jit compiles `fib`/`depth` as frameless code; node and edge counts and the dot goldens of the dumps and the exemplars; resources `countdown` ids; `regions.survive_*` count the arm regions; `graph.null_then`; port_fill_metadata (captures from a lambda); ONNX function_branch refusal case (non-recursive build); `broken_branch.node_in_two_arms` names the kind of the node; test::exhaust_perm_space replaces the old trigger (a recursion that escaped the type-specialization bound through its arm graphs). Fast tier: 4403 of 4416, `fs.remove_permissions` and `grammar.tables` environmental. fib 30: nvm -12%, fvm -24%, jit 0.11 s to 0.005 s.
