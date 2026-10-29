"""The pass a program takes to lower function values into direct calls, checked against the real CLI.

`std::defunctionalize` turns the call through a function value into a direct call wherever the
function is one the program can name, and leaves the rest as written. What it may not do is change
what the program does: this reads the three cases of `test/cases/opt/defun` with and without the
pass, on all four backends, and compares the observed output and the exit code. Then it reads the
graph statistics: the calls it lowered are counted as direct calls, no closure is added, and an
unsupported form (a closure) keeps the program's results and is reported.

The programs are small enough that the numbers below are the whole program, and exact on purpose:
a change of what this pass lowers is a change of these numbers, which is what a plan should say.
"""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASES = ROOT / "test/cases/opt/defun"

PASS = "std::defunctionalize"
BACKENDS = ("std::nvm", "std::fvm", "std::fastvm::jit", "std::taskflow")


def run(case, *passes):
    return subprocess.run(
        [str(EXE), str(CASES / case), *passes], cwd=ROOT, capture_output=True, text=True
    )


def stats(case, *passes):
    """The graph statistics of the pipeline, and the program's own output after them."""
    result = run(case, *passes, "std::stats")
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    assert lines and lines[0].startswith("GRAPH_STATS "), result.stdout
    return json.loads(lines[0][len("GRAPH_STATS ") :]), lines[1:]


# 1. The same program observed with and without the pass, on every backend. A lowering that changed
#    a result, an effect or its order would differ here; so would a backend-specific lowering.
for case in ("higher_order.cml", "effects.cml", "captured.cml", "native_map.cml"):
    without = {}
    for backend in BACKENDS:
        plain = run(case, backend)
        assert plain.returncode == 0, case + " without passes: " + plain.stderr
        lowered = run(case, PASS, backend)
        assert lowered.returncode == plain.returncode, case + " " + backend + ": " + lowered.stderr
        assert lowered.stdout == plain.stdout, (
            case + " " + backend + " differs:\n" + plain.stdout + "\n---\n" + lowered.stdout
        )
        without[backend] = plain
    # One program, one answer: no backend may differ from another either.
    expected = without[BACKENDS[0]].stdout
    for backend in BACKENDS[1:]:
        assert without[backend].stdout == expected, case + " on " + backend + " differs"

# 2. What the pass lowered is a direct call where it was a call through a value, and it adds no
#    closure. `higher_order.cml` passes a named function, a lambda, a two-parameter lambda and a
#    function in each arm of a branch; the calls it lowers are the calls written through them.
before, _ = stats("higher_order.cml")
after, program_output = stats("higher_order.cml", PASS)
assert before["closures"] == after["closures"] == 0, (before, after)
assert after["direct_calls"] > before["direct_calls"], (before, after)
assert after["indirect_calls"] < before["indirect_calls"], (before, after)
assert program_output == [
    "HIGHER ORDER named=16 lambda=104 pairs=[10, 40, 90] taken=25 other=-5"
], program_output

# 3. A form the pass cannot name is left as written and reported: the closure of `captured.cml`
#    keeps the call through it, the program's output is unchanged, and the warning names the graph
#    and what it holds. Nothing is guessed, so nothing changes.
captured = run("captured.cml", PASS, "std::nvm")
assert captured.returncode == 0, captured.stderr
assert captured.stdout == "CAPTURED captured=15 named=6\n", captured.stdout
warnings = [
    line for line in captured.stderr.splitlines() if "DefunctionalizationIncomplete" in line
]
assert len(warnings) == 2, captured.stderr
assert "a closure written here over 1 captured value(s)" in warnings[1], warnings

# 4. A graph nothing calls is not reported: `higher_order.cml` keeps the original `apply` and
#    `pairwise` graphs alive as function values, whose calls are no longer made, and the pass says
#    nothing about them.
lowered = run("higher_order.cml", PASS, "std::nvm")
assert lowered.returncode == 0, lowered.stderr
assert lowered.stderr == "", lowered.stderr

print("Defunctionalization lowers function values without changing results: PASS")