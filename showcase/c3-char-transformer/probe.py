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
    "13_jvp_scalar": "the forward control: `jvp` at one scalar parameter",
    "15_mask_in_f32": "the causal mask, written in f32 with `where`",
    "16_layer_norm_dtype": "the element type `layer_norm` answers with",
    "14_jvp_call": "a call inside the differentiated function",
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


def wall_tag(text):
    """The wall a refusal names, in as few characters as it can be named in."""
    for line in text.splitlines():
        match = re.search(r"has no forward rule for `([A-Za-z_:]+)`", line)
        if match:
            return f"no-rule:{match.group(1)}"
        match = re.search(r"no [a-z]+ rule for `([A-Za-z_:]+)`", line)
        if match:
            return f"no-rule:{match.group(1)}"
        match = re.search(r"/([^/]+\.cml:\d+:\d+): \[(Error|Fault)\]", line)
        if match:
            return match.group(1)
    return "ok" if not text.strip() else "other"


def compact_reason(text, limit=150):
    """A refusal in one line: where it was, what it said, and the name the compiler files it under.

    A run whose output a reader only sees the tail of still has to show which wall each probe hit,
    so this keeps the position, the message and the diagnostic's name, and drops the rest.
    """
    for line in text.splitlines():
        match = re.search(r"/([^/]+\.cml:\d+:\d+): \[(Error|Warn|Fault)\]: (.*)", line)
        if match:
            message = match.group(3).split(" (name=")[0].strip()
            named = re.search(r"name=([A-Za-z_:]+)", line)
            tag = f" [{named.group(1)}]" if named else ""
            return f"{match.group(1)}: {message[:limit]}{tag}"
    for line in text.splitlines():
        if line.strip():
            head = line.strip()
            return head[:limit]
    return "(no output)"


def wrap(line, width):
    """`line` cut into pieces of at most `width` characters, so a reader sees all of it."""
    if not line:
        return [""]
    return [line[at:at + width] for at in range(0, len(line), width)]


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
    version = run_probe(camel, Path("--version"), [])
    print(f"camel binary={camel} version={version['stdout'].strip() or version['stderr'].strip()} "
          f"version_exit={version['exit']}")
    for path in paths:
        result = run_probe(camel, path, passes)
        combined = result["stdout"] + result["stderr"]
        verdict = "ok" if result["exit"] == 0 else "refused"
        print(f"probe {path.stem} exit={result['exit']} seconds={result['seconds']:.3f} "
              f"construct={CONSTRUCTS.get(path.stem, '?')}")
        # A diagnostic is one long line, and a report that truncates it is a report that cannot be
        # acted on: the text is wrapped rather than clipped, so the whole of it survives.
        for line in combined.splitlines():
            for chunk in wrap(line.rstrip(), 96):
                print(f"probe-out {path.stem} {chunk}")
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

    # The inventory, grouped by the wall each probe hit: one group per distinct refusal, with the
    # probes that reached it.  This is the list WALLS.cn.md is written from, and it is last so that
    # a run whose output is shown by its tail still shows the whole of it.
    groups = []
    for reading in readings:
        if reading["verdict"] == "ok":
            continue
        reason = compact_reason(reading["stdout"] + reading["stderr"])
        for group in groups:
            if group["reason"] == reason:
                group["probes"].append(reading["probe"])
                break
        else:
            groups.append({"reason": reason, "probes": [reading["probe"]]})
    for group in groups:
        joined = ",".join(group["probes"])
        for chunk in wrap(f"probes={joined} :: {group['reason']}", 96):
            print(f"probe-group refused {chunk}")
    answered = ",".join(r["probe"] for r in readings if r["verdict"] == "ok")
    print(f"probe-groups total={len(groups)} answered={answered or '(none)'}")
    # One line for the whole inventory: which wall each probe hit, by name.  This is the table
    # WALLS.cn.md is written from and the one thing a run must not lose in a tail.
    spelled = " ".join(f"{r['probe']}={wall_tag(r['stdout'] + r['stderr'])}" for r in readings)
    for chunk in wrap(f"probe-walls {spelled}", 96):
        print(chunk)
    if args.json:
        Path(args.json).write_text(json.dumps({"readings": readings}, indent=2), encoding="utf-8")
        print(f"probes json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())