"""Run the wall probes of C3 and print what each one does.

Every probe under `probes/` is the smallest program that reaches one construct of a char-level
Transformer -- a matrix product, a softmax, a reduction, a layout change, a mask, an embedding
lookup, a second parameter, a `with` parameter -- through the library's own reverse transform
(`stdlib/vjp.cml`: `vjp(f)(x)` gives `(y, pullback)` and `pullback(ct)` gives one cotangent per
parameter). The output of this script is the reading the wall report is made of, so it says, for
each probe, whether the library answered the gradient or refused, and the refusal verbatim.

Two things this script is not:
  * it is not a training run.  `train.cml` is; this script says what that program can reach.
  * it is not a comparison with PyTorch.  `compare.py` is, for the one block small enough that a
    hand-derived Jacobian and a PyTorch module can both state the answer.

Run (with the Camel install on PATH, or name it):
    python showcase/c3-char-transformer/probe.py
    python showcase/c3-char-transformer/probe.py --camel out/latest/bin/camel --json probes.json
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PROBES = HERE / "probes"

# The construct each probe reaches, so the report can say which wall is which without reading the
# probe's text.  The key is the file stem.
CONSTRUCTS = {
    "01_grad_scalar": "the control: the library `grad` at one scalar parameter",
    "02_grad_table": "an array parameter read by position (an embedding table)",
    "03_matmul": "the matrix product (`@`), the operator of every layer",
    "04_softmax": "softmax over the key axis",
    "05_reduction": "a reduction along an axis (the mean and variance of a layer norm)",
    "06_layout": "reshape and transpose (splitting and joining attention heads)",
    "07_two_parameters": "two parameters: one cotangent per parameter",
    "08_embedding": "a row read out of a table (the lookup itself)",
    "09_causal_mask": "the causal mask (`where`)",
    "10_with_parameter": "a parameter tree as a `with` parameter",
    "11_grad_of_a_model": "`grad` pointed at a model",
    "12_attention_block": "one head of attention, two tokens, one dimension",
}


def camel_executable(explicit):
    if explicit:
        return Path(explicit)
    name = "camel.exe" if os.name == "nt" else "camel"
    return REPO / "out" / "latest" / "bin" / name


def camel_env():
    env = os.environ.copy()
    home = REPO / "out" / "latest"
    env["CAMEL_HOME"] = str(home)
    env["PATH"] = str(home / "bin") + os.pathsep + env.get("PATH", "")
    return env


def first_reason(text):
    """The one line of a refusal a reader needs: the first line that names a diagnostic."""
    interesting = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        interesting.append(line)
    if not interesting:
        return "(no output)"
    # A compiler diagnostic carries a source position and a name; a fault carries "Fault" or
    # "RuntimeError".  Prefer those two over a bare stack frame.
    for line in interesting:
        if re.search(r"\b(error|Error|Fault|fault|refus|no rule|NoMatching|Invalid|Invalid call)\b", line):
            return line
    return interesting[-1]


def run_probe(camel, path, passes):
    command = [str(camel), str(path)] + passes
    started = time.perf_counter()
    process = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=camel_env(),
    )
    seconds = time.perf_counter() - started
    return {
        "exit": process.returncode,
        "stdout": process.stdout,
        "stderr": process.stderr,
        "seconds": seconds,
        "command": command,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--camel", default=None, help="the camel binary (default: out/latest/bin)")
    parser.add_argument("--only", default=None, help="run the probes whose stem contains this")
    parser.add_argument("--json", default=None, help="write the readings here as JSON")
    parser.add_argument(
        "--passes",
        default="std::nvm",
        help="the passes after the case (default: std::nvm; empty for the default pipeline)",
    )
    args = parser.parse_args()

    camel = camel_executable(args.camel)
    if not camel.exists():
        raise SystemExit(f"no camel binary at {camel}")
    passes = [p for p in args.passes.split() if p]

    paths = sorted(p for p in PROBES.glob("*.cml"))
    if args.only:
        paths = [p for p in paths if args.only in p.stem]
    if not paths:
        raise SystemExit(f"no probes under {PROBES}")

    readings = []
    for path in paths:
        result = run_probe(camel, path, passes)
        combined = result["stdout"] + result["stderr"]
        verdict = "ok" if result["exit"] == 0 else "refused"
        print(f"probe {path.stem} exit={result['exit']} seconds={result['seconds']:.3f} "
              f"construct={CONSTRUCTS.get(path.stem, '?')}")
        for line in combined.splitlines():
            if line.strip():
                print(f"probe-out {path.stem} {line.rstrip()}")
        print(f"probe-reason {path.stem} {first_reason(combined)}")
        print(f"probe-verdict {path.stem} {verdict}")
        readings.append({
            "probe": path.stem,
            "construct": CONSTRUCTS.get(path.stem, "?"),
            "exit": result["exit"],
            "seconds": result["seconds"],
            "verdict": verdict,
            "reason": first_reason(combined),
            "stdout": result["stdout"],
            "stderr": result["stderr"],
        })

    ok = sum(1 for r in readings if r["verdict"] == "ok")
    refused = len(readings) - ok
    print(f"probes total={len(readings)} ok={ok} refused={refused} passes={' '.join(passes) or '(default)'}")
    if args.json:
        Path(args.json).write_text(json.dumps({"readings": readings}, indent=2), encoding="utf-8")
        print(f"probes json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())