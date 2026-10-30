"""The step a program takes to give its function values the form the design asks for.

`std::defun_value` gives every function the program takes as a value one variant of the enum of its
type, with the values it captured as that variant's data, and turns every call through such a value
into a direct call. What it may not do is change what the program does: this reads four cases of
`test/cases/opt/defun` with and without the step, on all four backends, and compares the observed
output and the exit code. Then it reads the graph statistics: no call through a value and no
closure is left where the family could be lowered, and a form the step cannot see whole keeps its
closure, keeps the program's results, and is reported.

The programs are small enough that the numbers below are the whole program, and exact on purpose: a
change of what this step lowers is a change of these numbers, which is what a plan should say.
"""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASES = ROOT / "test/cases/opt/defun"

PASS = "std::defun_value"
BACKENDS = ("std::nvm", "std::fvm", "std::fastvm::jit", "std::taskflow")


def run(case, *passes):
    return subprocess.run(
        [str(EXE), str(CASES / case), *passes], cwd=ROOT, capture_output=True, text=True
    )


def program_lines(result):
    """What the program printed, without the report lines the pass writes before it."""
    return [line for line in result.stdout.splitlines() if not line.startswith("DEFUN_VALUE ")]


def stats(case, *passes):
    """The graph statistics of the pipeline, and the program's own output after them."""
    result = run(case, *passes, "std::stats")
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    line = next((one for one in lines if one.startswith("GRAPH_STATS ")), None)
    assert line is not None, result.stdout
    return json.loads(line[len("GRAPH_STATS ") :]), lines[lines.index(line) + 1 :]


# 1. The same program observed with and without the step, on every backend. A lowering that changed
#    a result, an effect or its order would differ here; so would a backend-specific lowering.
for case in ("higher_order.cml", "effects.cml", "captured.cml", "native_map.cml"):
    without = {}
    for backend in BACKENDS:
        plain = run(case, backend)
        assert plain.returncode == 0, case + " without passes: " + plain.stderr
        lowered = run(case, PASS, backend)
        assert lowered.returncode == plain.returncode, case + " " + backend + ": " + lowered.stderr
        assert program_lines(lowered) == program_lines(plain), (
            case + " " + backend + " differs:\n" + plain.stdout + "\n---\n" + lowered.stdout
        )
        without[backend] = plain
    # One program, one answer: no backend may differ from another either.
    expected = program_lines(without[BACKENDS[0]])
    for backend in BACKENDS[1:]:
        assert program_lines(without[backend]) == expected, case + " on " + backend + " differs"

# 2. Nothing is left of the function values `higher_order.cml` writes: three functions of one family
#    called through a parameter become one match, and two functions of one family called directly
#    become direct calls. No call through a value and no closure remains.
before, _ = stats("higher_order.cml")
after, program_output = stats("higher_order.cml", PASS)
assert before["closures"] == after["closures"] == 0, (before, after)
assert before["indirect_calls"] > 0, before
assert after["indirect_calls"] == 0, after
assert after["direct_calls"] > before["direct_calls"], (before, after)
assert after["branches"] == 2, after
assert program_output == [
    "HIGHER ORDER named=16 lambda=104 pairs=[10, 40, 90] taken=25 other=-5"
], program_output

# 3. A closure is a function value with the values it captured, and it is lowered like any other:
#    `captured.cml` keeps no call through a value and no closure, and has nothing to report.
captured, captured_output = stats("captured.cml", PASS)
assert captured["closures"] == 0, captured
assert captured["indirect_calls"] == 0, captured
assert captured["branches"] == 1, captured
assert captured_output == ["CAPTURED captured=15 named=6"], captured_output
quiet = run("captured.cml", PASS, "std::nvm")
assert quiet.stderr == "", quiet.stderr

# 4. A form the step cannot see whole is left as written and reported: `native_map.cml` hands a
#    closure to a higher-order operator, which calls it inside the operator. The program's output is
#    unchanged, the closure is still there, and the warning names the type and the reason.
native = run("native_map.cml", PASS, "std::nvm")
assert native.returncode == 0, native.stderr
assert program_lines(native) == ["MAP ys=[11, 12, 13] zs=[2, 4, 6] pairs=[(1, 10), (2, 20)]"], native.stdout
warnings = [
    line for line in native.stderr.splitlines() if "DefunctionalizationIncomplete" in line
]
assert len(warnings) == 1, native.stderr
assert "(x: int) => int" in warnings[0], warnings
assert "read by a node that is not a call" in warnings[0], warnings

# 5. A program whose function values are all lowered says nothing.
lowered = run("higher_order.cml", PASS, "std::nvm")
assert lowered.returncode == 0, lowered.stderr
assert lowered.stderr == "", lowered.stderr

print("Defunctionalization gives function values one form without changing results: PASS")