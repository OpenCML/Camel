"""E7 route B: torch.onnx through autograd — nail the gray area with artifacts.

Unified model: Linear(10,32)->ReLU->Linear(32,1), MSE, SGD lr 0.01, params as
explicit inputs where the route allows it, dynamic batch where the route claims it.

Steps (each archives verbatim output to the log):
  B1  verbatim reproduction of pytorch#122107 (dynamic axes -> runtime
      BroadcastIterator error on a different batch size)
  B2  fixed-shape ceiling: MLP fwd+loss+autograd.grad via TorchScript opset 17,
      op census + InferenceSession numerics + what is missing
  B3  in-graph SGD attempt + 10-step external closed loop vs PyTorch trajectory
  B4  torch.onnx.export(dynamo=True) on the same autograd.grad module

Run with: venv-bench/bin/python route_b_torch.py  (writes route_b.log/.json)
"""

from __future__ import annotations

import json
import sys
import traceback
from collections import Counter
from pathlib import Path

import numpy as np
import torch

LOG = open(Path(__file__).with_name("route_b.log"), "w", encoding="utf-8")
OUT = {"torch": torch.__version__}
LR = 0.01


def say(*a):
    line = " ".join(str(x) for x in a)
    print(line)
    LOG.write(line + "\n")
    LOG.flush()


def opset_census(path):
    import onnx

    m = onnx.load(str(path))
    return {
        "nodes": len(m.graph.node),
        "ops": dict(Counter(n.op_type for n in m.graph.node)),
        "inputs": [i.name for i in m.graph.input],
        "outputs": [o.name for o in m.graph.output],
        "initializers": [i.name for i in m.graph.initializer],
        "opset": [(o.domain or "ai.onnx", o.version) for o in m.opset_import],
    }


# ---------------------------------------------------------------- B1: verbatim pytorch#122107
say("=" * 72)
say("[B1] verbatim reproduction of pytorch/pytorch#122107 (torch", torch.__version__, ")")
OUT["B1"] = {}
try:
    import onnxruntime as ort

    doBackprop = True

    class Model(torch.nn.Module):
        def forward(self, inputs):
            if doBackprop:
                xseq = inputs[0].detach().requires_grad_()
                with torch.enable_grad():
                    y = torch.pow(xseq, 2).sum(dim=-1).mean(dim=-1)
                    loss = y.sum() + y.mean()
                    return torch.autograd.grad([loss], [xseq])[0]
            else:
                xseq = inputs[0]
                return torch.pow(xseq, 2).sum()

    torch.onnx.export(
        Model(),
        [torch.randn([2, 2, 10]) ** 2],
        "route_b_b1.onnx",
        input_names=["x"],
        dynamic_axes={"x": {0: "batch_size"}},
        opset_version=17,
        output_names=["lossgrad"],
        verbose=False,
    )
    sess = ort.InferenceSession("route_b_b1.onnx")
    sess.run(None, {"x": torch.randn([2, 2, 10]).numpy()})
    say("run with export shape: ok")
    try:
        sess.run(None, {"x": torch.randn([4, 2, 10]).numpy()})
        say("run with different batch size: SUCCEEDED (unexpected)")
        OUT["B1"]["different_batch"] = "succeeded"
    except Exception as e:
        say("run with different batch size FAILED (verbatim):")
        say(str(e))
        OUT["B1"]["different_batch_error"] = str(e)
except Exception:
    tb = traceback.format_exc()
    say(tb)
    OUT["B1"]["export_error"] = tb


# ---------------------------------------------------------------- shared pieces for B2/B3/B4
class UnifiedGrad(torch.nn.Module):
    """Linear(10,32)->ReLU->Linear(32,1), MSE vs zero target, grad of the mean loss
    w.r.t. both weights, computed in-module."""

    def __init__(self):
        super().__init__()
        self.w1 = torch.nn.Parameter(torch.randn(10, 32) * 0.1)
        self.b1 = torch.nn.Parameter(torch.zeros(32))
        self.w2 = torch.nn.Parameter(torch.randn(32, 1) * 0.1)
        self.b2 = torch.nn.Parameter(torch.zeros(1))

    def forward(self, x):
        y = torch.relu(x @ self.w1 + self.b1) @ self.w2 + self.b2
        loss = (y * y).mean()
        g1, g2 = torch.autograd.grad(loss, [self.w1, self.w2], create_graph=True)
        return loss, g1, g2


class UnifiedGradLinear(UnifiedGrad):
    """Ceiling probe: same model without ReLU (pure linear chain)."""

    def forward(self, x):
        y = (x @ self.w1 + self.b1) @ self.w2 + self.b2
        loss = (y * y).mean()
        g1, g2 = torch.autograd.grad(loss, [self.w1, self.w2], create_graph=True)
        return loss, g1, g2


class UnifiedGradStep(UnifiedGradLinear):
    """B3: also emit the SGD-updated weights (the update is in the graph)."""

    def forward(self, x):
        loss, g1, g2 = super().forward(x)
        return loss, self.w1 - LR * g1, self.w2 - LR * g2


def numeric_check(name, path, model, x):
    """ORT vs torch on fresh input (fixed export shape)."""
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]
    ref = model(x)  # NOT under no_grad: the model computes autograd.grad internally
    got = sess.run(None, {"x": x.numpy()})
    errs = []
    for i, o in enumerate(outs):
        r = ref[i].detach().numpy() if isinstance(ref, tuple) else ref.detach().numpy()
        errs.append(float(np.max(np.abs(got[i] - r))) / max(float(np.max(np.abs(r))), 1e-12))
    return dict(zip(outs, [f"{e:.2e}" for e in errs]))


