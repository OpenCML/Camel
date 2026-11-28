# RQ1 classic suite

Five small programs used to measure what a backend costs on its own: call overhead, collection
loops and array construction, not the end-to-end wall clock of a big workload. They are the classic
benchmark shapes (`binary-trees`, `nbody`, `spectral-norm`, `mandelbrot`, `fannkuch`), each a single
`main.cml` with a `main.cpp` beside it.

This directory adds nothing to the tensor harness in `benchmarks/` above it; that one is a separate
contract and is untouched.

## Layout

```
benchmarks/classic/
  mandelbrot/     main.cml, main.cpp
  binary_trees/   main.cml, main.cpp
  spectral_norm/  main.cml, main.cpp
  fannkuch/       main.cml, main.cpp
  nbody/          main.cml, main.cpp
```

## Two numbers per program: the result and the work count

Each program prints its result **and a work count**, and the C++ column prints the same two
numbers. A matching result alone is not proof that two columns did the same work -- a loop that
runs zero times, or a smaller N, can still print a matching line. The work count closes that hole:

| program | result | work count | what the count is |
|---|---|---|---|
| `mandelbrot` | grid points inside the set | `2238729` | iterations the whole 200 x 200 grid cost |
| `binary_trees` | nodes in three trees of depth 16 | `393213` | the node count itself: every node is visited by the count |
| `spectral_norm` | `int(norm * 1e9)` | `302400` | (i, j) products, counted per multiply, n = 120, 10 iterations |
| `fannkuch` | checksum, largest flip count | `5040` | permutations visited (7!) |
| `nbody` | `int(total energy * 1e9)` | `375000` | (body, other body) pairs walked, 5000 steps of 0.01 |

The same five numbers come out of `main.cpp` for `mandelbrot`, `spectral_norm`, `fannkuch` and
`nbody` (`binary_trees` prints only the node count, which is its work count). Results are printed
as integers (`int(x * 1e9)` for a float) so a last-bit formatting difference cannot make two
backends look different while agreeing.

## How the columns are produced

* **Camel**: the compiler that runs them is built from `develop` (`D-1128-207` item 3), and the run
  pass names the backend (`std::nvm`, `std::fvm`, `std::jit`). The programs are user code and do not
  decide which compiler is measured.
* **C++**: `clang++ -O3 -o <name>.cpp.bin <name>.cpp`, the same arithmetic in the same order, and
  the same work count. No `volatile`, no `-O0`, no `noinline`: the result is consumed by `printf`,
  so nothing is optimized away -- and the work count is printed, which a removed loop would zero.

## What is measured

Wall clock and CPU (`user+sys`) per run, three runs per (program, backend), and the host load before
and after. A reading taken while the host is loaded is marked "not in the table": it shows the order
of magnitude only.

The C++ column is too fast for one run to measure (single runs are 2-16 ms, and the shell reports
`0.00`), so it is measured as **100 runs in one process loop**, and the per-run figure is the total
divided by 100; the per-unit column divides that by the work count.