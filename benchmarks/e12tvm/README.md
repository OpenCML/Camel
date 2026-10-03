# E12 / T8: Camel -> TVM/Relax second exit

Same simplified MLP forward graph, two artifacts: ONNX (exit #1) and Relax
(exit #2). Proof that Camel is not bound to the ONNX ecosystem. No TVM
performance comparison — this is an integration, not a contest.

## Layout

- `prepare_camel.py` — fresh Camel artifacts: `out/mlp_forward.onnx`
  (simplified forward graph, opset 17, 9 standard nodes) and
  `out/camel_native.npy` (native `std::nvm` output).
- `onnx2relax.py` — the thin translator (ONNX exit graph -> Relax function via
  `relax.BlockBuilder`; 4 op mappings: MatMul/Add/Relu/Identity).
- `smoke_relax.py` — minimal Relax build+VM evidence and entry-point census.
- `run_three_way.py` — Camel native vs ONNX Runtime vs Relax VM vs TVM-native
  `from_onnx`, plus a float64 exact reference; writes `out/e12_results.csv`.

## Run

```powershell
# one-time: python -m venv .cache/venv-e12tvm; pip install apache-tvm==0.27.0.post1 onnxruntime onnx numpy
python benchmarks/e12tvm/prepare_camel.py
python benchmarks/e12tvm/smoke_relax.py
python benchmarks/e12tvm/run_three_way.py
```

Outputs land in `out/` (gitignored; archived copies in
`Camel-Archive/MLSys2027/artifacts/e12-tvm/`).
