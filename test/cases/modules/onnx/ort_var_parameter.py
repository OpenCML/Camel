#!/usr/bin/env python3
"""Export of a function whose state is a `var` handed to a called function, checked on ONNX Runtime.

The model is exported twice (by the operator in a default run, and by the pass onnx::export after
std::macro std::opt); each file must pass onnx.checker and give tanh(x)*2 and s+tanh(x)
for two inputs. A change of the inliner once made the exporter refuse this program."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
HOME = Path(os.environ.get("CAMEL_HOME", REPO_ROOT / "out" / "latest"))
CAMEL = HOME / "bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = HERE / "export_var_parameter.cml"


def check(path: Path, label: str) -> bool:
    onnx.checker.check_model(str(path), full_check=True)
    session = ort.InferenceSession(str(path))
    names = [i.name for i in session.get_inputs()]
    ok = True
    for x, s in [([0.5, -1.0, 0.25], [0.0, 0.0, 0.0]), ([2.0, 0.0, -3.0], [1.0, -2.0, 0.5])]:
        x = np.array(x, dtype=np.float32)
        s = np.array(s, dtype=np.float32)
        out, state = session.run(None, {names[0]: x, names[1]: s})
        err = max(float(np.abs(out - np.tanh(x) * 2).max()), float(np.abs(state - (s + np.tanh(x))).max()))
        ok = ok and err <= 1e-6
        print(f"{label}: max abs error {err:.2e}")
    return ok


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, CAMEL_HOME=str(HOME))
        for label, passes, var in [
            ("operator", [], "EXPORT_VAR_PARAMETER_OUT"),
            ("pass", ["std::macro", "std::opt", "onnx::export"], "CAMEL_ONNX_OUT"),
        ]:
            out = Path(tmp) / f"{label}.onnx"
            run_env = dict(env, EXPORT_VAR_PARAMETER_OUT=str(Path(tmp) / "unused.onnx"))
            run_env[var] = str(out)
            proc = subprocess.run([str(CAMEL), str(CASE), *passes], env=run_env, capture_output=True, text=True, timeout=120)
            if proc.returncode != 0 or not out.exists():
                print(f"{label}: export failed ({proc.returncode}): {proc.stdout.strip()[:400]} {proc.stderr.strip()[:400]}")
                ok = False
                continue
            ok = check(out, label) and ok
    print("var parameter export: PASS" if ok else "var parameter export: FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
