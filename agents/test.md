# Test Rules

> **Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../docs/design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.

## General Policy
- Prefer TDD: write tests before implementation whenever feasible.
- Place test cases under `test/`.
- For substantial refactors, update `docs/` in the same change set.

## Verification Style
- Always define explicit acceptance criteria for an implementation plan and validate against them.
- For every new feature or new test case, manually run the relevant command-line case first and inspect the raw execution behavior and output before trusting the automated test harness. Use that manual observation to decide what the later `verify_*` or plan assertions should actually lock down.
- Start with small inputs when debugging high-volume workloads such as `fib 30`.
- Use `std::gir` for graph inspection, `std::nvm` for baseline runtime behavior, and `std::fvm` or `std::jit` for scheduler/runtime verification when relevant.
- Prefer routine verification in `npm run build`; use `npm run debug` only when diagnosing logging or debug-only code paths.
- **Run the plans your change touches, not the whole tree.** The full suite (`test/plans`), the GC poison sweep, the GC stress plans (`gc.collection_trees_*`, `gc_elder_*`), the progen random-program checks, the PyTorch comparisons and the perf/timeit plans are heavy: they saturate every core for minutes and are worth running only at a milestone, by the coordinator, on a quiet machine. During development a targeted plan list plus `smoke` is the expected verification. Never start a full-suite or stress run to "be safe", and do not leave one running when you stop.

