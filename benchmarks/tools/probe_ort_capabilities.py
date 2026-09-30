"""Probe which ONNX operators ONNX Runtime can run, per opset and element type.

For every operator the Camel ONNX backend can emit, every element type a Camel
tensor can have (float32, int64, bool) and every opset in the range, the probe
builds a one-node model, validates it against the ONNX schema
(``onnx.checker``), creates an ONNX Runtime session on the chosen execution
provider and runs it. The outcome is one of:

  supported   the model is valid and runs;
  no_kernel   the model is valid ONNX but the provider has no implementation
              (ORT reports NOT_IMPLEMENTED when the session is created);
  (absent)    the ONNX schema does not allow this operator/type/opset.

The result is written as declarative capability data,
``modules/onnx/capabilities/onnxruntime-cpu.json``, which the Camel exporter
embeds and checks at export time (see modules/onnx/capability.h). Re-run this
script after upgrading ONNX Runtime, then rebuild Camel.

Usage:
  .venv/bin/python tools/probe_ort_capabilities.py [--out PATH] [--opsets 13-21]
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO_ROOT / "modules" / "onnx" / "capabilities" / "onnxruntime-cpu.json"

DTYPES = {"float32": (TensorProto.FLOAT, np.float32), "int64": (TensorProto.INT64, np.int64), "bool": (TensorProto.BOOL, np.bool_)}

# A template builds (inputs: [(name, shape)], initializers, node, outputs dtype-agnostic) for element type T.
Template = Callable[[int, int], Tuple[List[Tuple[str, List[int]]], List[onnx.TensorProto], onnx.NodeProto]]


def ints(name: str, values: List[int]) -> onnx.TensorProto:
    return numpy_helper.from_array(np.array(values, dtype=np.int64), name)


def nary(op: str, shapes: List[List[int]], **attrs) -> Template:
    def build(t: int, opset: int):
        names = [f"x{i}" for i in range(len(shapes))]
        return list(zip(names, shapes)), [], helper.make_node(op, names, ["y"], **attrs)

    return build


def reduce(op: str) -> Template:
    def build(t: int, opset: int):
        axes_input = opset >= (13 if op == "ReduceSum" else 18)
        if axes_input:
            node = helper.make_node(op, ["x0", "axes"], ["y"], keepdims=0)
            return [("x0", [2, 3])], [ints("axes", [1])], node
        return [("x0", [2, 3])], [], helper.make_node(op, ["x0"], ["y"], axes=[1], keepdims=0)

    return build


def with_ints(op: str, shape: List[int], extra: Dict[str, List[int]], **attrs) -> Template:
    def build(t: int, opset: int):
        inits = [ints(name, values) for name, values in extra.items()]
        return [("x0", shape)], inits, helper.make_node(op, ["x0", *extra], ["y"], **attrs)

    return build


def constant_of_shape(t: int, opset: int):
    value = helper.make_tensor("value", t, [1], [1])
    return [], [ints("shape", [2, 3])], helper.make_node("ConstantOfShape", ["shape"], ["y"], value=value)


def where(t: int, opset: int):
    # The condition is bool; T is the type of the selected values.
    return [("c", [2, 3]), ("x1", [2, 3]), ("x2", [2, 3])], [], helper.make_node("Where", ["c", "x1", "x2"], ["y"])


TEMPLATES: Dict[str, Template] = {
    **{op: nary(op, [[2, 3], [2, 3]]) for op in ("Add", "Sub", "Mul", "Div", "Pow", "Max", "Min")},
    **{op: nary(op, [[2, 3], [2, 3]]) for op in ("Equal", "Greater", "GreaterOrEqual", "Less", "LessOrEqual")},
    **{op: nary(op, [[2, 3], [2, 3]]) for op in ("And", "Or")},
    **{
        op: nary(op, [[2, 3]])
        for op in ("Abs", "Neg", "Exp", "Log", "Sqrt", "Reciprocal", "Sigmoid", "Tanh", "Relu", "Erf", "Identity", "Not", "Shape")
    },
    "Gelu": nary("Gelu", [[2, 3]]),
    "MatMul": nary("MatMul", [[2, 3], [3, 4]]),
    "Gemm": nary("Gemm", [[2, 3], [3, 4], [4]]),
    "Softmax": nary("Softmax", [[2, 3]], axis=-1),
    "LogSoftmax": nary("LogSoftmax", [[2, 3]], axis=-1),
    "ArgMax": nary("ArgMax", [[2, 3]], axis=1, keepdims=0),
    "Transpose": nary("Transpose", [[2, 3]], perm=[1, 0]),
    "Flatten": nary("Flatten", [[2, 3, 4]], axis=1),
    "Concat": nary("Concat", [[2, 3], [2, 3]], axis=0),
    "Cast": nary("Cast", [[2, 3]], to=TensorProto.FLOAT),
    "Conv": nary("Conv", [[1, 2, 5, 5], [3, 2, 3, 3]]),
    "MaxPool": nary("MaxPool", [[1, 2, 4, 4]], kernel_shape=[2, 2]),
    "AveragePool": nary("AveragePool", [[1, 2, 4, 4]], kernel_shape=[2, 2]),
    "BatchNormalization": nary("BatchNormalization", [[1, 2, 3, 3], [2], [2], [2], [2]]),
    "LayerNormalization": nary("LayerNormalization", [[2, 3], [3], [3]]),
    **{op: reduce(op) for op in ("ReduceSum", "ReduceMean", "ReduceMax", "ReduceMin")},
    "Reshape": with_ints("Reshape", [2, 3], {"shape": [3, 2]}),
    "Unsqueeze": with_ints("Unsqueeze", [2, 3], {"axes": [0]}),
    "Slice": with_ints("Slice", [4, 3], {"starts": [0], "ends": [2], "axes": [0]}),
    "Gather": with_ints("Gather", [4, 3], {"indices": [0, 2]}, axis=0),
    "Expand": with_ints("Expand", [1, 3], {"shape": [2, 3]}),
    "Pad": with_ints("Pad", [2, 3], {"pads": [0, 1, 0, 1]}),
    "ConstantOfShape": constant_of_shape,
    "Where": where,
}


def feeds_for(model: onnx.ModelProto) -> Dict[str, np.ndarray]:
    rng = np.random.default_rng(0)
    feeds = {}
    for inp in model.graph.input:
        shape = [d.dim_value for d in inp.type.tensor_type.shape.dim]
        elem = inp.type.tensor_type.elem_type
        if elem == TensorProto.BOOL:
            feeds[inp.name] = rng.random(shape) > 0.5
        elif elem == TensorProto.INT64:
            feeds[inp.name] = rng.integers(1, 3, shape).astype(np.int64)
        else:
            feeds[inp.name] = (rng.random(shape) + 0.5).astype(np.float32)
    return feeds


def probe(op: str, dtype: str, opset: int, provider: str) -> Optional[str]:
    t, _ = DTYPES[dtype]
    inputs, inits, node = TEMPLATES[op](t, opset)
    value_infos = []
    for name, shape in inputs:
        elem = TensorProto.BOOL if (op == "Where" and name == "c") else t
        value_infos.append(helper.make_tensor_value_info(name, elem, shape))
    graph = helper.make_graph([node], "probe", value_infos, [helper.make_tensor_value_info("y", 0, None)], inits)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", opset)])
    model.ir_version = min(model.ir_version, 10)
    try:
        model = onnx.shape_inference.infer_shapes(model, strict_mode=True)
        onnx.checker.check_model(model, full_check=True)
    except Exception:
        return None  # not valid ONNX for this type/opset
    try:
        session = ort.InferenceSession(model.SerializeToString(), providers=[provider])
    except Exception as exc:
        text = str(exc)
        if "NOT_IMPLEMENTED" in text or "Could not find an implementation" in text:
            return "no_kernel"
        return None
    try:
        session.run(None, feeds_for(model))
    except Exception:
        return "no_kernel"
    return "supported"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--opsets", default="13-21", help="inclusive range, e.g. 13-21")
    ap.add_argument("--provider", default="CPUExecutionProvider")
    args = ap.parse_args()
    lo, hi = (int(x) for x in args.opsets.split("-"))
    opsets = list(range(lo, hi + 1))

    ops: Dict[str, Dict[str, Dict[str, List[int]]]] = {}
    for op in sorted(TEMPLATES):
        entry: Dict[str, Dict[str, List[int]]] = {"supported": {}, "no_kernel": {}}
        for dtype in DTYPES:
            for opset in opsets:
                outcome = probe(op, dtype, opset, args.provider)
                if outcome:
                    entry[outcome].setdefault(dtype, []).append(opset)
        entry = {k: v for k, v in entry.items() if v}
        ops[op] = entry
        gaps = entry.get("no_kernel")
        print(f"{op:20s} {json.dumps(entry.get('supported', {}))}" + (f"  NO KERNEL {json.dumps(gaps)}" if gaps else ""))

    data = {
        "format": 1,
        "runtime": "onnxruntime",
        "runtime_version": ort.__version__,
        "provider": args.provider,
        "onnx_version": onnx.__version__,
        "opsets": opsets,
        "generated": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "generated_by": "benchmarks/tools/probe_ort_capabilities.py",
        "note": "Per operator: element type -> opsets in which the provider runs it (supported) or the model is "
        "valid ONNX but the provider has no kernel (no_kernel). Combinations absent from both are not valid ONNX. "
        "The element type is that of the operator's first input (Where: its value inputs).",
        "ops": ops,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=1, sort_keys=False) + "\n")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
