"""Check the optimization namespace against the real CLI: the names the `cxx::` isolation gives the
C++ passes, and the names it retired.

The isolation keeps **no alias** for the old sub-pass names (the author's rework, 2026-11-18,
docs/handover/reviews/questions-2026-10-29.md: "旧的子遍名不留别名 ... 内测期旧写法干净删除").
`std::opt::fold` and the rest are refused as unknown passes, and the one name that stays is
`std::opt` -- not an alias but the default pipeline's own name, the choice "optimize by default",
which today selects the C++ implementation.

Both directions are read here, because either one alone passes for the wrong reason: a build that
registered nothing would keep every refusal green, and a build that aliased everything would keep
every run green. The old spellings are refused **and the refusal names the name**, which is what
tells "this name is gone" from "this build is broken".
"""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = ROOT / "test/cases/opt/inline/consecutive_sync_calls.cml"

# The names the isolation gives the C++ passes: the standalone stages, plus the four inline names.
STAGES = ("inline", "small", "arm", "hybrid", "devirtualize", "specialize", "fold", "algebra",
          "saturate", "extract", "cse", "dce", "order", "opt")

# The names those passes answered to before the separation: the thirteen sub-pass spellings (the
# alias table the isolation deleted), the six inline-strategy spellings, and the stage's own short
# name. None of them is an alias any more, and `std::opt` -- the default pipeline's name -- is the
# one name that is deliberately not in this list.
RETIRED = (
    [f"std::opt::{name}" for name in STAGES if name != "opt"]
    + ["std::opt::inline::small", "std::opt::inline::arm", "std::opt::inline::hybrid",
       "std::inline", "std::inline::small", "std::inline::arm", "std::inline::hybrid"]
    # Retired before the isolation, and still retired: `ctrl` was renamed to `order`
    # (docs/design/effects/edges.cn.md 5.4), and these two never existed as core passes.
    + ["std::opt::ctrl", "std::opt::simplify", "std::devirtualize", "std::specialize"]
)


def run(*passes):
    return subprocess.run(
        [str(EXE), str(CASE), *passes], cwd=ROOT, capture_output=True, text=True
    )


# (1) Every name the isolation gives is a name the command line knows, and it runs.
optimized = run("std::opt", "std::nvm")
assert optimized.returncode == 0, optimized.stderr
assert optimized.stdout.split() == ["1", "11", "203", "4"], optimized.stdout

canonical = run("std::cxx::opt", "std::nvm")
assert canonical.returncode == 0, canonical.stderr
assert canonical.stdout == optimized.stdout, (
    "the default pipeline `std::opt` and `std::cxx::opt` ran different programs: "
    f"{optimized.stdout!r} vs {canonical.stdout!r}"
)

for stage in ("devirtualize", "specialize", "fold", "cse", "dce", "order"):
    result = run("std::cxx::" + stage, "std::nvm")
    assert result.returncode == 0, result.stderr
    assert result.stdout.split() == ["1", "11", "203", "4"], result.stdout

# The four inline names each run and each leaves a graph to draw; the strategies are the stage
# with a target strategy fixed, so they are read as names that exist, not as one name's aliases.
for name in ("std::cxx::inline", "std::cxx::small", "std::cxx::arm", "std::cxx::hybrid"):
    result = run(name, "std::gir")
    assert result.returncode == 0, f"{name}: {result.stderr}"
    assert result.stdout.strip(), f"{name}: the run printed no graph"

# (2) The old spellings are not accepted, and the refusal is the one an unknown pass gets: the
#     listing's header, which quotes the name it did not find. A name that still resolved would
#     run, and a name refused for another reason would carry a different sentence.
for retired in RETIRED:
    result = run(retired, "std::gir")
    assert result.returncode != 0, f"retired entry still registered: {retired}"
    expected = f"Pass <{retired}> not found"
    assert expected in (result.stderr + result.stdout), (
        f"{retired} was refused, but not as an unknown pass: "
        f"{(result.stderr + result.stdout).strip()[:400]!r}"
    )

print("Optimization namespace: the cxx:: names run and the old sub-pass names are refused: PASS")