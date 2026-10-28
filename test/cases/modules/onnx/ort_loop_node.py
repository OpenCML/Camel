"""The source's structured loop (`loop ... do`) exported as an ONNX Loop.

`for` (`total`, one round per element of a tensor; `sweep`, one round per step of an interval
`for i in a..b`), a conditional loop (`halve`, a `while` whose data decides where it ends) and
`for ... while` (`bounded`, a trip count and a condition, two state values, so a tuple): each
model is exported once with a dynamic axis 0 and must pass onnx.checker's full check with
exactly one Loop node. For several lengths, the model's outputs on ONNX Runtime must match (a)
the same loop written in NumPy and (b) Camel's own run of the same program; floats within 1e-5
of the largest element, integers exactly.

Usage: python ort_loop_node.py for|cond   (needs numpy, onnx and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "export_loop_node.cml"
TOL = 1e-5
LIMIT = 5
F = np.float32

# Which models a mode runs, and how many outputs each has.
MODELS = {
    "for": {"total": 1, "sweep": 2},
    "cond": {"halve": 1, "bounded": 2},
}


def reference(model: str, x):
    """The outputs of `model`, in the order of the ONNX outputs."""
    if model == "total":
        acc = x * F(0.0)
        for v in x:
            acc = acc + v
        return [acc]
    if model == "sweep":
        # `for i in 0..int(sum(x))`: the rounds are the interval's, and none when it ends first.
        rounds = max(int(x.sum(dtype=np.float32)), 0) if x.size else 0
        acc = x * F(0.0)
        for i in range(rounds):
            acc = acc + F(i)
        return [np.int64(sum(range(rounds))), acc]
    if model == "halve":
        h = x.astype(np.float32)
        while float((h * h).sum(dtype=np.float32)) > 1.0:
            h = h * F(0.5)
        return [h]
    acc, n = x * F(0.0), 0
    for v in x:
        if n >= LIMIT:
            break
        acc, n = acc + v, n + 1
    return [acc, np.int64(n)]


def camel(cwd: Path, model: str, onnx_out: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "MODEL": model, "ONNX_OUT": onnx_out}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], cwd=cwd, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed ({model}):\n{proc.stdout}\n{proc.stderr}")
    if onnx_out and not Path(onnx_out).exists():
        # The export reports a refusal through the error value of the call, which a program
        # that drops it loses: say the model is missing here rather than at os.path.exists.
        raise SystemExit(f"camel wrote no model ({model}):\n{proc.stdout}\n{proc.stderr}")


def count_ops(graph, counts=None) -> dict:
    """Operator counts of a graph, the bodies of its Loops and the branches of its Ifs included."""
    counts = {} if counts is None else counts
    for node in graph.node:
        counts[node.op_type] = counts.get(node.op_type, 0) + 1
        for attr in node.attribute:
            if attr.HasField("g"):
                count_ops(attr.g, counts)
    return counts


def differs(got, want) -> float:
    """0.0 for an exact match of an integer; the error of a float, relative to its largest."""
    want = np.asarray(want)
    got = np.asarray(got)
    if want.dtype.kind in "ib":
        return 0.0 if got.astype(np.int64).reshape(-1).tolist() == want.astype(np.int64).reshape(-1).tolist() else float("inf")
    if want.size == 0:
        return 0.0 if got.size == 0 else float("inf")
    got = got.astype(np.float64).reshape(want.shape)
    return float(np.abs(got - want).max() / max(1.0, np.abs(want).max()))


def main() -> int:
    import onnx
    import onnxruntime as ort

    mode = sys.argv[1]
    if mode not in MODELS:
        raise SystemExit(f"unknown mode {mode}")
    cases = [
        np.array([1.0, -2.0, 0.5, 3.0, -0.25, 1.5], np.float32),
        np.array([0.5, 0.25], np.float32),  # one round: already small enough for `halve`
        np.array([8.0, -4.0, 2.0, 1.0, 0.5, 0.25, -1.0, 2.0], np.float32),
        np.array([0.0], np.float32),  # one round, and `halve` runs none of them
        np.array([12.0, 0.0, -8.0, 4.0], np.float32),  # `bounded` stops on its condition
        np.array([3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0], np.float32),  # more than five rounds
        np.zeros(0, np.float32),  # the trip count is 0: the `for` loops run no round
    ]
    ok = True
    worst_ref = worst_native = 0.0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        np.save(tmp / "x.npy", cases[0])
        sessions = {}
        for model, count in MODELS[mode].items():
            path = tmp / f"{model}.onnx"
            camel(tmp, model, str(path))
            proto = onnx.load(str(path))
            onnx.checker.check_model(proto, full_check=True)
            ops = count_ops(proto.graph)
            loops = ops.get("Loop", 0)
            quiet = ort.SessionOptions()
            quiet.log_severity_level = 3
            sessions[model] = ort.InferenceSession(str(path), quiet, providers=["CPUExecutionProvider"])
            kinds = [o.type for o in sessions[model].get_outputs()]
            print(f"{model}: {sum(ops.values())} nodes, {loops} Loop, outputs {kinds}")
            ok &= loops == 1 and len(kinds) == count
        for x in cases:
            np.save(tmp / "x.npy", x)
            camel(tmp, "", "")
            for model, count in MODELS[mode].items():
                sess = sessions[model]
                got = sess.run(None, {sess.get_inputs()[0].name: x})
                want = reference(model, x)
                native = [np.load(tmp / f"{model}_{i}.npy") for i in range(count)]
                e_ref = max(differs(got[i], want[i]) for i in range(count))
                e_native = max(differs(got[i], native[i]) for i in range(count))
                worst_ref, worst_native = max(worst_ref, e_ref), max(worst_native, e_native)
                good = e_ref < TOL and e_native < TOL
                ok &= good
                print(
                    f"{model}, input {np.array2string(x, precision=2)}: "
                    f"vs NumPy {e_ref:.2e}, vs native {e_native:.2e} {'ok' if good else 'MISMATCH'}"
                )
    print(f"onnx::loop node {mode}: worst vs NumPy {worst_ref:.2e}, vs native {worst_native:.2e}")
    print(f"onnx::loop node {mode}: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())