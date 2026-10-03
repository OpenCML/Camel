"""E12 prepare: fresh Camel artifacts for the MLP forward graph.

Runs benchmarks/mlp/model.cml twice with the shared camel_bench protocol
(see benchmarks/common/camel.py):

  1. CAMEL_BENCH_EXPORT -> out/mlp_forward.onnx  (simplified forward graph)
  2. std::nvm           -> out/camel_native.npy  (native execution output)

The ONNX file is the ONNX exit artifact; onnx2relax.py turns the same file
into the Relax exit artifact. Run before run_three_way.py.
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CAMEL = REPO / "out" / "latest" / "bin" / "camel.exe"
MODEL = REPO / "benchmarks" / "mlp"
ARTIFACTS = REPO / "benchmarks" / "artifacts" / "mlp"
OUT = HERE / "out"


def run(env_extra: dict, *passes: str) -> None:
    env = dict(os.environ)
    env.update(env_extra)
    env.setdefault("CAMEL_HOME", str(REPO / "out" / "latest"))
    env["CAMEL_PACKAGES"] = str(REPO / "benchmarks" / "common")
    env["CAMEL_BENCH_WEIGHTS"] = str(ARTIFACTS / "weights")
    env["CAMEL_BENCH_INPUT"] = str(ARTIFACTS / "input.npy")
    proc = subprocess.run([str(CAMEL), "model.cml", *passes], cwd=MODEL, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"camel exited {proc.returncode}: {proc.stderr.strip()[-2000:]}")
    for line in proc.stdout.splitlines():
        if line.startswith("CAMEL_BENCH"):
            print(line)


def main() -> None:
    OUT.mkdir(exist_ok=True)
    onnx_path = OUT / "mlp_forward.onnx"
    onnx_path.unlink(missing_ok=True)
    run({"CAMEL_BENCH_EXPORT": str(onnx_path)})
    assert onnx_path.exists(), "ONNX export produced no file"

    native_path = OUT / "camel_native.npy"
    native_path.unlink(missing_ok=True)
    run(
        {
            "CAMEL_BENCH_OUTPUT": str(native_path),
            "CAMEL_BENCH_WARMUP": "2",
            "CAMEL_BENCH_REPS": "3",
            "CAMEL_BENCH_THREADS": "4",
        },
        "std::nvm",
    )
    assert native_path.exists(), "native run produced no output"
    print(f"artifacts: {onnx_path.name}, {native_path.name}")


if __name__ == "__main__":
    sys.exit(main())
