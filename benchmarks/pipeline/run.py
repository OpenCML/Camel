#!/usr/bin/env python3
"""pipeline-runner: run Camel programs under a set of pass pipelines, one fresh process each.

For every (program, pipeline, repeat) it writes one JSON run record:

    <out>/records/<program-id>/<pipeline-id>.r<k>.json
    <out>/raw/<program-id>/<pipeline-id>.r<k>.stdout | .stderr | .effects.jsonl

A record holds the command line, exit code, timeout flag, wall time, the program's own stdout
(everything before the JSON diagnostics array), the parsed diagnostics (camel is always run
with `--output-format json`), and the effect trace (CAMEL_EFFECT_TRACE). Raw streams are kept
byte for byte. Classification is done separately by classify.py from the records alone.

Usage:
    run.py --corpus corpus.json --out DIR [--pipelines P0,P1,...] [--repeats 3]
           [--timeout 60] [--jobs 4] [--camel PATH] [--only SUBSTR]
    run.py --program file.cml --out DIR [...]          (single program, no corpus file)
    run.py --program file.cml --extra-pipeline 'X1=std::macro,std::nope' --out DIR

Paths in the corpus are relative to the repository root, which is also the working directory
of every run (the test harness does the same). Nothing here depends on the host layout.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import re
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipelines as pl  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def default_camel() -> str:
    override = os.environ.get("CAMEL_EXE")
    if override:
        return override
    return str(REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def sha1_text(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8", "replace")).hexdigest()


def program_id(path: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", path.replace("\\", "/").removesuffix(".cml"))


def split_diagnostics(stdout: str):
    """Returns (program_output, diagnostics or None). The diagnostics array, when present, is
    the last thing camel prints: a line `[`, one object per line, a line `]`."""
    clean = stdout.replace("\r\n", "\n")
    start = clean.rfind("\n[\n{")
    if start < 0 and clean.startswith("[\n{"):
        start = -1
    elif start < 0:
        return clean, None
    body = clean[start + 1:]
    try:
        diags = json.loads(body)
    except json.JSONDecodeError:
        return clean, None
    return clean[: start + 1], diags


def git_commit() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, capture_output=True,
                              text=True).stdout.strip()
    except OSError:
        return ""


def run_one(camel: str, program: dict, pipeline_id: str, passes: List[str], repeat: int,
            out: Path, timeout: float, commit: str) -> dict:
    pid = program["id"]
    raw_dir = out / "raw" / pid
    rec_dir = out / "records" / pid
    raw_dir.mkdir(parents=True, exist_ok=True)
    rec_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{pipeline_id}.r{repeat}"
    effects_path = raw_dir / f"{stem}.effects.jsonl"
    if effects_path.exists():
        effects_path.unlink()

    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(camel).resolve().parents[1]))
    for key, value in program.get("env", {}).items():
        env[key] = value
    env["CAMEL_EFFECT_TRACE"] = str(effects_path)

    cmd = [camel, "--output-format", "json", program["path"], *passes]
    started = time.time()
    timed_out = False
    try:
        proc = subprocess.run(cmd, cwd=REPO_ROOT, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, timeout=timeout)
        exit_code, out_b, err_b = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as e:
        timed_out, exit_code = True, None
        out_b, err_b = e.stdout or b"", e.stderr or b""
    wall_ms = (time.time() - started) * 1000.0

    (raw_dir / f"{stem}.stdout").write_bytes(out_b)
    (raw_dir / f"{stem}.stderr").write_bytes(err_b)
    stdout = ANSI.sub("", out_b.decode("utf-8", "replace"))
    stderr = ANSI.sub("", err_b.decode("utf-8", "replace"))
    output, diags = split_diagnostics(stdout)

    effects: List[dict] = []
    if effects_path.exists():
        for line in effects_path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                effects.append(json.loads(line))
            except json.JSONDecodeError:
                effects.append({"unparsed": line})

    record = {
        "program": program["path"],
        "program_id": pid,
        "source_sha1": hashlib.sha1((REPO_ROOT / program["path"]).read_bytes()).hexdigest(),
        "corpus": program.get("corpus", "dev"),
        "origin": program.get("origin", ""),
        "pipeline_id": pipeline_id,
        "passes": passes,
        "repeat": repeat,
        "command": cmd,
        "env": program.get("env", {}),
        "exit_code": exit_code,
        "timed_out": timed_out,
        "timeout_s": timeout,
        "wall_ms": round(wall_ms, 3),
        "output": output,
        "output_sha1": sha1_text(output),
        "stderr_sha1": sha1_text(stderr),
        "stderr_bytes": len(err_b),
        "diagnostics": diags,
        "effects": effects,
        "raw": {"stdout": str((raw_dir / f"{stem}.stdout").relative_to(out)).replace("\\", "/"),
                "stderr": str((raw_dir / f"{stem}.stderr").relative_to(out)).replace("\\", "/"),
                "effects": str(effects_path.relative_to(out)).replace("\\", "/")},
        "camel_commit": commit,
        "host": socket.gethostname(),
    }
    (rec_dir / f"{stem}.json").write_text(json.dumps(record, indent=1), encoding="utf-8")
    return record


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Run Camel programs under pass pipelines.")
    ap.add_argument("--corpus", help="corpus JSON written by corpus.py")
    ap.add_argument("--program", action="append", default=[], help="a .cml file (repeatable)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--pipelines", default="", help="comma-separated pipeline ids (default: all)")
    ap.add_argument("--extra-pipeline", action="append", default=[],
                    help="ID=pass1,pass2,... (repeatable); adds a pipeline to the set")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds per run")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--camel", default=default_camel())
    ap.add_argument("--only", default="", help="run only programs whose path contains this")
    args = ap.parse_args(argv)

    programs: List[dict] = []
    if args.corpus:
        programs += json.loads(Path(args.corpus).read_text(encoding="utf-8"))["programs"]
    for path in args.program:
        rel = os.path.relpath(Path(path).resolve(), REPO_ROOT).replace("\\", "/")
        programs.append({"id": program_id(rel), "path": rel, "env": {}, "corpus": "dev", "origin": "cli"})
    if args.only:
        programs = [p for p in programs if args.only in p["path"]]
    if not programs:
        ap.error("no programs: give --corpus or --program")

    pipelines: Dict[str, List[str]] = pl.all_pipelines()
    for spec in args.extra_pipeline:
        pid, _, passes = spec.partition("=")
        pipelines[pid] = [p for p in passes.split(",") if p]
    if args.pipelines:
        wanted = [p for p in args.pipelines.split(",") if p]
        wanted += [s.partition("=")[0] for s in args.extra_pipeline]
        pipelines = {k: pipelines[k] for k in dict.fromkeys(wanted)}

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    commit = git_commit()
    version = subprocess.run([args.camel, "--version"], capture_output=True, text=True).stdout.strip()
    meta = {
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": socket.gethostname(), "platform": platform.platform(), "python": sys.version,
        "cpu_count": os.cpu_count(), "camel": args.camel, "camel_version": version,
        "camel_commit": commit, "repeats": args.repeats, "timeout_s": args.timeout,
        "jobs": args.jobs, "pipelines": pipelines, "programs": len(programs),
        "random_seed": pl.RANDOM_SEED, "argv": sys.argv,
    }
    (out / "run-meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")

    tasks = [(prog, pid, passes, rep)
             for prog in programs for pid, passes in pipelines.items() for rep in range(args.repeats)]
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, args.camel, prog, pid, passes, rep, out, args.timeout, commit)
                   for prog, pid, passes, rep in tasks]
        for fut in concurrent.futures.as_completed(futures):
            fut.result()
            done += 1
            if done % 500 == 0 or done == len(tasks):
                print(f"[pipeline-runner] {done}/{len(tasks)} runs", flush=True)

    meta["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    meta["runs"] = len(tasks)
    (out / "run-meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
