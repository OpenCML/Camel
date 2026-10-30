"""The compiler gives one answer for one input, run after run: compile the flaky program forty times.

`examples/models/naive-in-expert-out/train_step.cml` used to fail about once in ten compiles with
`SemanticDiag::UnusedPureResult` at a statement that takes its value (a loop condition, a loop
body), and at a different line each time: a map in the compiler was keyed by the address of an
object the body it described outlived, and the heap laid those addresses out differently on every
run. Which body collided was fixed by the layout of the binary, so the same input passed on one
build and failed on another, and any gate could be interrupted by it.

The fix named a body by an id that is never reused. This asks for the answer on forty runs of forty
processes. The pipeline stops at `std::gir`, so the compile is what is measured and the program's
own training loop never runs.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CASE = ROOT / "examples/models/naive-in-expert-out/train_step.cml"
RUNS = 40


def main(argv):
    if len(argv) > 1:
        camel = argv[1]
    else:
        camel = str(ROOT / "out/latest/bin" / ("camel.exe" if sys.platform == "win32" else "camel"))
    for run in range(RUNS):
        result = subprocess.run(
            [camel, str(CASE), "std::gir"], cwd=ROOT, capture_output=True, text=True
        )
        assert result.returncode == 0, (
            f"run {run + 1} of {RUNS} exited {result.returncode}:\n"
            + result.stdout
            + result.stderr
        )
        assert result.stdout.startswith("digraph"), (
            f"run {run + 1} of {RUNS} built no graph:\n" + result.stdout + result.stderr
        )
        assert result.stderr == "", f"run {run + 1} of {RUNS} said something:\n" + result.stderr
    print(f"one input, one answer: PASS ({RUNS} compiles of {CASE.name}, every one silent)")


if __name__ == "__main__":
    main(sys.argv)
