"""E12 smoke: minimal Relax function build + VM run on apache-tvm.

Builds `main(x) = relu(x @ w + b)` via relax.BlockBuilder, compiles with
relax.build, and executes on the Relax VM. Also reports which programmable
entry points exist in this TVM build (frontend.onnx, frontend.torch, nn).
"""

import numpy as np
import tvm
from tvm import relax
from tvm.script import relax as R


def main() -> None:
    print("tvm", tvm.__version__)

    # --- entry-point census -------------------------------------------------
    import importlib

    for name in (
        "tvm.relax.frontend.onnx",
        "tvm.relax.frontend.torch",
        "tvm.relax.frontend.nn",
    ):
        try:
            mod = importlib.import_module(name)
            print(f"entry {name}: OK ({[s for s in dir(mod) if 'from' in s or 'export' in s]})")
        except Exception as exc:  # noqa: BLE001 - census wants every failure verbatim
            print(f"entry {name}: FAIL {type(exc).__name__}: {exc}")

    # --- minimal function: y = relu(x @ w + b) ------------------------------
    rng = np.random.default_rng(0)
    x_np = rng.standard_normal((4, 8)).astype("float32")
    w_np = (rng.standard_normal((8, 16)) / np.sqrt(8)).astype("float32")
    b_np = rng.standard_normal((16,)).astype("float32")
    ref = np.maximum(x_np @ w_np + b_np, 0)

    x = relax.Var("x", R.Tensor((4, 8), "float32"))
    bb = relax.BlockBuilder()
    with bb.function("main", [x]):
        with bb.dataflow():
            h = bb.emit(relax.op.nn.relu(relax.op.add(relax.op.matmul(x, relax.const(w_np)), relax.const(b_np))))
            out = bb.emit_output(h)
        bb.emit_func_output(out)
    mod = bb.get()
    print("--- IRModule ---")
    print(mod.script())

    ex = relax.build(mod, target="llvm")
    vm = relax.VirtualMachine(ex, tvm.runtime.cpu())
    got = vm["main"](tvm.runtime.tensor(x_np)).numpy()
    diff = float(np.max(np.abs(got - ref)))
    print(f"relax_vm vs numpy: max|diff| = {diff:.3e}")
    assert diff <= 1e-6, diff
    print("SMOKE PASS")


if __name__ == "__main__":
    main()
