# Hand-over: regions (R2) and native loops (R3)

Head: gs/wide = fsh-dev 80cace9b (fast-forward; loops + linear/loops plans green after the merge). Branch `gs/wide-builder` (local, not for merge as is) holds one extra step, see "Remaining work 1".

## Delivered
- R2: arms are Arm regions built by the front end (Builder::buildArm); arm anchors, arm graphs, captures, lowerBranchArms at materialization deleted; ControlRegions (O(1) within/armOf/inBranch) drives the validator, layout and BranchArmEntries; debugger reads regions (NodeInfo::controlRegion, GraphInfo::visibleAt); unended_unrolling 3.5 s.
- R3: LOOP node with LoopCond/LoopBody regions (front end still builds loop graphs, regionizeLoops makes the regions at Context::materializeRuntimeProgram). Rounds run in the frame of the function on nvm, macro evaluator, std::prl walker (camel/execute/loop_frame.h: loopBegin/loopTest/loopEnd/stepLoops), FastVM/JIT bytecode LBEG/LTST/LEND, std::cpp `for (;;)`. Plan: ExecLoop/ExecLoopEvent in exec_plan.h, layout in computeExecutionOrder (graph_runtime_support.cpp). LOOP With inputs = [body result, cond result (if any), int64 round Port]; round slot -1 = stopped.
- Deleted: loop_lowering, native_loop, CAMEL_FVM_NATIVE_LOOP, evalLoop, SCHD loop marks, wantsLoopFrames, taskflow executionGraph.

## Baseline failures (fail on fsh-dev without our work; filed as H41)
ort_loop_values, ort_grad_loop_values, lsp.editing, examples.ml.export_onnx_translate, grammar.tables, fs.remove_permissions; slow: debugger.dap_stepping. The ort_* ones: exporter refuses the CAST of a model-dependent struct to the nominal type ("cast of a model-dependent value to 'Run'"); I did not investigate further.

## Remaining work
1. Builder emits loop regions directly. Done on branch gs/wide-builder (commit 43286534 + docs beb86776): visitLoopNode uses buildArm(kind LoopCond/LoopBody, declare ports), addRegionLoopNode/addRegionPortNode, encoder copies region ports/params and keeps ports of loops alive, isEffectNode(Loop) scans the regions, loop_lowering/:loop/collect/:graph/original/Program::original deleted. It conflicts with develop's collectLoopAppends / inferLoopStateFromAppend (graph-form, builder.h/builder_control.cpp): port them to the ArmBuild (body->value, body->ports) and rebase. Invariants found: ports must be nodes of their region (setNodeRegion), a LOOP's ctrl inputs from inside the loop are not visited before the ranges, going back to a round start must not restart/test enclosing loops (stepLoops `resumed`), ControlRegions::any() counts loops.
2. After 1: delete regionizeLoops at materialization; keep outlineLoopRegions/regionizeLoops only as autodiff's extract/embed (rename accordingly); LoopView graph-form mode exists only for that.
3. JIT: lower LBEG/LTST/LEND in x64_backend.cpp (today collectBlockers refuses the graph, so it is interpreted). Jump targets: getJumpTargetPc; optimizer/linker already handle them.
4. Perf targets of docs/technical/39_loops.md (10M rounds, two-state while, hand-written tail recursion 0.98 nvm / 0.49 fvm): now nvm 2.0 s, fvm 1.0, jit 1.0, prl 1.0. Cost is the per-round tuple FILL for k>1 state and the state tuple unpack; pass the k states separately in slots (ExecLoop already has the port slots).
5. Autodiff still raises arms to graphs (raiseBranchArms/lowerBranchArms/regionizeArmCalls) and outlines loops: delete when pullback.cpp works on regions.
6. ONNX still refuses a loop; it can now read the regions.
7. feat/trans/cpp.agree_with_fvm (slow) was not run; loop programs were compiled and compared by hand.
