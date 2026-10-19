# Parallel scheduler benchmarks

Programs and scripts for measuring `std::prl` (`std::tf`) against the linear back ends. The
results and what they mean are in `docs/technical/06_runtime_and_execution.md`, 4.3.2 (cost
model) and 4.3.3 (serial graphs run as bytecode).

## Cases (`cases/`)

| Case | Shape | What it measures |
|---|---|---|
| `fib` | serial: scalar recursion, built-in operators | the per-call cost |
| `small_calls` | serial: a tail-recursive loop calling two small helpers | the per-call cost of short calls |
| `serial_math` | serial: a loop calling a module operator (`math::sqrt`) through a helper | serial code the granularity rule calls heavy |
| `map_light` | parallel `map` / `filter` / `reduce`, elements of a few operators | per-element overhead |
| `map_medium` | parallel `map`, elements a tail-recursive loop of tens of calls | mixed: parallel operator, serial elements |
| `map_heavy` | parallel `map`, 64 elements each a recursion of milliseconds | mixed: parallel operator, serial elements |
| `collatz_reduce` | parallel `map` whose elements are ordered `reduce`s calling a scalar loop | mixed, nested operators |
| `driver_inner` | serial driver loop calling a parallel inner function at every step | mixed: the other way round |
| `mc_pi`, `asian_mc` | parallel `map`, each element one module operator of 0.1 to 0.3 s | parallel only |
| `merge_sort` | recursive split, leaves sorted by a module operator | parallel only (task graphs) |
| `tensor_train` | SGD steps of a small MLP, after `std::macro` | tensor kernels |

The first line of a case may name passes that must run before the scheduler
(`// passes: std::macro`).

## Running

Inside the build container, from the repository root, with `CAMEL_HOME` set to a build:

```sh
# every case on std::fvm, std::nvm, std::prl with 1 and 4 threads and the default (all) threads
benchmarks/parallel/run.sh

# some cases, more rounds
REPS=7 benchmarks/parallel/run.sh fib small_calls serial_math map_heavy driver_inner

# two builds side by side (ratio of the first to the second per configuration)
BUILDS="new=$CAMEL_HOME old=/path/to/other/out/latest" benchmarks/parallel/run.sh fib

# the bytecode hand-off on and off in one table: the off column is a parameter of the pass
# (std::prl(bytecode=false) evaluates every graph itself, as before 4.3.3)
CONFIGS="std::fvm std::prl std::prl(bytecode=false)" benchmarks/parallel/run.sh fib small_calls
```

`run.sh` interleaves the configurations round by round and records the load average with
every run; `prl_bench.py --help` has the options (CPU pinning, JSON output, resuming).
`stack_sample.sh` takes stack samples of a running case. On a machine with other load only the
comparison within one table means anything: read the minima and the per-round ratios.
