# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The canonical export after `std::opt`: the entry walk must not read a non-function static slot.

`CanonicalDocument::entryGraphJson` follows the root's exit back to the graph the entry call names.
Before `std::opt` the root's exit is the entry call itself, whose context input is the function
value, so the walk stops there. `std::opt` inlines that call into the root, and then the walk
crosses the body: the call of a captured closure has the literal `4` as a normal input, a Data node
in the same static area as the function values. Reading that slot as a `Function*` and asking it
for its graph dereferenced the integer 4 and killed the export on 167 corpus programs.

The case is the one-line program this was reduced to. It asserts the observable contract only: the
pipeline exits 0 and what it writes is the version 2 document, parsed as JSON. A slot whose type
is not a function is not a callee, and an export that merely stopped crashing but wrote a broken
document would still fail here.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
CASE = ROOT / "test" / "cases" / "trans" / "canonical_opt_static_slot.cml"


def main() -> int:
    done = subprocess.run(
        [str(EXE), str(CASE), *ARGV, "std::opt", "std::jgir", "canonical=true"],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
    )
    assert done.returncode == 0, (
        "the canonical export of the optimized program must succeed; it exited "
        f"{done.returncode}\nstdout:\n{done.stdout}\nstderr:\n{done.stderr}"
    )
    body = "\n".join(
        line for line in done.stdout.splitlines() if not line.startswith("GRAPH_STATS")
    )
    document = json.loads(body)
    assert document["format"] == "camel.gir", document.get("format")
    assert document["version"] == 2 and document["canonical"] is True, (
        "canonical=true is the version 2 document (docs/technical/gir-interchange.md)"
    )
    assert document["graphs"], "the document names no graph, so nothing was exported"
    print("canonical export after std::opt: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())