# D5 -- run the Tree-LSTM showcase and compare it with the PyTorch counterpart.
#
# Generates the synthetic four-leaf trees, runs both recursive implementations on the same
# arrays, checks the losses and the validation accuracy, and writes RESULTS.md.  It exits
# non-zero when a bound is exceeded, so the slow plan entry fails with it.

from __future__ import annotations

import argparse
import datetime as dt
import re
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "showcase"))

from common.showcase_compare import (  # noqa: E402
    camel_env, parse_fields, python_env, run_command, section_counts, table, write_results,
)

TOL_STEP_LOSS = 1e-2
TOL_ACCURACY = 1e-2

PROFILES = {
    "small": {"train": 256, "val": 64, "steps": 200, "lr": 0.01, "log_every": 20},
    "standard": {"train": 256, "val": 64, "steps": 1000, "lr": 0.01, "log_every": 100},
}


def camel_executable(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    return REPO / "out" / "latest" / "bin" / ("camel.exe" if sys.platform == "win32" else "camel")


def prepare_data(data: Path, profile: dict) -> None:
    result = run_command([
        sys.executable, str(HERE / "make_data.py"), "--out", str(data),
        "--train", str(profile["train"]), "--val", str(profile["val"]),
    ], python_env(1))
    if result["exit"] != 0:
        sys.stderr.write(result["stdout"] + result["stderr"])
        raise SystemExit("make_data.py failed")
    sys.stdout.write(result["stdout"])


def camel_command(camel: Path, data: Path, profile: dict, passes: list[str]) -> list[str]:
    return [
        str(camel), str(HERE / "train.cml"),
        "--arg", f"data={data}",
        "--arg", f"steps={profile['steps']}",
        "--arg", f"lr={profile['lr']}",
        "--arg", f"log_every={profile['log_every']}",
        "--arg", "threads=1",
        *passes,
    ]


def torch_command(data: Path, profile: dict) -> list[str]:
    return [
        sys.executable, str(HERE / "baseline" / "train_torch.py"),
        "--data", str(data),
        "--steps", str(profile["steps"]), "--lr", str(profile["lr"]),
        "--log_every", str(profile["log_every"]), "--threads", "1",
    ]


def parse_run(stdout: str) -> dict:
    init = parse_fields(stdout, "d5 init")[0]
    trained = parse_fields(stdout, "d5 trained")[0]
    steps = {}
    for line in stdout.splitlines():
        match = re.match(r"d5 step=(\d+) loss=([0-9eE.+-]+)\s*$", line)
        if match:
            steps[int(match.group(1))] = float(match.group(2))
    return {
        "init_accuracy": float(init["val_accuracy"]),
        "steps": steps,
        "final_accuracy": float(trained["val_accuracy"]),
        "seconds": float(trained["seconds"]),
    }


def relative(a: float, b: float) -> float:
    return abs(a - b) / max(abs(a), abs(b), 1e-12)


def code_size() -> list[tuple]:
    camel = section_counts(HERE / "train.cml", "==", "camel")
    torch = section_counts(HERE / "baseline" / "train_torch.py", "==", "python")
    sections = list(camel) + [name for name in torch if name not in camel]
    rows = [(name, camel.get(name, 0), torch.get(name, 0)) for name in sections]
    rows.append(("total", sum(camel.values()), sum(torch.values())))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="small")
    parser.add_argument("--data", default=None)
    parser.add_argument("--results", default=None)
    parser.add_argument("--camel", default=None)
    args = parser.parse_args()
    profile = PROFILES[args.profile]
    data = Path(args.data or (HERE / "artifacts" / args.profile))
    results = Path(args.results or (HERE / "RESULTS.md"))
    camel = camel_executable(args.camel)

    prepare_data(data, profile)
    c = run_command(camel_command(camel, data, profile, ["std::macro", "std::nvm"]), camel_env(REPO, 1))
    t = run_command(torch_command(data, profile), python_env(1))
    for name, run in (("Camel", c), ("PyTorch", t)):
        if run["exit"] != 0:
            sys.stderr.write(run["stdout"] + run["stderr"])
            raise SystemExit(f"{name} failed")
    cr, tr = parse_run(c["stdout"]), parse_run(t["stdout"])

    # The first logged step is the shared starting point; compare it not with a separate
    # initial line (the example reports accuracy there) but with the first training loss.
    compile_times = []
    for _ in range(3):
        compile_times.append(run_command(
            camel_command(camel, data, profile, ["std::macro", "std::stats", "std::null"]),
            camel_env(REPO, 1))["seconds"])
    compile_time = statistics.median(compile_times)

    worst = max(cr["steps"], key=lambda step: relative(cr["steps"][step], tr["steps"].get(step, 0.0)))
    worst_rel = relative(cr["steps"][worst], tr["steps"].get(worst, 0.0))
    accuracy_diff = abs(cr["final_accuracy"] - tr["final_accuracy"])
    rows = [
        ("initial validation accuracy", f"{cr['init_accuracy']:.9f}", f"{tr['init_accuracy']:.9f}",
         f"{abs(cr['init_accuracy'] - tr['init_accuracy']):.3e}", "1e-02 abs",
         "PASS" if abs(cr["init_accuracy"] - tr["init_accuracy"]) <= TOL_ACCURACY else "FAIL"),
        (f"training loss over the logged steps (worst: step {worst})",
         f"{cr['steps'][worst]:.9f}", f"{tr['steps'].get(worst, 0):.9f}", f"{worst_rel:.3e}",
         "1e-02 rel", "PASS" if worst_rel <= TOL_STEP_LOSS else "FAIL"),
        ("final validation accuracy", f"{cr['final_accuracy']:.9f}", f"{tr['final_accuracy']:.9f}",
         f"{accuracy_diff:.3e}", "1e-02 abs",
         "PASS" if accuracy_diff <= TOL_ACCURACY else "FAIL"),
    ]
    failures = sum(1 for row in rows if row[-1] == "FAIL")

    version = run_command([str(camel), "--version"], camel_env(REPO, 1))
    lines = []
    lines.append("# D5 — Tree-LSTM sentiment: measured results")
    lines.append("")
    lines.append("Generated by `showcase/d5-tree-lstm/compare.py`; do not edit by hand.")
    lines.append("")
    lines.append(f"- date: {dt.datetime.now().astimezone().isoformat(timespec='seconds')}")
    lines.append(f"- Camel: {version['stdout'].strip() or version['stderr'].strip()}")
    lines.append(f"- PyTorch: {__import__('torch').__version__}, Python {sys.version.split()[0]}, "
                 f"NumPy {__import__('numpy').__version__}")
    lines.append(f"- command: `{' '.join([Path(sys.executable).name, *sys.argv])}`")
    lines.append(f"- profile: {args.profile}; {profile['steps']} steps, lr {profile['lr']}, "
                 f"one sample per step, threads 1; train {profile['train']}, val {profile['val']}")
    lines.append("")
    lines.append("## Agreement")
    lines.append("")
    lines.append(table(rows, ("metric", "Camel", "PyTorch", "difference", "tolerance", "result")))
    lines.append("")
    lines.append("## Performance")
    lines.append("")
    lines.append(table([
        ("training time (s)", f"{cr['seconds']:.6f}", f"{tr['seconds']:.6f}",
         f"{cr['seconds'] / tr['seconds']:.3f}"),
        ("throughput (samples/s)", f"{profile['steps'] / cr['seconds']:.1f}",
         f"{profile['steps'] / tr['seconds']:.1f}", f"{cr['seconds'] / tr['seconds']:.3f}"),
        ("peak process memory (MiB)", f"{c['peak_bytes'] / 2**20:.1f}",
         f"{t['peak_bytes'] / 2**20:.1f}", f"{c['peak_bytes'] / t['peak_bytes']:.3f}"),
        ("compile time (s)", f"{compile_time:.3f}", "n/a (eager)", "n/a"),
        ("whole process wall time (s)", f"{c['seconds']:.3f}", f"{t['seconds']:.3f}",
         f"{c['seconds'] / t['seconds']:.3f}"),
    ], ("metric", "Camel", "PyTorch", "Camel / PyTorch")))
    lines.append("")
    lines.append("Tolerance: the initial accuracy is a discrete count, so it must agree exactly "
                 "in practice and is bounded at 1e-02 absolute; logged losses are bounded at "
                 "1e-02 relative and the final accuracy at 1e-02 absolute.  The two "
                 "implementations are not bit-identical (design principles 7).")
    lines.append("")
    lines.append("## Code size (code lines by section)")
    lines.append("")
    lines.append(table(code_size(), ("section", "Camel", "PyTorch")))
    lines.append("")
    text = "\n".join(lines) + "\n"
    write_results(results, text)
    sys.stdout.write("\n" + text)
    print(f"d5 compare: {'FAIL' if failures else 'PASS'} ({failures} bound(s) exceeded)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())