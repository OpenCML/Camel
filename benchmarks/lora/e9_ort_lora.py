"""E9 (4): head-to-head vs ORT Training's frozen-params flow.

The same LoRA-MLP (rank 8, pretrained backbone, zero-B adapters) is handed to
onnxruntime-training's generate_artifacts with the 6 adapter initializers as
requires_grad and the 6 backbone initializers as frozen_params — the only
freezing mechanism ORT Training offers. We record, for the capability table:

  * what the user must supply (name lists, built-in loss choice, artifact dir,
    coupled TrainingSession API),
  * the non-standard (com.microsoft) operator census of training_model.onnx,
  * numerics of one MSELoss(= mean(y*y)) training step at lr 0.01 vs a plain
    PyTorch LoRA reference from identical init/inputs,

… and contrast it with Camel's standard opset-17 graph driven by an unmodified
InferenceSession (E9 (2)/(3)).

Runs on the server's venv-orttrain (onnxruntime-training 1.19.2). Usage:
  venv-orttrain/bin/python e9_ort_lora.py --weights artifacts/lora/pretrained \
      --batches artifacts/lora/user_batches --out e9/ort_lora_contrast.json
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

LAYERS = ("fc1", "fc2", "fc3")
ADAPTERS = [f"{n}_lora_{s}" for n in LAYERS for s in ("a", "b")]
BACKBONE = [f"{n}_{s}" for n in LAYERS for s in ("w", "b")]
LR = 0.01


def export_forward(weights: dict, path: Path) -> None:
    import torch

    class LoRAMLPPacked(torch.nn.Module):
        def __init__(self, w):
            super().__init__()
            for n, v in w.items():  # harness names (fc1_lora_a ...) are valid attribute names
                self.register_parameter(n, torch.nn.Parameter(torch.from_numpy(v), requires_grad=False))

        def forward(self, x):
            h = x
            for n in LAYERS:
                h = (
                    h @ getattr(self, f"{n}_w")
                    + (h @ getattr(self, f"{n}_lora_b")) @ getattr(self, f"{n}_lora_a") * 2.0
                    + getattr(self, f"{n}_b")
                )
                if n != "fc3":
                    h = torch.relu(h)
            return h

    net = LoRAMLPPacked(weights)
    x = torch.zeros(32, 784)
    torch.onnx.export(
        net,
        (x,),
        str(path),
        input_names=["input1"],
        output_names=["output"],
        opset_version=17,
        dynamo=False,
    )


def torch_reference_step(weights: dict, x: np.ndarray) -> dict:
    import torch

    adapters = {n: torch.from_numpy(weights[n]).requires_grad_(True) for n in ADAPTERS}
    frozen = {n: torch.from_numpy(weights[n]) for n in BACKBONE}
    h = torch.from_numpy(x)
    for n in LAYERS:
        h = h @ frozen[f"{n}_w"] + (h @ adapters[f"{n}_lora_b"]) @ adapters[f"{n}_lora_a"] * 2.0 + frozen[f"{n}_b"]
        if n != "fc3":
            h = torch.relu(h)
    loss = (h * h).mean()
    loss.backward()
    with torch.no_grad():
        for p in adapters.values():
            p -= LR * p.grad
    return {"__loss__": float(loss), **{n: p.detach().numpy() for n, p in adapters.items()}}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--batches", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    import onnx

    weights = {p.stem: np.load(p) for p in sorted(args.weights.glob("*.npy"))}
    x = np.load(args.batches / "batch_0.npy")

    report = {"flow": "onnxruntime-training generate_artifacts with frozen_params", "lr": LR}
    with tempfile.TemporaryDirectory(prefix="e9_ort_lora_") as tmp:
        work = Path(tmp)
        fwd = work / "lora_fwd.onnx"
        export_forward(weights, fwd)
        proto = onnx.load(str(fwd))
        init_names = [i.name for i in proto.graph.initializer]  # harness names
        requires_grad = [n for n in init_names if "lora" in n]
        frozen = [n for n in init_names if "lora" not in n]
        report["user_must_supply"] = {
            "forward_onnx": "torch-exported LoRA forward with all 12 weights as initializers",
            "requires_grad_names": sorted(requires_grad),
            "frozen_param_names": sorted(frozen),
            "loss": "built-in LossType.MSELoss against a zero target (our mean(y*y)); cross-entropy is not a built-in — a CE training step needs a hand-built loss graph",
            "optimizer": "built-in OptimType.SGD, lr set through the training API",
            "driver": "coupled onnxruntime.training.api CheckpointState/Module/Optimizer — a plain InferenceSession is not the intended driver",
        }

        from onnxruntime.training import artifacts

        (work / "art").mkdir(parents=True, exist_ok=True)  # generate_artifacts does not create it
        artifacts.generate_artifacts(
            onnx.load(str(fwd)),  # ModelProto, not a path — the path form trips an external-data checker quirk
            requires_grad=requires_grad,
            frozen_params=frozen,
            loss=artifacts.LossType.MSELoss,
            optimizer=artifacts.OptimType.SGD,
            artifact_directory=str(work / "art"),
        )
        tm = onnx.load(str(work / "art" / "training_model.onnx"))
        from collections import Counter

        domains = Counter(n.domain or "ai.onnx" for n in tm.graph.node)
        ms_ops = sorted({n.op_type for n in tm.graph.node if n.domain == "com.microsoft"})
        report["training_model_census"] = {
            "nodes": len(tm.graph.node),
            "domains": dict(domains),
            "com_microsoft_op_types": ms_ops,
            "artifact_files": sorted(p.name for p in (work / "art").iterdir() if p.is_file() or p.is_dir()),
        }

        from onnxruntime.training.api import checkpoint_state, module as mapi, optimizer as optapi

        state = checkpoint_state.CheckpointState.load_checkpoint(str(work / "art" / "checkpoint"))
        m = mapi.Module(
            str(work / "art" / "training_model.onnx"),
            state,
            str(work / "art" / "eval_model.onnx"),  # export_model_for_inferencing needs it
            device="cpu",
        )
        opt = optapi.Optimizer(str(work / "art" / "optimizer_model.onnx"), m)
        if hasattr(opt, "set_learning_rate"):
            opt.set_learning_rate(LR)
        train_inputs = [i.name for i in tm.graph.input]
        skip = (
            set(requires_grad)
            | set(frozen)  # frozen backbone initializers are checkpoint state too, not user inputs
            | {n + "_grad.accumulation.buffer" for n in requires_grad}
            | {"lazy_reset_grad"}
        )
        feeds = []
        for name in train_inputs:
            if name in skip:
                continue
            feeds.append(np.ascontiguousarray(x) if name == "input1" else np.zeros((32, 10), dtype=np.float32))
        ret = m(*feeds)
        opt.step()
        m.lazy_reset_grad()
        loss = float(np.asarray(ret[0] if isinstance(ret, (list, tuple)) else ret).reshape(()))

        out_names = [o.name for o in proto.graph.output]
        infer = work / "infer.onnx"
        m.export_model_for_inferencing(str(infer), out_names)
        updated = {i.name: onnx.numpy_helper.to_array(i) for i in onnx.load(str(infer)).graph.initializer}
        ref = torch_reference_step(weights, x)
        numerics = {"ort_training_loss": loss, "torch_loss": ref["__loss__"], "adapters": {}}
        for n in ADAPTERS:
            got = updated[n]
            r = ref[n]
            scale = max(float(np.max(np.abs(r))), 1e-12)
            numerics["adapters"][n] = float(np.max(np.abs(got - r))) / scale
        report["one_step_numerics_vs_torch"] = numerics

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
