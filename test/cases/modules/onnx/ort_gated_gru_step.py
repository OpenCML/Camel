"""The exported training step of the gated GRU, on every path of its decision tree.

export_gated_gru_step.cml exports `step(m, x) = (loss, sgd(m, grad, 0.01))` of a GRU whose cell
update at each of the T = 3 time steps fires only when mean(|x_t|) > 0.5. The model is exported
once and then run for all 2^3 gate patterns, each with the literal weights of the program and
with random weights; ort_gated_gru_step.cml runs the same step natively. Two comparisons are
made and reported apart:

  1. exported model on ONNX Runtime  vs  Camel's own native run of the same step
     (the exported artifact is right): loss and the 14 updated weights;
  2. Camel's native run  vs  PyTorch autograd on the same equations
     (the step itself is right). Skipped with a note when torch is not installed.

Neither stands in for the other. Tolerance 1e-5 (absolute, float32) for both.

Usage: python ort_gated_gru_step.py   (needs numpy, onnx and onnxruntime; torch for 2)
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
EXPORT = HERE / "export_gated_gru_step.cml"
NATIVE = HERE / "ort_gated_gru_step.cml"
TOL = 1e-5
LR = 0.01
TAU = 0.5

LITERAL = {
    "w_ir": [[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]],
    "w_iz": [[0.25, 0.5, -0.75], [-0.5, 1.0, 0.5]],
    "w_in": [[0.75, -0.5, 0.5], [0.25, -1.0, 0.5]],
    "w_hr": [[0.5, 0.25, -0.25], [-0.5, 0.75, 0.5], [0.25, -0.75, 1.0]],
    "w_hz": [[0.25, -0.5, 0.5], [0.75, 0.5, -1.0], [0.5, 0.25, -0.25]],
    "w_hn": [[-0.5, 0.5, 0.25], [0.5, -0.75, 1.0], [0.25, 0.5, -0.5]],
    "b_ir": [0.1, 0.0, -0.1],
    "b_iz": [0.0, 0.1, 0.2],
    "b_in": [-0.1, 0.2, 0.0],
    "b_hr": [0.1, 0.1, 0.0],
    "b_hz": [0.0, -0.1, 0.1],
    "b_hn": [0.2, 0.0, -0.2],
    "fc_w": [[0.5, -0.5], [1.0, 0.25], [-0.75, 0.5]],
    "fc_b": [0.3, -0.2],
}


def camel(program: Path, env: dict, *passes: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    full = {**os.environ, **env}
    full.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(program), *passes], env=full, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")


def count_ops(graph, counts: dict) -> None:
    for node in graph.node:
        counts[node.op_type] = counts.get(node.op_type, 0) + 1
        for attr in node.attribute:
            if attr.HasField("g"):
                count_ops(attr.g, counts)


def torch_step(weights: dict, x: np.ndarray):
    """(loss, updated weights, gate pattern) by PyTorch autograd, or None without torch."""
    try:
        import torch
    except ImportError:
        return None
    p = {k: torch.tensor(v, requires_grad=True) for k, v in weights.items()}
    xt_all = torch.from_numpy(x)
    h = torch.zeros(x.shape[0], weights["w_hr"].shape[0])
    for t in range(x.shape[1]):
        xt = xt_all[:, t, :]
        if float(xt.abs().mean()) > TAU:
            r = torch.sigmoid(xt @ p["w_ir"] + p["b_ir"] + h @ p["w_hr"] + p["b_hr"])
            z = torch.sigmoid(xt @ p["w_iz"] + p["b_iz"] + h @ p["w_hz"] + p["b_hz"])
            n = torch.tanh(xt @ p["w_in"] + p["b_in"] + r * (h @ p["w_hn"] + p["b_hn"]))
            h = (1.0 - z) * n + z * h
    y = h @ p["fc_w"] + p["fc_b"]
    loss = (y * y).mean()
    loss.backward()
    updated = {}
    for k, v in p.items():
        grad = v.grad if v.grad is not None else torch.zeros_like(v)
        updated[k] = (v.detach() - LR * grad).numpy()
    return float(loss.detach()), updated


def main() -> int:
    import onnx
    import onnxruntime as ort

    weight_sets = {"literal": {k: np.array(v, np.float32) for k, v in LITERAL.items()}}
    rng = np.random.default_rng(29)
    weight_sets["random"] = {k: (0.6 * rng.standard_normal(v.shape)).astype(np.float32) for k, v in weight_sets["literal"].items()}

    worst_artifact, worst_reference, have_torch, moved = 0.0, 0.0, True, 0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        model = tmp / "step.onnx"
        camel(EXPORT, {"ONNX_OUT": str(model)})
        proto = onnx.load(str(model))
        onnx.checker.check_model(proto, full_check=True)
        counts: dict = {}
        count_ops(proto.graph, counts)
        print(f"model: {sum(counts.values())} nodes, If {counts.get('If', 0)}, Loop {counts.get('Loop', 0)}, onnx.checker ok")
        sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        outputs = [o.name for o in sess.get_outputs()]

        for set_name, weights in weight_sets.items():
            pdir = tmp / f"params_{set_name}"
            pdir.mkdir()
            for k, v in weights.items():
                np.save(pdir / f"{k}.npy", v)
            for mask in range(8):
                pattern = [(mask >> s) & 1 for s in range(3)]
                x = (0.2 * rng.standard_normal((2, 3, 2))).astype(np.float32)
                for s, bit in enumerate(pattern):
                    # |x_t| around 0.9 fires the update, around 0.1 skips it; signs stay mixed
                    x[:, s, :] = np.sign(x[:, s, :] + 1e-3) * ((0.9 if bit else 0.1) + 0.1 * np.abs(x[:, s, :]))
                fired = [int(np.abs(x[:, s, :]).mean() > TAU) for s in range(3)]
                assert fired == pattern, (fired, pattern)
                np.save(tmp / "x.npy", x)
                out = tmp / f"out_{set_name}_{mask}"
                out.mkdir()
                camel(NATIVE, {"PARAMS": str(pdir), "X": str(tmp / "x.npy"), "OUT": str(out)}, "std::macro", "std::nvm")
                native = {"output_0": np.load(out / "loss.npy").reshape(())}
                for k in weights:
                    native[f"output_1_{k}"] = np.load(out / "params" / f"{k}.npy")
                got = dict(zip(outputs, sess.run(None, {**weights, "input1": x})))
                if set(got) != set(native):
                    print(f"FAIL: outputs {sorted(got)} differ from the native results {sorted(native)}")
                    return 1
                artifact = max(float(np.abs(np.asarray(got[k], np.float64) - native[k]).max()) for k in native)
                worst_artifact = max(worst_artifact, artifact)
                line = f"{set_name} gates {pattern}: ORT vs native {artifact:.2e}"
                # The GRU weights get a gradient exactly when some step fires; fc_b always does.
                gru_moved = float(np.abs(native["output_1_w_in"] - weights["w_in"]).max()) > 0.0
                head_moved = float(np.abs(native["output_1_fc_b"] - weights["fc_b"]).max()) > 0.0
                moved += int(head_moved and gru_moved == any(pattern))
                ref = torch_step(weights, x)
                if ref is None:
                    have_torch = False
                else:
                    reference = max(
                        abs(float(native["output_0"]) - ref[0]),
                        max(float(np.abs(native[f"output_1_{k}"] - ref[1][k]).max()) for k in weights),
                    )
                    worst_reference = max(worst_reference, reference)
                    line += f", native vs torch {reference:.2e}"
                print(line)

    print(f"ort gated gru step: 16 runs, {moved} with the weight updates their gates imply")
    print(f"artifact (ORT vs Camel native): worst {worst_artifact:.2e}")
    if have_torch:
        print(f"reference (Camel native vs PyTorch): worst {worst_reference:.2e}")
    else:
        print("reference (Camel native vs PyTorch): not run, torch is not installed")
    ok = worst_artifact < TOL and (not have_torch or worst_reference < TOL) and moved == 16
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
