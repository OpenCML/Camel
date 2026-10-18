"""Pass-composition export of pure programs, checked numerically against their native run.

Each program of this folder is a pure `main` (no export request, no `import onnx`). It is

  - exported by `camel p.cml std::macro std::opt onnx::export` (CAMEL_ONNX_OUT),
    the model checked by onnx.checker(full_check) when the onnx package is present and its
    inputs, initializers and outputs compared with what the declared types say;
  - run natively with the same values (`--arg name=file`, `-o dir`) under the default
    pipeline and under std::fvm, std::jit and std::tf;
  - run on ONNX Runtime with those values.

The ONNX Runtime outputs must match every native run within 1e-5.

Cases: declared shapes with a dynamic axis (two batch sizes), a tuple result, a parameterless
`main` (a constant model), weights as `with` parameters exported as inputs and, with
`--arg p=<dir>` at export, as initializers, and a training step (loss and updated weights).

Usage: python ort_entry_export.py   (needs numpy and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[4]
TOL = 1e-5
SCHEDULERS = [None, "std::fvm", "std::jit", "std::tf"]
EXPORT = ["std::macro", "std::opt", "onnx::export"]


def camel(program: str, *args: str, env: dict | None = None) -> str:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    full = {**os.environ, **(env or {})}
    full.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(HERE / program), *args], env=full, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"FAIL: camel {program} {' '.join(args)} failed:\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def save_tree(root: Path, name: str, value) -> str:
    """Writes a value the way --arg reads it: a tensor as <name>.npy, a dict as a directory of leaves."""
    if isinstance(value, dict):
        d = root / name
        d.mkdir(exist_ok=True)

        def leaves(prefix: str, v) -> None:
            for k, x in v.items():
                key = f"{prefix}.{k}" if prefix else k
                if isinstance(x, dict):
                    leaves(key, x)
                else:
                    np.save(d / f"{key}.npy", x)

        leaves("", value)
        return str(d)
    if isinstance(value, np.ndarray):
        np.save(root / f"{name}.npy", value)
        return str(root / f"{name}.npy")
    return repr(value)


def flatten(prefix: str, value) -> dict:
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            out.update(flatten(f"{prefix}_{k}", v))
        return out
    return {prefix: value}


def load_result(path: Path, outputs: list[str]) -> list[np.ndarray]:
    """The native result in the order of the model outputs (output, output_0, output_1_w, ...)."""
    if len(outputs) == 1 and outputs[0] == "output":
        f = path.with_suffix(".npy") if path.with_suffix(".npy").exists() else path
        return [np.load(f) if f.suffix == ".npy" else np.array(float(f.read_text()))]
    got = []
    for name in outputs:
        leaf = name[len("output_"):].replace("_", ".")
        npy = path / f"{leaf}.npy"
        got.append(np.load(npy) if npy.exists() else np.array(float((path / leaf).read_text())))
    return got


def check(tmp: Path, label: str, program: str, values: dict, bound: list[str], want_inputs: list, want_inits: list[str], want_outputs: list[str]) -> float:
    """Exports `program` with the parameters `bound` given at export, runs it everywhere, returns the worst error."""
    import onnxruntime as ort

    files = {name: save_tree(tmp, f"{label}_{name}", value) for name, value in values.items()}
    model = tmp / f"{label}.onnx"
    export_args = [a for name in bound for a in ("--arg", f"{name}={files[name]}")]
    out = camel(program, *export_args, *EXPORT, env={"CAMEL_ONNX_OUT": str(model)})
    if f"onnx::export: wrote {model}" not in out:
        raise SystemExit(f"FAIL: {label}: no model written: {out}")
    try:
        import onnx

        m = onnx.load(str(model))
        onnx.checker.check_model(m, full_check=True)
        inits = sorted(i.name for i in m.graph.initializer)
        if not set(want_inits) <= set(inits):
            raise SystemExit(f"FAIL: {label}: initializers {inits}, expected {want_inits} among them")
        if not want_inits and any(not (n.startswith("const") or n.startswith("ints")) for n in inits):
            raise SystemExit(f"FAIL: {label}: unexpected named initializers {inits}")
    except ImportError:
        pass
    sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
    inputs = [(i.name, list(i.shape)) for i in sess.get_inputs()]
    outputs = [o.name for o in sess.get_outputs()]
    if inputs != want_inputs:
        raise SystemExit(f"FAIL: {label}: model inputs {inputs}, expected {want_inputs}")
    if outputs != want_outputs:
        raise SystemExit(f"FAIL: {label}: model outputs {outputs}, expected {want_outputs}")

    feeds = {}
    for name, value in values.items():
        if name not in bound:
            feeds.update(flatten(name, value))
    feeds = {k: (v if isinstance(v, np.ndarray) else np.array(v, np.float64)) for k, v in feeds.items()}
    got = sess.run(None, feeds)

    worst = 0.0
    run_args = [a for name in values for a in ("--arg", f"{name}={files[name]}")]
    for sched in SCHEDULERS:
        result = tmp / f"{label}_result_{(sched or 'default').replace(':', '_')}"
        camel(program, *run_args, "-o", str(result), *([sched] if sched else []))
        native = load_result(result, outputs)
        for name, g, n in zip(outputs, got, native):
            if np.asarray(g).shape != n.shape:
                raise SystemExit(f"FAIL: {label}: output {name} has shape {np.asarray(g).shape} on ONNX Runtime, {n.shape} natively ({sched or 'default'})")
            worst = max(worst, float(np.abs(np.asarray(g, np.float64) - n.astype(np.float64)).max()) if n.size else 0.0)
    print(f"{label}: inputs {[n for n, _ in inputs]} outputs {outputs}: max abs err {worst:.2e}")
    return worst


def main() -> int:
    rng = np.random.default_rng(7)
    w = rng.standard_normal((3, 4)).astype(np.float32)
    b = rng.standard_normal((4,)).astype(np.float32)
    worst = 0.0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        for n in (2, 5):
            x = rng.standard_normal((n, 3)).astype(np.float32)
            worst = max(worst, check(tmp, f"forward_batch{n}", "forward.cml", {"x": x, "w": w}, [],
                                     [("x", ["x_dim0", 3]), ("w", [3, 4])], [], ["output"]))
            worst = max(worst, check(tmp, f"weights_inputs_batch{n}", "weights.cml", {"p": {"fc": {"w": w, "b": b}}, "x": x}, [],
                                     [("p_fc_w", [3, 4]), ("p_fc_b", [4]), ("x", ["x_dim0", 3])], [], ["output"]))
            worst = max(worst, check(tmp, f"weights_initializers_batch{n}", "weights.cml", {"p": {"fc": {"w": w, "b": b}}, "x": x}, ["p"],
                                     [("x", ["x_dim0", 3])], ["w", "b"], ["output"]))
        x2 = rng.standard_normal((2, 3)).astype(np.float32)
        worst = max(worst, check(tmp, "tuple", "tuple.cml", {"x": x2, "w": w}, [],
                                 [("x", [2, 3]), ("w", [3, 4])], [], ["output_0", "output_1"]))
        worst = max(worst, check(tmp, "constant", "constant.cml", {}, [], [], [], ["output"]))
        x5 = rng.standard_normal((5, 3)).astype(np.float32)
        y5 = rng.standard_normal((5, 4)).astype(np.float32)
        worst = max(worst, check(tmp, "train_step", "train_step.cml", {"p": {"w": w, "b": b}, "x": x5, "y": y5}, [],
                                 [("p_w", [3, 4]), ("p_b", [4]), ("x", [5, 3]), ("y", [5, 4])], [],
                                 ["output_0", "output_1_w", "output_1_b"]))
    print(f"ort entry export: worst {worst:.2e}")
    print("PASS" if worst < TOL else "FAIL")
    return 0 if worst < TOL else 1


if __name__ == "__main__":
    sys.exit(main())
