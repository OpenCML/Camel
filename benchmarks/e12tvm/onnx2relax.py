"""E12 thin translator: a Camel-exported ONNX forward graph -> a Relax function.

This is the "second exit" translator (MLSys task T8): the simplified Camel
graph's ONNX representation is replayed node-for-node into a Relax function
via ``relax.BlockBuilder``. Initializers become embedded Relax constants (the
same role they play as ONNX initializers); the remaining graph inputs become
function parameters. Ops covered are exactly the ones the Camel MLP forward
export uses; anything else raises ``NotImplementedError`` verbatim — the
translator is deliberately thin, not complete.
"""

import numpy as np
import onnx
import tvm
from onnx import numpy_helper
from tvm import relax
from tvm.script import relax as R

ONNX_DTYPE = {
    onnx.TensorProto.FLOAT: "float32",
    onnx.TensorProto.DOUBLE: "float64",
    onnx.TensorProto.INT64: "int64",
    onnx.TensorProto.INT32: "int32",
}

OP_MAP = {
    "MatMul": relax.op.matmul,
    "Add": relax.op.add,
    "Relu": relax.op.nn.relu,
    "Identity": lambda x: x,
}


def onnx_to_relax(model: onnx.ModelProto, func_name: str = "main") -> tvm.IRModule:
    graph = model.graph
    env = {}  # onnx value name -> relax expr

    for init in graph.initializer:
        env[init.name] = relax.const(numpy_helper.to_array(init))

    params = []
    for value in graph.input:
        if value.name in env:
            continue
        ttype = value.type.tensor_type
        shape = [d.dim_value for d in ttype.shape.dim]
        var = relax.Var(value.name, R.Tensor(shape, ONNX_DTYPE[ttype.elem_type]))
        params.append(var)
        env[value.name] = var

    bb = relax.BlockBuilder()
    with bb.function(func_name, params):
        with bb.dataflow():
            for node in graph.node:
                if node.op_type not in OP_MAP:
                    raise NotImplementedError(f"onnx2relax: no mapping for {node.op_type!r}")
                ins = [env[name] for name in node.input]
                result = OP_MAP[node.op_type](*ins)
                env[node.output[0]] = result if node.op_type == "Identity" else bb.emit(result)
            out = bb.emit_output(env[graph.output[0].name])
        bb.emit_func_output(out)
    return bb.get()


def run_on_relax_vm(model: onnx.ModelProto, feeds: dict) -> np.ndarray:
    """Translate, build (llvm) and execute; returns the single output as numpy."""
    ex = relax.build(onnx_to_relax(model), target="llvm")
    vm = relax.VirtualMachine(ex, tvm.runtime.cpu())
    result = vm["main"](*[tvm.runtime.tensor(feeds[name]) for name in _input_names(model)])
    return result.numpy()


def _input_names(model: onnx.ModelProto) -> list:
    init_names = {t.name for t in model.graph.initializer}
    return [v.name for v in model.graph.input if v.name not in init_names]
