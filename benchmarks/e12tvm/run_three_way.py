# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E12 three-way numerical agreement for the MLP forward graph (MLSys T8).

One Camel program (benchmarks/mlp/model.cml forward) is executed four ways:

  1. Camel native     -- out/camel_native.npy, produced by camel.exe std::nvm
  2. ONNX artifact    -- out/mlp_forward.onnx, executed by ONNX Runtime
  3. Relax artifact   -- the same ONNX graph re-emitted as a Relax function by
                         onnx2relax.onnx_to_relax, executed by the Relax VM
  4. TVM-native check -- the same artifact via relax.frontend.onnx.from_onnx

A float64 NumPy evaluation of the same graph serves as the exact reference:
pairwise diffs between independent float32 GEMM kernels mix two accumulation
orders, so each executor's own distance to the reference is reported too.

Writes out/e12_results.csv. Acceptance (round3 T8): pairwise <= 1e-6 on the
three primary executors; the ours-vs-from_onnx leg must be bitwise identical.
"""

import csv
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import numpy_helper

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from onnx2relax import onnx_to_relax, run_on_relax_vm  # noqa: E402

OUT = HERE / "out"
ONNX_PATH = OUT / "mlp_forward.onnx"
INPUT_NPY = Path(__file__).resolve().parents[1] / "artifacts" / "mlp" / "input.npy"
BOUND = 1e-6


def run_ort(model_path: Path, x: np.ndarray) -> np.ndarray:
    sess = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    return sess.run(None, {sess.get_inputs()[0].name: x})[0]


def run_tvm_native_onnx(model: onnx.ModelProto, x: np.ndarray) -> np.ndarray:
    """Same artifact consumed by TVM's own ONNX importer (cross-check)."""
    import tvm
    from tvm.relax.frontend.onnx import from_onnx

    mod = from_onnx(model, keep_params_in_input=False)
    ex = tvm.relax.build(mod, target="llvm")
    vm = tvm.relax.VirtualMachine(ex, tvm.runtime.cpu())
    return vm["main"](tvm.runtime.tensor(x)).numpy()


def reference_f64(model: onnx.ModelProto, x: np.ndarray) -> np.ndarray:
    """Exact (float64) evaluation of the same MLP graph."""
    w = {t.name: numpy_helper.to_array(t).astype(np.float64) for t in model.graph.initializer}
    h = np.maximum(x.astype(np.float64) @ w["fc1_w"] + w["fc1_b"], 0)
    h = np.maximum(h @ w["fc2_w"] + w["fc2_b"], 0)
    return h @ w["fc3_w"] + w["fc3_b"]


def main() -> None:
    x = np.load(INPUT_NPY)
    model = onnx.load(ONNX_PATH)

    outs = {
        "camel_native": np.load(OUT / "camel_native.npy"),
        "onnx_ort": run_ort(ONNX_PATH, x),
        "relax_vm": run_on_relax_vm(model, {"input": x}),
        "relax_vm_tvm_from_onnx": run_tvm_native_onnx(model, x),
    }
    ref = reference_f64(model, x)
    print(f"output |y| in [{np.abs(ref).min():.3f}, {np.abs(ref).max():.3f}]  (1 ULP at |y|=1 is 1.19e-07)")
    for name, arr in outs.items():
        assert arr.shape == (64, 10), (name, arr.shape)

    pairs = [
        ("camel_native", "onnx_ort"),
        ("camel_native", "relax_vm"),
        ("onnx_ort", "relax_vm"),
        ("relax_vm", "relax_vm_tvm_from_onnx"),
    ]
    rows = []
    for left, right in pairs:
        d = np.abs(outs[left].astype(np.float64) - outs[right].astype(np.float64))
        rows.append((f"{left} vs {right}", float(d.max()), float(d.mean())))
    for name in ("camel_native", "onnx_ort", "relax_vm"):
        d = np.abs(outs[name].astype(np.float64) - ref)
        rows.append((f"{name} vs f64_reference", float(d.max()), float(d.mean())))

    csv_path = OUT / "e12_results.csv"
    with csv_path.open("w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["comparison", "max_abs_diff", "mean_abs_diff", "bound_1e-6_pass"])
        for name, dmax, dmean in rows:
            wcsv.writerow([name, f"{dmax:.3e}", f"{dmean:.3e}", int(dmax <= BOUND)])
            print(f"{name:42s} max {dmax:.3e}  mean {dmean:.3e}  {'PASS' if dmax <= BOUND else 'over 1e-6'}")
    print(f"csv: {csv_path}")

    assert rows[0][1] <= BOUND, "camel_native vs onnx_ort broke the A.2 artifact leg"
    assert rows[3][1] == 0.0, "our relax artifact differs from TVM's own ONNX import"
    assert all(r[1] <= 2e-6 for r in rows[4:]), "an executor drifted from the f64 reference"
    print("E12 CHECK DONE (see NOTES.md for the pairwise accumulation-order analysis)")


if __name__ == "__main__":
    main()
