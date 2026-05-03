# Sealed-Only Regression Matrix

This checklist verifies the sealed-only execution contract after Graph/Node convergence.

## Cases

- **`sealed-success-fvm`**: `test/perf/fib.cml`
  - Command: `camel test\perf\fib.cml std::inline std::fvm`
  - Expect: successful execution and final result print.
- **`sealed-success-jit`**: `test/run/jit/fib_recursive.cml`
  - Command: `camel test\run\jit\fib_recursive.cml std::jit`
  - Expect: successful execution under JIT backend.
- **`sealed-success-inline-structure`**: `test/passes/inline/inline_scc_entry_guard_recursion.cml`
  - Command: `camel test\passes\inline\inline_scc_entry_guard_recursion.cml std::inline std::gir`
  - Expect: valid GIR dump with no crash/assert.
- **`sealed-api-hardening`**: compile-time guard
  - Scope: internalized mutation channel (`detail::NodeMutation`) and non-public `FuncNode::setBodyGraph`.
  - Expect: external code cannot call previous public mutation entrypoints directly.

## Debug-only negative check

In debug builds, backends assert/fail on non-sealed or incomplete-frame graphs.
Recommended local sanity: run backend pipeline only through sealed outputs (`std::inline`, `std::fvm`, `std::jit`) and ensure no runtime "ensure layout" path is used.
