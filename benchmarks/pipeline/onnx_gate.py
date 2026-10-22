#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The artifact gate of an ONNX cell: runs the model a pipeline wrote on ONNX Runtime and
compares its outputs with the zero-pass run of the same program.

    onnx_gate.py REQUEST.json RESULT.json

REQUEST: {"model": path of the model written by `... onnx::export`,
          "native": the corpus entry's native oracle or null,
          "ref_output": the printed output of the zero-pass run,
          "ref_model": the model the zero-pass run wrote through export_model, or ""}

Two oracles, recorded in RESULT["oracle"]:
  native                 the corpus declares where the zero-pass run prints the value of the
                         exported function and which input it used (`native.kind == "printed"`):
                         the model is run on that input and compared with the printed numbers.
                         This compares the artifact with native execution.
  export_model_artifact  otherwise the model is compared with the model the zero-pass run
                         itself exported (`export_model`), both run on the same seeded random
                         inputs (three trials; dynamic dimensions take sizes 2, 3, 4; Adam
                         state kept in its domain, as test/cases/modules/onnx/pass_export.py
                         does). This compares two artifacts of the same lowering code: a
                         defect shared by both export paths is invisible to it.
  none                   neither is available (the zero-pass run wrote no model).

Comparison (plan C-3): floating-point outputs |a-b| <= tol * max(1, |a|) elementwise with
tol = 1e-5 (`within_strict`) and 1e-3 (`within_near`), NaN equal to NaN; integer and boolean
outputs, shapes and dtypes exactly. Effect order is not observable on this branch: the model
has no effects and the pipeline does not execute the program.

