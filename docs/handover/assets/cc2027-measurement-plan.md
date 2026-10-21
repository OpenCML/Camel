# CC 2027 measurement campaign (task H48)

Copied from the CC 2027 paper draft; every placeholder in its evaluation section has an item here. Run on the release v0.3.0 (task H47) instead of the tag `eval/cc2027` named below; everything else as written.

```text
=====================================================================================================
MEASUREMENT PLAN: one campaign on ONE pinned version (every \mock cell of Sec. 6 has an item here).
Hand-over version for a separate engineer; the same list is in the revision report.

E0  Setup.
    (a) Pin: tag the current origin/fsh-dev head as eval/cc2027 (record the commit hash in the artifact).
        Also build the comparison version v0.2.20 (last version before arm regions; it has the commit-time
        arm invariant and the progen `loop` feature). Release builds of both: npm run build.
    (b) One quiet Linux x86-64 machine, nothing else running (1-minute load < 0.5 before every series),
        performance governor, turbo off, SMT siblings idle; pin with taskset -c 2 (parallel runs: a fixed core
        set). Record lscpu, uname -a, compiler, Python, onnx, onnxruntime, numpy, torch versions (env.json).
    (c) Timing rule for every program P and pipeline Q: 10 rounds, variants interleaved round-robin;
        whole-process wall time t(P,Q); start-up baseline b(Q) = median of camel empty.cml Q (main returns 0)
        measured in the same rounds; report median(t) - median(b) and min..max of t - b. Size inputs so that
        std::fvm runs >= 1 s after subtraction. Peak RSS from /proc/<pid>/status VmHWM.
    Expected output: env.json, raw/*.jsonl (one line per run), summary tables (CSV + Markdown).
E1  Back-end performance (tab-perf). Programs (scaled per E0c): fib (test/cases/perf/fib_recursive.cml, N=35);
    tail loop, string churn (docs/design/cpp-memory a03_tail_loop, a04_string_churn, steps x10);
    two-state while loop (docs/design/loops/examples/bench_loop.cml, 10^8 rounds);
    MLP training step (showcase/C2, the program its compare.py drives, 300 steps).
    Pipelines: std::nvm; std::fvm; std::jit; std::opt::inline std::jit; std::macro std::opt std::jit (MLP);
    std::cpp via camel-cpp P -o P.bin then ./P.bin (report C++ compile time separately).
    Outside references on the same machine, same algorithm and input: CPython 3.12 scripts; g++ -O2 and
    clang++ -O2 hand-written C++ (fib, loops, strings); PyTorch CPU and NumPy for the MLP step
    (showcase/C2/compare.py), threads fixed and reported. Check equal work (outputs, call/round counts).
    Driver: python3 docs/design/cpp-memory/tools/measure.py --rounds 10 (extend with the CPython/C++/PyTorch
    commands). Expected output: one median (s) and range per cell of tab-perf.
E2  Parallel scaling. benchmarks/parallel/cases/{map_heavy,mc_pi,merge_sort}.cml under std::prl with
    -t 1,2,4,8,16 (and the machine's core count) and std::fvm; 10 rounds each. Expected: time per worker count.
E3  JIT benefit, time-weighted. (a) python3 test/tools/jitcov/jitcov.py --timeout 60 --out J over test/cases,
    examples, docs/tutorial/examples, benchmarks: per graph status (compiled = compiled and entered;
    compilable but never hot; refused) and mode (frameless/framed). (b) python3 test/tools/jitcov/bench.py on
    test/tools/jitcov/workloads.cml (nine workloads) with std::fvm, std::jit, 'std::jit(hot_threshold=1000000000)'
    (no-compile control), and the same after std::opt::inline. Expected: graphs table; times per workload.
E4  C++ coverage: python3 test/tools/cppexec/run.py --jobs 4 test/cases benchmarks examples exemplars, and again
    after std::opt::inline. Expected: reached / translated / compiled / same output as std::fvm / no runtime,
    and the first refusal reason per refused program (one version only).
E5  The region change as a controlled experiment (tab-difftest, part B). On BOTH v0.2.20 and eval/cc2027:
      python test/tools/progen/progen.py diff --seeds 1-2000 --matrix full --jobs 6 --out R/<ver>/s1
      python test/tools/progen/progen.py diff --seeds 1-400 --size 4 --matrix full --jobs 6 --out R/<ver>/s4
    (all features, including `loop`; the same generator revision for both versions: use the generator of
    eval/cc2027 and set CAMEL_EXE/CAMEL_HOME to each build). Then progen.py triage, and progen.py reduce for each
    program with a bad cell; bisect the pass sequence to the smallest failing one; assign each reduced program a
    root cause (the causes listed in Sec. 6.2, or a new one, described). Report per program and per root cause,
    never per cell. Report programs containing a loop inside an arm separately (known defect; do not drop them).
    Generator coverage: for every generated program write camel P std::jgir > P.json and histogram node kinds,
    region kinds and region-nesting depth; build eval/cc2027 with coverage (gcov/llvm-cov) and report line
    coverage of src/passes and src/execute for the s1 batch. Arm-delimiting code sites in both versions:
      git grep -c -E "branchArm|BranchArm|armRegions|armOf|ControlRegions|collectRuntimeBranchArmRegion" <ver> -- src include modules
    and list the functions found. Cost of commit validation: CPU samples (benchmarks/compile/hotspots.py) of
    std::opt on progen --size 20 and scale-4000; share of samples in validateRuntimeGraph*.
    Expected: part B of tab-difftest filled; a list of root causes per version with one reduced program each.
E6  Correctness on eval/cc2027: npm run test:all, then CAMEL_GC=mark-sweep npm run test:all; record pass/fail/skip
    and names of failures. progen diff --seeds 2001-2400 --matrix full with CAMEL_GC=mark-sweep;
    CAMEL_GC_POISON=1 CAMEL_JIT_HOT_THRESHOLD=0 progen diff --seeds 2401-2600 --backends jit.
    Expected: per-program counts as in E5.
E7  ONNX fidelity against native execution. Corpus: every program of test/cases/modules/onnx and the forward and
    training-step workloads of benchmarks/{mlp,lenet,gru,resnet,transformer}/model.cml, with fixed inputs.
    For each: native run camel M --arg ... -o out_native (std::nvm); export
    camel M std::macro std::opt 'onnx::export(out="m.onnx")'; onnx.checker; run ONNX Runtime (CPU) on the same
    inputs; compare every output with |a-b| <= 1e-5 * max(1, |a|) (float32; report max abs/rel error).
    Classify: exported and agrees / exported and differs / refused with a position / refused without a position /
    crash; for every refusal, record the construct at the reported position (audit all if <= 100, else 100 at
    random). Effect probes: the export tests whose programs print on a control path must be refused at the print.
    Expected: tab-coverage ONNX rows; a list of disagreements with programs.
E8  Gradient checks and loop-AD cost. Programs: test/cases/modules/autodiff/* (branches, recursion, loops, @vjp)
    and showcase/{C1,C2,C6} (their compare.py checks against PyTorch). For each scalar-output program: central
    finite differences in float64 where the program allows it (else float32 with step 1e-3), relative error
    ||g - g_fd|| / max(1, ||g_fd||). Report max error per program and pipeline (std::nvm, std::macro std::opt std::fvm,
    std::macro std::opt std::jit). Loop AD: a summation loop with n = 10^3..10^6 rounds: time and peak RSS of
    with_grad vs the forward loop; for n <= 64 also the tail-recursive form unrolled by std::macro std::opt.
    Straight-line check: after std::macro std::opt, count FUNC/CALL/FILL closure nodes in the gradient graph
    (std::stats). Expected: tab-coverage AD rows; one figure or table of loop-AD time/RSS vs n.
E9  Compile cost: python3 benchmarks/compile/bench.py run --camel new=<eval HOME> on the inputs of
    docs/technical/38 sec. 1.3; plus camel --time-phases=t.json X std::opt std::null and X std::opt::inline std::null
    on progen --size 20 and test/tools/scale mixed-4000, with hotspots.py to split pass time into rewriting,
    commit validation and encoding. Expected: phase table; split of the superlinear passes.
E10 Passes written in Camel: neutral::integers vs 'std::opt::algebra(neutral_only=true)' and unused::values vs
    'std::opt::dce(values_only=true)' on progen --size 8 programs (seeds 1-100): time (--time-phases) and graph
    equality (std::jgir, tools/graph-diff). Count the lines of the C++ functions that implement the two modes.
=====================================================================================================
@ctx Section 6. Every number from a record named in the % @src lines of the tables; everything to be measured is
a \mock cell with an item above.
```
