# RQ1 classic suite

Five small programs used to measure what a backend costs on its own: call overhead, collection
loops and array construction, not end-to-end wall clock of a big workload. They are the classic
benchmark shapes (`binary-trees`, `nbody`, `spectral-norm`, `mandelbrot`, `fannkuch`), written so
that each one is a single `main.cml` with a `main.cpp` beside it.

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

## What each program prints

| program | printed | note |
|---|---|---|
| `mandelbrot` | count of grid points inside the set | 200 x 200 grid, 200 iterations |
| `binary_trees` | total node count of three full trees | depth 16; a node is its array of children (a type may contain itself only through an array, design principles 5.2) |
| `spectral_norm` | `int(norm * 1e9)` | n = 120, 10 power iterations |
| `fannkuch` | checksum, then the largest flip count | n = 7, permutation enumeration is recursive over "done"/"rest"; the sign is the parity of the flip count |
| `nbody` | `int(total energy * 1e9)` | five bodies, 5000 leapfrog steps of 0.01 |

The numbers are printed as integers (`int(x * 1e9)`) so a last-bit difference in float formatting
cannot make two backends look different while agreeing.

## How the columns are produced

* **Camel**: the compiler that runs them is built from `develop` (`D-1128-207` item 3), and the
  run pass names the backend (`std::nvm`, `std::fvm`, `std::jit`). The programs themselves are
  user code and do not decide what compiler is measured.
* **C++**: `clang++ -O3 -o <name>.cpp.bin <name>.cpp` from `clang++-23` of the build host's clang
  environment; the same arithmetic, in the same order, as the Camel file, so the printed line is
  the same on both sides.

## What is measured

Wall clock and CPU (`user+sys`) per run, three runs per (program, backend), and the host load
before and after. A reading taken while the host is loaded is marked "not in the table": it shows
the order of magnitude only.