# A prelude cache: what it can take, measured

The question was whether the cost of a program's first moments — the standard library's
declarations, recompiled on every start — can be kept between runs. The answer below is
measured, not estimated: a working cache of the parser's product was built, run, and it lost.
What it lost by, and what the layers above it would cost, are the numbers this note is for.

Program used throughout: `func main() sync { let x = 1; println(x) }` run with `std::nvm`, on
the Linux build host, build `Camel v0.2.37 (Build 261029_154527_1522db0ba)`. Times are CPU
(`cpu_ns` of the phase report); the host is a busy 32-core machine whose wall clock swings
between 27 and 65 ms for the same run, so wall is not quoted for single runs.

## Where a run's time goes

| phase | CPU ms | share |
|---|---:|---:|
| `startup` (process + context + builtin module) | 4.5 | 37% |
| `parse prelude` | 1.81 | 15% |
| `gct prelude` | 2.97 | 25% |
| `declare prelude` | 2.33 | 19% |
| `parse` + `gct` + `declare` of `types` | 1.01 | 8% |
| entry module, `gir`, teardown | 0.7 | 6% |
| **whole run** | **12** | |

The standard library's three passes over `prelude.cml` are 7.1 ms — 58% of the run — and
`startup`, of which the builtin module's construction is most, is another 37%. Everything the
user wrote is under 1%.

`import autodiff` adds `parse graph` 0.55, `parse rewrite` 0.20, `parse autodiff` 0.10, and
three `cmo_load`s: `graph` 0.6, `rewrite` 1.8, `autodiff` 1.2. The `.cmo` loads are `dlopen` of
a shared library: nothing to cache there, the loader already does it.

## A cache of the parser's product is a regression

`prelude.cml` is 30 KiB; its token buffer and syntax tree come to 111 KiB as a cache entry
(`types.cml`: 20 KiB). The entry is keyed on the installation's build fingerprint and the
module's own text, written to the user's cache directory, checked on read for format, key and
digest, and validated array by array before it is trusted.

| phase (CPU ms) | cache off | first run (miss, then write) | later runs (hit) | hit − off |
|---|---:|---:|---:|---:|
| `startup` | 4.55 | 4.44 | 4.08 | −0.47 |
| `parse prelude` | 1.81 | 2.35 | 2.29 | **+0.49** |
| `parse types` | 0.39 | 0.48 | 0.48 | **+0.08** |
| `gct prelude` | 2.97 | 2.94 | 2.85 | −0.12 |
| `declare prelude` | 2.33 | 2.24 | 2.14 | −0.18 |
| `gir min` | 0.47 | 0.41 | 0.36 | −0.11 |

(A −0.1 row is the host, not the cache: `gct` and `declare` read nothing this change wrote.)

Reading the entry back costs 0.5 ms; lexing and parsing the text it stands in for costs 0.35.
The cache holds a product that is more expensive to load than to make, because it is not the
expensive part: the phase called `parse` is, at 30 KiB, some 0.35 ms of lexing and parsing and
about 1.7 ms of building the AST — and a tree of tokens cannot stand in for an AST. Output,
diagnostics and both graph dumps were byte-identical with the cache on and off, and a module
whose text changed was recompiled (the key is the text's SHA-256), so the mechanism was sound;
it was simply paying more than it saved, and would have to be kept, documented and invalidated
forever for a 4% regression.

## What each layer would take

| layer | CPU ms | what keeping it needs | verdict |
|---|---:|---|---|
| module resolution | 0.05–0.15 | a fingerprint of every directory searched, since a file added beside a module changes the answer | not worth it |
| tokens + CST | 0.35 | flat arrays, no pointers | measured: a net loss, above |
| AST | ~1.7 | all ~45 load classes field by field, plus each node's origin (span offsets, stage, kind, label, parent); its correctness can be established only by rebuilding the GCT from the restored AST and comparing it with the one the live AST gives | 15% of the run, and a silent divergence is the failure mode |
| GCT + declarations | ~5.3 | types (interned structurally, built from AST type expressions — there is no reader for a type from text), data constants, and the declaration tables | 43% of the run: the real prize, and a different piece of work |

## The shape it should take

The declarations of the standard library are a function of the build, not of the user's program
or machine. A cache in a user-writable directory keyed on the build fingerprint does work, but it
reproduces what the build already knows, cannot be shared by machines running the same build, and
gives every run a directory to write into and a staleness question to answer.

Design principles already name the shape: the declarations are precompiled with the standard
library into a cache module, and a program's cost becomes the cost of loading it. Concretely, a
build step that writes the standard library's declared state — signatures, types and declaration
tables as data, or a `.cmo`-shaped module that registers them when loaded — stamped by the same
fingerprint the install already records, and a run that loads it instead of compiling
`prelude.cml` and the declarations of the modules it imports. Nothing to invalidate at run time:
an artifact of another build cannot be reached, because it belongs to the install that build
wrote. This is the step that takes the 7.1 ms of `prelude` and the builtin module's construction,
which is 95% of a start between them.

The parser's product is not that step, and this note is the measurement that says so: the
experiment is kept as a patch outside the tree rather than merged.