Needs numpy and onnxruntime; uses onnx (checker, full_check) when it is installed.
"""

from __future__ import annotations

import json
import re
import sys

STRICT, NEAR = 1e-5, 1e-3
DTYPES = {"tensor(float)": "float32", "tensor(double)": "float64", "tensor(int64)": "int64",
          "tensor(int32)": "int32", "tensor(bool)": "bool"}


def compare(np, ref, got) -> dict:
    """Largest absolute and scaled error between two arrays, and whether they agree."""
    if ref.shape != got.shape:
        return {"shape_equal": False, "ref_shape": list(ref.shape), "shape": list(got.shape),
                "within_strict": False, "within_near": False}
    if not (np.issubdtype(ref.dtype, np.floating) or np.issubdtype(got.dtype, np.floating)):
        same = bool(np.array_equal(ref, got))
        return {"shape_equal": True, "exact": same, "within_strict": same, "within_near": same,
                "max_abs_err": 0.0 if same else None}
    a, b = ref.astype(np.float64), got.astype(np.float64)
    both_nan = np.isnan(a) & np.isnan(b)
    diff = np.where(both_nan, 0.0, np.abs(a - b))
    diff = np.where(np.isnan(diff), np.inf, diff)          # NaN on one side only
    diff = np.where((a == b), 0.0, diff)                    # equal infinities
    scale = np.maximum(1.0, np.where(np.isfinite(a), np.abs(a), 1.0))
    scaled = diff / scale
    worst = float(scaled.max()) if scaled.size else 0.0
    return {"shape_equal": True, "max_abs_err": float(diff.max()) if diff.size else 0.0,
            "max_scaled_err": worst, "within_strict": worst <= STRICT, "within_near": worst <= NEAR}


def main() -> int:
    request = json.loads(open(sys.argv[1], encoding="utf-8").read())
    result: dict = {"available": True, "oracle": "none", "effects_observable": False,
                    "checker_ok": None, "load_ok": None, "run_ok": None, "comparisons": []}

    def finish() -> int:
        with open(sys.argv[2], "w", encoding="utf-8") as fh:
            json.dump(result, fh)
        return 0

    try:
        import numpy as np
        import onnxruntime as ort
    except ImportError as e:
        result.update(available=False, error=f"numpy/onnxruntime not importable: {e}")
        return finish()
    result["versions"] = {"numpy": np.__version__, "onnxruntime": ort.__version__}

    try:
        import onnx
        result["versions"]["onnx"] = onnx.__version__
        try:
            onnx.checker.check_model(request["model"], full_check=True)
            result["checker_ok"] = True
            model = onnx.load(request["model"])
            result["n_nodes"] = len(model.graph.node)
            result["domains"] = sorted({n.domain or "ai.onnx" for n in model.graph.node})
        except Exception as e:  # noqa: BLE001
            result.update(checker_ok=False, error=f"onnx.checker: {str(e)[:500]}")
            return finish()
    except ImportError:
        pass

    def session(path: str):
        return ort.InferenceSession(path, providers=["CPUExecutionProvider"])

    try:
        got = session(request["model"])
        result["load_ok"] = True
    except Exception as e:  # noqa: BLE001
        result.update(load_ok=False, error=f"InferenceSession: {str(e)[:500]}")
        return finish()
    sig = lambda s: [(i.name, i.type, [d if isinstance(d, int) else str(d) for d in i.shape]) for i in s.get_inputs()]  # noqa: E731
    result["inputs"] = sig(got)
    result["outputs"] = [o.name for o in got.get_outputs()]

    native = request.get("native")
    if native and native.get("kind") == "printed":
        result["oracle"] = "native"
        found = None
        for line in request.get("ref_output", "").splitlines():
            found = re.match(native["pattern"], line.strip())
            if found:
                break
        if not found:
            result.update(oracle="none", error="the zero-pass output has no line matching the native pattern")
            return finish()
        try:
            expected = np.array([float(x) for x in found.groups()], dtype=np.float64)
            inputs = got.get_inputs()
            feeds = {inp.name: np.array(feed["values"], dtype=DTYPES[inp.type]).reshape(feed["shape"])
                     for inp, feed in zip(inputs, native["feeds"])}
            outs = got.run(None, feeds)
            result["run_ok"] = True
        except Exception as e:  # noqa: BLE001
            result.update(run_ok=False, error=f"run: {str(e)[:500]}")
            return finish()
        cmp = compare(np, expected, np.asarray(outs[0]).reshape(-1))
        cmp["output"] = result["outputs"][0]
        cmp["note"] = "native values are the printed ones (println precision)"
        result["comparisons"].append(cmp)
        return finish()

    if not request.get("ref_model"):
        result["error"] = "the zero-pass run wrote no model to compare with"
        return finish()
    result["oracle"] = "export_model_artifact"
    try:
        ref = session(request["ref_model"])
    except Exception as e:  # noqa: BLE001
        result.update(oracle="none", error=f"the zero-pass model does not load: {str(e)[:500]}")
        return finish()
    if sig(ref) != sig(got) or [o.name for o in ref.get_outputs()] != result["outputs"]:
        result.update(run_ok=True, signature_equal=False, ref_inputs=sig(ref),
                      ref_outputs=[o.name for o in ref.get_outputs()])
        result["comparisons"].append({"output": "<signature>", "within_strict": False, "within_near": False,
                                      "shape_equal": False})
        return finish()
    result["signature_equal"] = True
    rng = np.random.default_rng(0)
    try:
        for trial in range(3):
            feeds = {}
            for name, ty, shape in sig(got):
                dims = [d if isinstance(d, int) else trial + 2 for d in shape]
                feeds[name] = rng.standard_normal(dims).astype(DTYPES[ty])
                if name.startswith("adam_v"):
                    feeds[name] = np.abs(feeds[name])
                elif name == "adam_t":
                    feeds[name] = np.array(trial, dtype=np.int64)
            ya, yb = ref.run(None, feeds), got.run(None, feeds)
            for o, u, v in zip(got.get_outputs(), ya, yb):
                cmp = compare(np, np.asarray(u), np.asarray(v))
                cmp.update(output=o.name, trial=trial, bit_identical=bool(
                    np.asarray(u).dtype == np.asarray(v).dtype and np.asarray(u).tobytes() == np.asarray(v).tobytes()))
                result["comparisons"].append(cmp)
        result["run_ok"] = True
    except Exception as e:  # noqa: BLE001
        result.update(run_ok=False, error=f"run: {str(e)[:500]}")
    return finish()


if __name__ == "__main__":
    sys.exit(main())
