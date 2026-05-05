# Test Rules

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

