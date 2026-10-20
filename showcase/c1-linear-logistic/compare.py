# C1 -- run the linear/logistic showcase and compare it with the PyTorch counterpart.
#
# The script generates deterministic data (make_data.py), runs both implementations on both
# problems, checks the numbers against the tolerances below, and writes RESULTS.md.  It exits
# non-zero when a bound is exceeded, so the plan entry fails with it.
#
# Profiles: `small` is the one the slow test plan runs (a few hundred steps, seconds); the
# `standard` profile produces the numbers quoted in the README.

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

TOL_INIT_LOSS = 1e-5       # relative
TOL_STEP_LOSS = 2e-2       # relative, every logged step
TOL_VAL_LOSS = 1e-2        # relative
TOL_METRIC = 1e-2          # absolute (MAE for linear, accuracy for logistic)

PROFILES = {
    "small": {"train": 512, "val": 128, "steps": 200, "batch": 32,
              "lr": {"linear": 0.1, "logistic": 0.05}, "log_every": 20},
    "standard": {"train": 512, "val": 128, "steps": 1000, "batch": 32,
                 "lr": {"linear": 0.1, "logistic": 0.05}, "log_every": 100},
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


def camel_command(camel: Path, data: Path, model: str, profile: dict, passes: list[str]) -> list[str]:
    return [
        str(camel), str(HERE / "train.cml"),
        "--arg", f"data={data / model}",
        "--arg", f"model={model}",
        "--arg", f"steps={profile['steps']}",
        "--arg", f"batch={profile['batch']}",
        "--arg", f"lr={profile['lr'][model]}",
        "--arg", f"log_every={profile['log_every']}",
        "--arg", "threads=1",
        *passes,
    ]


def torch_command(data: Path, model: str, profile: dict) -> list[str]:
    return [
        sys.executable, str(HERE / "baseline" / "train_torch.py"),
        "--data", str(data / model), "--model", model,
        "--steps", str(profile["steps"]), "--batch", str(profile["batch"]),
        "--lr", str(profile["lr"][model]), "--log_every", str(profile["log_every"]),
        "--threads", "1",
    ]


def parse_run(stdout: str) -> dict:
    config = parse_fields(stdout, "c1 config")[0]
    init = parse_fields(stdout, "c1 init")[0]
    trained = parse_fields(stdout, "c1 trained")[0]
    steps = {}
    for line in stdout.splitlines():
        match = re.match(r"c1 step=(\d+) loss=([0-9eE.+-]+)\s*$", line)
        if match:
            steps[int(match.group(1))] = float(match.group(2))
    return {
        "config": config,
        "init_loss": float(init["loss"]),
        "init_metric": float(init["metric"]),
        "steps": steps,
        "final_loss": float(trained["loss"]),
        "final_metric": float(trained["metric"]),
        "seconds": float(trained["seconds"]),
    }


def relative(a: float, b: float) -> float:
    return abs(a - b) / max(abs(a), abs(b), 1e-12)


def check_pair(model, camel, torch) -> list[dict]:
    rows = []
    rows.append(("initial validation loss", camel["init_loss"], torch["init_loss"],
                 relative(camel["init_loss"], torch["init_loss"]), "1e-05 rel",
                 relative(camel["init_loss"], torch["init_loss"]) <= TOL_INIT_LOSS))
    worst = max(camel["steps"], key=lambda step: relative(camel["steps"][step], torch["steps"].get(step, 0.0)))
    worst_rel = relative(camel["steps"][worst], torch["steps"].get(worst, 0.0))
    rows.append((f"training loss over the logged steps (worst: step {worst})",
                 camel["steps"][worst], torch["steps"].get(worst, float("nan")), worst_rel,
                 "2e-02 rel", worst_rel <= TOL_STEP_LOSS))
    rows.append(("final validation loss", camel["final_loss"], torch["final_loss"],
                 relative(camel["final_loss"], torch["final_loss"]), "1e-02 rel",
                 relative(camel["final_loss"], torch["final_loss"]) <= TOL_VAL_LOSS))
    metric_diff = abs(camel["final_metric"] - torch["final_metric"])
    rows.append((f"final validation {'MAE' if model == 'linear' else 'accuracy'}",
                 camel["final_metric"], torch["final_metric"], metric_diff, "1e-02 abs",
                 metric_diff <= TOL_METRIC))
    return rows


def code_size() -> list[tuple]:
    camel = section_counts(HERE / "train.cml", "==", "camel")
    torch = section_counts(HERE / "baseline" / "train_torch.py", "==", "python")
    sections = [name for name in camel if name in torch]
    sections += [name for name in camel if name not in torch]
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
    runs = {}
    for model in ("linear", "logistic"):
        c = run_command(camel_command(camel, data, model, profile, ["std::macro", "std::nvm"]),
                        camel_env(REPO, 1))
        t = run_command(torch_command(data, model, profile), python_env(1))
        if c["exit"] != 0:
            sys.stderr.write(c["stdout"] + c["stderr"])
            raise SystemExit(f"camel failed for {model}")
        if t["exit"] != 0:
            sys.stderr.write(t["stdout"] + t["stderr"])
            raise SystemExit(f"PyTorch failed for {model}")
        runs[model] = (c, t, parse_run(c["stdout"]), parse_run(t["stdout"]))

    compile_times = []
    for _ in range(3):
        c = run_command(camel_command(camel, data, "linear", profile,
                                      ["std::macro", "std::stats", "std::null"]), camel_env(REPO, 1))
        compile_times.append(c["seconds"])
    compile_time = statistics.median(compile_times)

    all_rows = []
    failures = 0
    for model in ("linear", "logistic"):
        c, t, cr, tr = runs[model]
        rows = check_pair(model, cr, tr)
        all_rows.append((model, rows))
        failures += sum(1 for row in rows if not row[-1])

    version = run_command([str(camel), "--version"], camel_env(REPO, 1))
    camel_version = version["stdout"].strip() or version["stderr"].strip()

    lines = []
    lines.append("# C1 — linear and logistic regression: measured results")
    lines.append("")
    lines.append("Generated by `showcase/c1-linear-logistic/compare.py`; do not edit by hand.")
    lines.append("")
    lines.append(f"- date: {dt.datetime.now().astimezone().isoformat(timespec='seconds')}")
    lines.append(f"- Camel: {camel_version}")
    lines.append(f"- PyTorch: {__import__('torch').__version__}, Python {sys.version.split()[0]}, "
                 f"NumPy {__import__('numpy').__version__}")
    lines.append(f"- command: `{' '.join([Path(sys.executable).name, *sys.argv])}`")
    lines.append(f"- profile: {args.profile}; {profile['steps']} steps, batch {profile['batch']}, "
                 f"lr {profile['lr']['linear']} (linear) / {profile['lr']['logistic']} "
                 f"(logistic), threads 1; train {profile['train']}, val {profile['val']}")
    lines.append("")
    for model, rows in all_rows:
        lines.append(f"## {model}")
        lines.append("")
        lines.append(table(
            [(name, f"{a:.9f}", f"{b:.9f}", f"{diff:.3e}", tol, "PASS" if ok else "FAIL")
             for name, a, b, diff, tol, ok in rows],
            ("metric", "Camel", "PyTorch", "difference", "tolerance", "result")))
        lines.append("")
        c, t, cr, tr = runs[model]
        lines.append(table([
            ("training time (s)", f"{cr['seconds']:.6f}", f"{tr['seconds']:.6f}",
             f"{cr['seconds'] / tr['seconds']:.3f}"),
            ("throughput (samples/s)", f"{profile['steps'] * profile['batch'] / cr['seconds']:.1f}",
             f"{profile['steps'] * profile['batch'] / tr['seconds']:.1f}",
             f"{(cr['seconds'] / tr['seconds']):.3f}"),
            ("peak process memory (MiB)", f"{c['peak_bytes'] / 2**20:.1f}",
             f"{t['peak_bytes'] / 2**20:.1f}", f"{c['peak_bytes'] / t['peak_bytes']:.3f}"),
            ("compile time (s)", f"{compile_time:.3f}", "n/a (eager)", "n/a"),
            ("whole process wall time (s)", f"{c['seconds']:.3f}", f"{t['seconds']:.3f}",
             f"{c['seconds'] / t['seconds']:.3f}"),
        ], ("metric", "Camel", "PyTorch", "Camel / PyTorch")))
        lines.append("")

    lines.append("## Code size (code lines by section)")
    lines.append("")
    lines.append(table(code_size(), ("section", "Camel", "PyTorch")))
    lines.append("")
    lines.append("Tolerance: the initial loss only differs by the order of the dot product and "
                 "the bias add, hence 1e-05 relative.  Over a training run small differences "
                 "compound through the optimizer, so every logged loss is bounded at 2e-02 "
                 "relative, the final loss at 1e-02 relative, and the final metric at 1e-02 "
                 "absolute.  The logged bound is wider because logistic regression at lr 0.05 "
                 "amplifies a per-step difference of order 1e-7 within a few steps; the worst "
                 "logged gap is reported above.  There is no bit-for-bit equality between two "
                 "implementations (design principles 7).")
    lines.append("")
    text = "\n".join(lines) + "\n"
    write_results(results, text)

    sys.stdout.write("\n" + text)
    print(f"c1 compare: {'FAIL' if failures else 'PASS'} ({failures} bound(s) exceeded)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
