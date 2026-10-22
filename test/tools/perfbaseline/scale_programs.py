#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Generated programs of a given size for the size-scaling rows of the graph-store baseline.

The six programs of baseline.py are tiny (the largest has 495 nodes), so they cannot show a cost
that grows with the size of the program: the compiler's scans of the whole program, the rewrite
passes that look at every graph, the inliner's bookkeeping. These programs are made of `units`
small functions that call one another, so that the optimizer has inlining, generic instances,
branches and loops to work on in every size.

Unit `i` is `f<i>(x: int): int`; the units form chains of 8 (a unit calls the one before it, the
first of a chain calls nothing), and each chain unit is one of four shapes in turn:

  arith     `g(x) * k + c`
  branch    `if x > k then g(x - k) else x + c`        (two branch arms)
  loop      `loop acc = x for j in range(0, 3) do acc + g(j)`   (a LOOP node: cond and body graphs)
  generic   `pick(g(x), x + c, x > k)` and `len(pick('s<i>', 'abc', x > k)) + g(x)`: instances of
            one generic function for int and for string

Every function has exactly one call site of the previous unit, so inlining a chain is linear in
the program and the run costs a few calls per unit. `main` sums `f<i>(1)` for every unit through
helpers of 40 calls each and prints the sum, so that a run's output can be compared before and
after a change of the compiler.

The text depends only on `units` (no random numbers): the same count gives the same program.

    python3 test/tools/perfbaseline/scale_programs.py UNITS [FILE]   print or write one program
"""

import os
import sys

CHAIN = 8
HELPER = 40

# Sizes of the baseline rows: name -> units. The nodes each gives are in the baseline document.
SIZES = {"scale.1k": 90, "scale.10k": 900, "scale.50k": 4000}


def unit(i):
    k = i % 7 + 2
    c = i % 11 + 1
    first = i % CHAIN == 0
    prev = "x" if first else f"f{i - 1}(x)"
    shape = i % 4
    if shape == 0:
        body = f"{prev} * {k} + {c}"
    elif shape == 1:
        arm = "x" if first else f"f{i - 1}(x - {k})"
        body = f"if x > {k} then {arm} else x + {c}"
    elif shape == 2:
        step = "j" if first else f"f{i - 1}(j)"
        body = f"loop acc = x for j in range(0, 3) do acc + {step}"
    elif i % 8 == 3:
        body = f"pick({prev}, x + {c}, x > {k})"
    else:
        body = f"len(pick('s{i}', 'abc', x > {k})) + {prev}"
    return f"func f{i}(x: int): int {{\n    return {body}\n}}\n"


def program(units):
    parts = [
        f"module scale_{units}\n",
        "func pick::<T>(a: T, b: T, c: bool): T {\n    return if c then a else b\n}\n",
    ]
    parts.extend(unit(i) for i in range(units))
    helpers = []
    for start in range(0, units, HELPER):
        calls = " + ".join(f"f{i}(1)" for i in range(start, min(units, start + HELPER)))
        helpers.append(f"func sum{start // HELPER}(): int {{\n    return {calls}\n}}\n")
    parts.extend(helpers)
    main = ["func main(): int sync {", "    var total = 0"]
    main += [f"    total = total + sum{h}()" for h in range(len(helpers))]
    main += ["    println('scale total={}'->format<total>)", "    return 0", "}"]
    parts.append("\n".join(main) + "\n")
    return "\n".join(parts)


def write(units, directory):
    path = os.path.join(directory, f"scale_{units}.cml")
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(program(units))
    return path


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    text = program(int(sys.argv[1]))
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as file:
            file.write(text)
    else:
        sys.stdout.write(text)
