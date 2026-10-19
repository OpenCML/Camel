"""The onnx::export translation pass against the onnx.export_model convenience entry.

  equiv    each program is exported twice: by running it (`camel m.cml`, the
           export_model operator) and by the pipeline `camel m.cml std::macro
           std::opt onnx::export` (its parameter out names the file; the
           program is not executed). Both models must pass
           onnx.checker.check_model(full_check=True) and give bit-identical
           ONNX Runtime outputs on the same inputs (numpy array_equal).
  refuse   pipelines that lack a required simplification are refused: non-zero
           exit, the diagnostic RuntimeDiag::TranslationRefused raised in stage
           pass:onnx::export at a source position, and no model file.

Usage: python pass_export.py equiv|refuse [--keep DIR]
       (equiv needs numpy, onnx and onnxruntime; refuse needs nothing extra)
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PIPELINE = ["std::macro", "std::opt", "onnx::export"]
# Plain forward export (closure over the weights, unrolled recursion, shape arithmetic) and a
# training-step export (value_and_grad + sgd, parameters as graph inputs).
# The rest widen the coverage: an If, dynamic axes, and Adam / batch-norm / conv training steps.
PROGRAMS = [
    "export_fold.cml",
    "export_train_step.cml",
    "export_branch.cml",
    "export_dynamic.cml",
    "export_train_step_dynamic.cml",
    "export_train_step_adam.cml",
    "export_train_step_bn.cml",
    "export_train_step_conv.cml",
    "export_train_step_frozen.cml",
    "export_dynamic_recursion.cml",
    "export_loop.cml",
    "export_slice_loop.cml",
    "export_var_compound.cml",
    "export_train_step_branch.cml",
    "export_train_step_branch_disjoint.cml",
    "export_train_step_branch_scalar.cml",
    "export_train_step_branch_tensor.cml",
    "export_gated_gru_forward.cml",
    "export_gated_gru_step.cml",
    "export_train_step_dynamic_mean.cml",
    "export_var_local.cml",
    "export_float_array.cml",
    "export_grad_mean.cml",
    "export_grad_matmul_vector.cml",
    "export_linear_recursion.cml",
    "export_loop_values.cml",
]
# (program, passes, expected fragment of the message, expected 1-based line of the position)
REFUSALS = [
    ("export_train_step.cml", ["onnx::export"], "run std::opt before onnx::export", 29),
    ("export_train_step.cml", ["std::opt", "onnx::export"], "run std::macro", 25),
    ("export_nontail_recursion.cml", PIPELINE, "several branches decide whether a function calls itself", None),
]
DTYPES = {"tensor(float)": "float32", "tensor(double)": "float64", "tensor(int64)": "int64"}


def camel(args: list, env_extra: dict) -> subprocess.CompletedProcess:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, **env_extra}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    return subprocess.run([str(exe), *args], env=env, capture_output=True, text=True, cwd=REPO_ROOT)


def with_out(passes: list, model: Path) -> list:
    """The pass list with the model file given as the parameter out of onnx::export."""
    return [f'onnx::export(out="{model.as_posix()}")' if p == "onnx::export" else p for p in passes]


def rel(program: str) -> str:
    return (HERE / program).relative_to(REPO_ROOT).as_posix()


def equiv(out: Path) -> bool:
    import numpy as np
    import onnx
    import onnxruntime as ort

    ok = True
    for program in PROGRAMS:
        stem = Path(program).stem
        by_op, by_pass = out / f"{stem}.export_model.onnx", out / f"{stem}.pass.onnx"
        proc = camel([rel(program)], {"ONNX_OUT": str(by_op)})
        if proc.returncode != 0 or not by_op.exists():
            raise SystemExit(f"{program}: export_model run failed:\n{proc.stdout}\n{proc.stderr}")
        proc = camel([rel(program), *with_out(PIPELINE, by_pass)], {"ONNX_OUT": ""})
        if proc.returncode != 0:
            raise SystemExit(f"{program}: pipeline failed:\n{proc.stdout}\n{proc.stderr}")
        # The pass consumes the graph: the program (its println) must not have run.
        lines = [l for l in proc.stdout.splitlines() if l.strip()]
        executed = lines != [f"onnx::export: wrote {by_pass}"]
        print(f"{program}: pipeline exit 0, program executed: {executed}")
        ok &= not executed

        for label, path in (("export_model", by_op), ("pass", by_pass)):
            onnx.checker.check_model(str(path), full_check=True)
            ops = [n.op_type for n in onnx.load(str(path)).graph.node]
            print(f"{program}: {label}: onnx.checker full_check ok, {len(ops)} nodes")

        a = ort.InferenceSession(str(by_op), providers=["CPUExecutionProvider"])
        b = ort.InferenceSession(str(by_pass), providers=["CPUExecutionProvider"])
        sig = lambda s: [(i.name, i.type, list(i.shape)) for i in s.get_inputs()]
        if sig(a) != sig(b) or [o.name for o in a.get_outputs()] != [o.name for o in b.get_outputs()]:
            print(f"{program}: graph inputs/outputs differ: {sig(a)} vs {sig(b)}")
            ok = False
            continue
        rng = np.random.default_rng(0)
        for trial in range(3):
            # A dynamic dimension (a name instead of an extent) takes a different size per trial.
            feeds = {
                name: rng.standard_normal(
                    [d if isinstance(d, int) else trial + 2 for d in shape]
                ).astype(DTYPES[ty])
                for name, ty, shape in sig(a)
            }
            # Keep the Adam state in its domain (second moments >= 0, a step count), so the
            # outputs are numbers rather than NaN on both sides.
            for name in feeds:
                if name.startswith("adam_v"):
                    feeds[name] = np.abs(feeds[name])
                elif name == "adam_t":
                    feeds[name] = np.array(trial, dtype=np.int64)
            ya, yb = a.run(None, feeds), b.run(None, feeds)
            for o, u, v in zip(a.get_outputs(), ya, yb):
                same = bool(np.array_equal(u, v)) and u.dtype == v.dtype
                same = same and u.tobytes() == v.tobytes()
                diff = float(np.max(np.abs(u.astype(np.float64) - v.astype(np.float64)))) if u.shape == v.shape and u.size else 0.0
                print(
                    f"{program}: trial {trial} output {o.name} shape {list(u.shape)} "
                    f"array_equal {same} max|diff| {diff:.3e}"
                )
                ok &= same
    return ok


def refuse(out: Path) -> bool:
    ok = True
    for program, passes, fragment, line in REFUSALS:
        model = out / f"{Path(program).stem}.refused.onnx"
        proc = camel(
            ["--output-format", "json", rel(program), *with_out(passes, model)],
            {"ONNX_OUT": str(model)},
        )
        label = f"{program} [{' '.join(passes)}]"
        try:
            diags = json.loads(proc.stdout[proc.stdout.index("[") :])
        except ValueError:
            print(f"{label}: no JSON diagnostics in output:\n{proc.stdout}\n{proc.stderr}")
            ok = False
            continue
        d = diags[0]
        start = d["range"]["start"]
        got_line = start["line"] + 1
        checks = {
            "exit non-zero": proc.returncode != 0,
            "name": d["data"]["name"] == "RuntimeDiag::TranslationRefused",
            "stage": d["data"]["stage"] == "pass:onnx::export",
            "position": d["data"]["modulePath"].endswith(program)
            and (got_line == line if line else got_line > 1),
            "message": fragment in d["message"],
            "no file": not model.exists(),
        }
        print(
            f"{label}: exit {proc.returncode} name {d['data']['name']} stage {d['data']['stage']} "
            f"at {Path(d['data']['modulePath']).name}:{got_line}:{start['character'] + 1} "
            f"file written: {model.exists()}"
        )
        print(f"  message: {d['message']}")
        for what, passed in checks.items():
            if not passed:
                print(f"  FAILED check: {what}")
                ok = False
    return ok


def main() -> int:
    mode = sys.argv[1]
    keep = sys.argv[sys.argv.index("--keep") + 1] if "--keep" in sys.argv else None
    with tempfile.TemporaryDirectory() as t:
        out = Path(keep).resolve() if keep else Path(t)
        out.mkdir(parents=True, exist_ok=True)
        if mode == "equiv":
            ok = equiv(out)
        elif mode == "refuse":
            ok = refuse(out)
        else:
            raise SystemExit(f"unknown mode {mode}")
    print(f"onnx::export {mode}:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
