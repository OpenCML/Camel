"""One program, one graph, one dump: `std::gir` prints the same bytes on every run.

The printer draws a node's order predecessors from the graph's own array. That array used to be
written in whatever order the draft held them, and the draft collected them from containers of a
rewrite, so the five order edges of one node could be printed in two different orders by the same
binary on the same input -- 472 lines both times, the same lines, five of them moved. Any gate that
compares dumps byte for byte then reddened about once in ten runs.

This reads `test/cases/linear/byref/var_capture_reference.cml` twenty times in twenty processes
(each with its own heap, which is what made the order move) and asks for one hash. The file is the
one the two orders were observed on.
"""
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CASE = ROOT / "test/cases/linear/byref/var_capture_reference.cml"
RUNS = 20


def main(argv):
    if len(argv) > 1:
        camel = argv[1]
    else:
        camel = str(ROOT / "out/latest/bin" / ("camel.exe" if sys.platform == "win32" else "camel"))
    hashes = {}
    lines = 0
    for run in range(RUNS):
        result = subprocess.run(
            [camel, str(CASE), "std::gir"], cwd=ROOT, capture_output=True, text=True
        )
        assert result.returncode == 0, f"run {run + 1} exited {result.returncode}: {result.stderr}"
        lines = result.stdout.count("\n")
        digest = hashlib.sha256(result.stdout.encode()).hexdigest()
        hashes.setdefault(digest, []).append(run + 1)
    # A dump of nothing is the same on every run too: the case has to be one the printer has work
    # to do on, or this guard says nothing.
    assert lines > 100, f"the dump is only {lines} lines: {CASE}"
    assert len(hashes) == 1, (
        f"{RUNS} runs printed {len(hashes)} different dumps: "
        + ", ".join(f"{digest[:12]} on runs {runs}" for digest, runs in hashes.items())
    )
    print(
        f"std::gir printed one graph one way: PASS ({RUNS} runs, {lines} lines, "
        f"{next(iter(hashes))[:12]})"
    )


if __name__ == "__main__":
    main(sys.argv)