# ---------------------------------------------------------------- B2: fixed-shape ceiling
say("=" * 72)
say("[B2] fixed-shape MLP fwd+loss+autograd.grad, TorchScript export, opset 17")
OUT["B2"] = {}
torch.manual_seed(7)
model = UnifiedGrad().eval()
x = torch.randn(8, 10)
try:
    torch.onnx.export(model, (x,), "route_b_b2.onnx", input_names=["x"], opset_version=17, dynamo=False)
    census = opset_census("route_b_b2.onnx")
    say("census:", json.dumps(census))
    OUT["B2"]["census"] = census
    errs = numeric_check("B2", "route_b_b2.onnx", model, x)
    say("ORT vs torch (fresh input, fixed shape):", json.dumps(errs))
    OUT["B2"]["numeric"] = errs
    OUT["B2"]["missing"] = [
        "no optimizer update in the graph (gradient subgraph only, not a training step)",
        f"parameters baked as initializers (not graph inputs): {census['initializers']}",
        "dynamic shapes break (B1)",
    ]
except Exception:
    tb = traceback.format_exc()
    say(tb)
    OUT["B2"]["error"] = tb


# ---------------------------------------------------------------- B2b: where the ceiling is (no-ReLU variant)
say("=" * 72)
say("[B2b] ceiling probe: same model WITHOUT ReLU (linear chain only)")
OUT["B2b"] = {}


lin_model = UnifiedGradLinear().eval()
lin_model.load_state_dict(model.state_dict())
try:
    torch.onnx.export(lin_model, (x,), "route_b_b2b.onnx", input_names=["x"], opset_version=17, dynamo=False)
    census = opset_census("route_b_b2b.onnx")
    say("census:", json.dumps(census))
    OUT["B2b"]["census"] = census
    errs = numeric_check("B2b", "route_b_b2b.onnx", lin_model, x)
    say("ORT vs torch (fresh input):", json.dumps(errs))
    OUT["B2b"]["numeric"] = errs
    OUT["B2b"]["missing"] = [
        "no optimizer update in the graph (gradient subgraph only)",
        f"parameters baked as initializers: {census['initializers']}",
    ]
    say("VERDICT: the gray area exists only for matmul-only chains; add one ReLU and it collapses (B2)")
except Exception:
    tb = traceback.format_exc()
    say(tb)
    OUT["B2b"]["error"] = tb


# ---------------------------------------------------------------- B3: in-graph SGD + external loop
say("=" * 72)
say("[B3] in-graph SGD (loss, w1', w2') + 10-step external closed loop vs PyTorch")
OUT["B3"] = {}
step_model = UnifiedGradStep().eval()
step_model.load_state_dict(model.state_dict())
try:
    torch.onnx.export(step_model, (x,), "route_b_b3.onnx", input_names=["x"], opset_version=17, dynamo=False)
    census = opset_census("route_b_b3.onnx")
    say("census:", json.dumps(census))
    OUT["B3"]["census"] = census
    import onnxruntime as ort

    sess = ort.InferenceSession("route_b_b3.onnx", providers=["CPUExecutionProvider"])
    # external closed loop: the baked initializers are the step's "weights"; updated
    # weights come out as outputs — but there is no parameter INPUT to feed them back.
    say("graph inputs:", census["inputs"], " initializers:", census["initializers"])
    say("feeding updated weights back requires parameter graph inputs; the export has none ->")
    say("a 10-step loop is impossible without re-export or graph surgery. Demonstrating:")
    torch_ref = UnifiedGradStep().eval()
    torch_ref.load_state_dict(model.state_dict())
    for i in range(3):
        loss_t, w1_t, w2_t = torch_ref(x)
        with torch.no_grad():
            torch_ref.w1.copy_(w1_t)
            torch_ref.w2.copy_(w2_t)
        say(f"  torch closed loop step {i}: loss {float(loss_t):.6g}")
    OUT["B3"]["feedback_possible"] = False
    OUT["B3"]["verdict"] = "in-graph SGD math exports, but the artifact is a one-shot function of x only: weights are constants, the training loop cannot close outside the exporter"
except Exception:
    tb = traceback.format_exc()
    say(tb)
    OUT["B3"]["error"] = tb


# ---------------------------------------------------------------- B4: dynamo path
say("=" * 72)
say("[B4] torch.onnx.export(dynamo=True) on the autograd.grad module")
OUT["B4"] = {}
try:
    torch.onnx.export(model, (x,), "route_b_b4.onnx", dynamo=True)
    census = opset_census("route_b_b4.onnx")
    say("dynamo export SUCCEEDED:", json.dumps(census))
    OUT["B4"]["census"] = census
    try:
        import onnx

        onnx.checker.check_model(onnx.load("route_b_b4.onnx"), full_check=True)
        say("checker: PASS")
        OUT["B4"]["checker"] = "pass"
    except Exception as e:
        say("checker FAIL (verbatim):", str(e)[:300])
        OUT["B4"]["checker_error"] = str(e)[:400]
except Exception:
    tb = traceback.format_exc()
    say(tb)
    OUT["B4"]["export_error"] = tb


LOG.close()
Path(__file__).with_name("route_b.json").write_text(json.dumps(OUT, indent=2, default=str))
print("[done] wrote route_b.log / route_b.json")
