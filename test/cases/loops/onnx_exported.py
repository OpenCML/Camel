"""A `loop` in a function handed to onnx.export_model is exported as one ONNX Loop.

`onnx_exported.cml` prints the loop's value and then exports the function that contains it. The
model must be written, must pass `onnx.checker`'s full check, must hold exactly one Loop node and
must take the two tensors the program gave it. Two modes: `default` runs the program on the
default pipeline, `prl` runs it on std::prl -- that back end lowers the loop in its own copy of
the graph, so the export must still read the program's own LOOP either way.

Usage: python onnx_exported.py default|prl   (needs onnx; numpy for the shapes it reads)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import onnx

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
PROGRAM = HERE / "onnx_exported.cml"
# The extra word the program is run with, per mode: std::prl schedules the graph on that back end.
PASSES = {"default": [], "prl": ["std::prl"]}


def count_loops(graph) -> int:
    """Loop nodes of a graph, the bodies of its Loops and the branches of its Ifs included."""
    total = 0
    for node in graph.node:
        if node.op_type == "Loop":
            total += 1
        for attr in node.attribute:
            if attr.HasField("g"):
                total += count_loops(attr.g)
    return total


def run(mode: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    with tempfile.TemporaryDirectory() as scratch:
        model_path = Path(scratch) / "onnx_exported.onnx"
        env = {**os.environ, "ONNX_OUT": str(model_path)}
        env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
        proc = subprocess.run(
            [str(exe), str(PROGRAM), *PASSES[mode]],
            cwd=scratch,
            env=env,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            raise SystemExit(
                f"camel failed on the {mode} back end ({proc.returncode}):\n"
                f"{proc.stdout}\n{proc.stderr}"
            )
        # The loop's value, printed before the export: the program ran on that back end.
        if "[" not in proc.stdout:
            raise SystemExit(
                f"the program printed no value on the {mode} back end:\n"
                f"{proc.stdout}\n{proc.stderr}"
            )
        if not model_path.exists():
            raise SystemExit(
                f"the export wrote no model on the {mode} back end:\n"
                f"{proc.stdout}\n{proc.stderr}"
            )

        model = onnx.load(str(model_path))
        onnx.checker.check_model(model, full_check=True)

        loops = count_loops(model.graph)
        if loops != 1:
            raise SystemExit(f"one Loop node expected on the {mode} back end, got {loops}")
        initializers = {init.name for init in model.graph.initializer}
        inputs = [value.name for value in model.graph.input if value.name not in initializers]
        if len(inputs) != 2:
            raise SystemExit(f"the model must take two tensors, got {inputs}")
        outputs = [value.name for value in model.graph.output]
        if len(outputs) != 1:
            raise SystemExit(f"one output expected, got {outputs}")

    print(f"onnx export {mode}: PASS")


def main() -> None:
    if len(sys.argv) != 2 or sys.argv[1] not in PASSES:
        raise SystemExit("usage: python onnx_exported.py default|prl")
    run(sys.argv[1])


if __name__ == "__main__":
    main()