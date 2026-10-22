#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""pipeline-runner: run Camel programs under a set of pass pipelines, one fresh process each.

For every cell (program, pipeline, repeat) it writes one JSON run record:

    <out>/records/<program-id>/<pipeline-id>.r<k>.json
    <out>/raw/<program-id>/<pipeline-id>.r<k>.stdout | .stderr | .effects.jsonl
    <out>/files/<program-id>/<pipeline-id>.r<k>/...   (kept only for P0.r0 and for ONNX cells)

A record holds the command line, exit code, wall time, the time limit that applied, the
program's own stdout (everything before the JSON diagnostics array; camel is always run with
`--output-format json`), the parsed diagnostics, the effect trace (CAMEL_EFFECT_TRACE), a
digest of every file the program wrote into its per-run directory and, for a pipeline that
ends in `onnx::export`, the result of running the written model on ONNX Runtime
(onnx_gate.py). Raw streams are kept byte for byte. Classification is done separately by
classify.py from the records alone.

Order and time limits (plan C-10). The zero-pass runs (P0) of every program are done first.
A program's time limit is L = max(floor, k * median wall time of its P0 repeats), default
k = 20 and floor = 10 s. A run is killed only at max(2 * L, hang cap) (default hang cap 60 s);
a run that finishes after L is recorded with `over_limit` and is classified RESOURCE_LIMIT,
a killed run is a HANG. P0 itself is killed at `--ref-cap` (300 s); the other cells of a
program whose P0 was killed are not run (their records say `skipped`).

Per-run directory. Environment values of the corpus may name `{tmp}`, a directory that is
empty at the start of each run; what the program writes there is digested into the record.
In the effect payload text the path is replaced by `{tmp}`. When the trace has no payload text
(only `payload_sha1`), the runs of such a program are instead done one after the other with
the same directory path, so that the hash of a payload containing the path is the same in
every run; all other cells run in parallel.

Written files (deviation D7 of the classifier rules). Besides the `.npy` digests, a written
file of at most TEXT_FILE_BYTES that is valid UTF-8 is recorded with its text (per-run paths
replaced as in the output), and a model (`.onnx`) that a program of the agree set wrote under a
native pipeline and that differs by hash from the one its zero-pass run wrote is compared with
that one on ONNX Runtime (onnx_gate.py, same seeded inputs); the result is the file's `vs_ref`.
classify.py uses both to compare an event of a file-writing operator whose payload text was
truncated in the trace. A `.npy` file of more than NPY_FULL_VALUES elements is recorded as a
sample; when numpy is importable and the file differs by hash from the zero-pass run's, all its
elements are compared with that file and the result is stored as `npy.vs_ref`, which classify.py
then uses instead of the sample. The zero-pass run's files (repeat 0) are kept under
files/<program>/P0.r0/ when they include a model or a sampled array.

Which cells exist. Programs of the agree set run under the native pipelines (P0-P12,
R01-R60) and the must-refuse pipelines XU1, XU2; programs with an export entry also under
the ONNX pipelines (E1, R61-R80) and XO1. Entries of the must-refuse set run once, under
their own command line (pipeline id PLAN). The list of cells is written to
`cells[.shard-K-of-N].json` before anything runs; the run fails (exit 1) when a record is
missing at the end.

Usage:
    run.py --corpus corpus.json --out DIR [--pipelines P0,P1,...] [--repeats 3] [--jobs 4]
           [--camel PATH] [--repo-root DIR] [--only SUBSTR] [--shard K/N]
           [--time-factor 20] [--time-floor 10] [--hang-cap 60] [--ref-cap 300]
    run.py --program file.cml --out DIR [...]          (single program, no corpus file)
    run.py --program file.cml --extra-pipeline 'X1=std::macro,std::nope' --out DIR

Paths in the corpus are relative to the repository root, which is also the working directory
of every run (the test harness does the same). The root is two levels above this file unless
`--repo-root` or $CAMEL_REPO_ROOT says otherwise. Nothing here depends on the host layout.
"""

from __future__ import annotations

import argparse
import array
import ast
import concurrent.futures
import hashlib
import json
import os
import platform
import re
import shutil
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Dict, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipelines as pl  # noqa: E402

HERE = Path(__file__).resolve().parent
REPO_ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
NPY_FULL_VALUES = 4096
TEXT_FILE_BYTES = 65536


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


# ---------------------------------------------------------------------------------------------
# Files a program writes
# ---------------------------------------------------------------------------------------------

_NPY_TYPES = {"<f4": "f", "<f8": "d", "<i4": "i", "<i8": "q", "|u1": "B", "|i1": "b", "|b1": "B"}


def npy_digest(data: bytes) -> Optional[dict]:
    """dtype, shape and values of a .npy file (standard library only). Arrays of more than
    NPY_FULL_VALUES elements keep an evenly spaced sample of that many elements plus the sum
    and the sum of absolute values; the classifier then compares those."""
    if data[:6] != b"\x93NUMPY":
        return None
    try:
        major = data[6]
        if major == 1:
            (hlen,) = struct.unpack("<H", data[8:10])
            start = 10
        else:
            (hlen,) = struct.unpack("<I", data[8:12])
            start = 12
        header = ast.literal_eval(data[start:start + hlen].decode("latin-1"))
        descr, shape = header["descr"], list(header["shape"])
        code = _NPY_TYPES.get(descr)
        if code is None or header.get("fortran_order"):
            return {"dtype": descr, "shape": shape, "values": None}
        values = array.array(code)
        values.frombytes(data[start + hlen:])
        if sys.byteorder != "little":
            values.byteswap()
        out = {"dtype": descr, "shape": shape, "count": len(values)}
        if len(values) <= NPY_FULL_VALUES:
            out["values"] = values.tolist()
        else:
            step = len(values) / NPY_FULL_VALUES
            out["sample"] = [values[int(i * step)] for i in range(NPY_FULL_VALUES)]
            out["sum"] = float(sum(values))
            out["abs_sum"] = float(sum(abs(v) for v in values))
        return out
    except Exception:  # noqa: BLE001 - a malformed file is reported by its hash only
        return None


def digest_files(tmp: Path, normalize=None) -> List[dict]:
    """One entry per file under the run directory: name, size, SHA-1; `.npy` files with their
    values (`npy`); other files that are short valid UTF-8 text with their text (`text`, after
    `normalize`, which replaces the per-run paths)."""
    out = []
    for path in sorted(p for p in tmp.rglob("*") if p.is_file()):
        data = path.read_bytes()
        entry = {"name": path.relative_to(tmp).as_posix(), "bytes": len(data),
                 "sha1": hashlib.sha1(data).hexdigest()}
        if path.suffix == ".npy":
            npy = npy_digest(data)
            if npy:
                entry["npy"] = npy
        elif len(data) <= TEXT_FILE_BYTES and b"\x00" not in data:
            try:
                text = data.decode("utf-8")
                entry["text"] = normalize(text) if normalize else text
            except UnicodeDecodeError:
                pass
        out.append(entry)
    return out


# ---------------------------------------------------------------------------------------------
# One run
# ---------------------------------------------------------------------------------------------

class Settings:
    def __init__(self, camel: str, out: Path, bench: Path, commit: str, gate_python: str, tmp_root: Path):
        self.camel, self.out, self.bench, self.commit, self.gate_python = camel, out, bench, commit, gate_python
        self.tmp_root = tmp_root
        self.chain = True   # see `units`; switched off when the trace carries payload text


def fill(text: str, tmp: Path, settings: Settings) -> str:
    return (text.replace("{tmp}", str(tmp)).replace("{bench}", str(settings.bench))
            .replace("{repo}", str(REPO_ROOT)))


def unfill(text: str, tmp: Path, settings: Settings) -> str:
    """Replaces the per-run and per-machine paths by their placeholders, so that two runs of
    the same program print the same text."""
    for value, name in ((str(tmp), "{tmp}"), (str(settings.bench), "{bench}"), (str(REPO_ROOT), "{repo}")):
        if value:
            text = text.replace(value, name)
    return text


def stable_tmp(program: dict) -> bool:
    return any("{tmp}" in v for v in program.get("env", {}).values())


def units(tasks: list, chain: bool = True) -> list:
    """Groups the cells into units of work for the pool: the cells of a program with a stable
    per-run directory form one unit (run sequentially), every other cell is its own unit.
    Longest units first. With `chain` off every cell is its own unit."""
    chains: Dict[str, list] = {}
    out = []
    for task in tasks:
        if chain and stable_tmp(task[0]):
            chains.setdefault(task[0]["id"], []).append(task)
        else:
            out.append([task])
    return sorted(chains.values(), key=len, reverse=True) + out


def stem_of(pipeline_id: str, repeat: int) -> str:
    return f"{pipeline_id}.r{repeat}"


def record_path(out: Path, pid: str, pipeline_id: str, repeat: int) -> Path:
    return out / "records" / pid / f"{stem_of(pipeline_id, repeat)}.json"


def base_record(program: dict, pipeline_id: str, passes: List[str], repeat: int, settings: Settings) -> dict:
    return {
        "program": program["path"],
        "program_id": program["id"],
        "source_sha1": hashlib.sha1((REPO_ROOT / program["path"]).read_bytes()).hexdigest(),
        "corpus": program.get("corpus", "dev"),
        "origin": program.get("origin", ""),
        "role": program.get("role", "agree"),
        "variant": program.get("variant", ""),
        "output_mask": program.get("output_mask", ""),
        "has_export_entry": bool(program.get("onnx")),
        "expect": program.get("expect"),
        "expect_source": program.get("source", ""),
        # D5: what the plan expects of a must-refuse command line (corpus.py); "" in the agree set.
        "expect_kind": program.get("expect_kind", "") if program.get("role") == "must_refuse" else "",
        # D4: the reason the program is on the static exclusion list, or "".
        "exclusion": program.get("exclusion", ""),
        "pipeline_id": pipeline_id,
        "pipeline_kind": pl.kind(pipeline_id, passes),
        "passes": passes,
        "repeat": repeat,
        "env": program.get("env", {}),
        "camel_commit": settings.commit,
        "host": socket.gethostname(),
    }


def write_record(out: Path, record: dict) -> None:
    path = record_path(out, record["program_id"], record["pipeline_id"], record["repeat"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, indent=1), encoding="utf-8")


def run_one(program: dict, pipeline_id: str, passes: List[str], repeat: int, settings: Settings,
            limit_s: float, kill_s: float, ref_wall_ms: Optional[float], ref_info: Optional[dict]) -> dict:
    out, pid = settings.out, program["id"]
    raw_dir = out / "raw" / pid
    raw_dir.mkdir(parents=True, exist_ok=True)
    stem = stem_of(pipeline_id, repeat)
    effects_path = raw_dir / f"{stem}.effects.jsonl"
    if effects_path.exists():
        effects_path.unlink()
    # A program whose environment names files in its per-run directory gets the same directory
    # path in every one of its runs (they are then run one after the other, see `units`), so
    # that an effect payload containing the path is the same text, and the same hash, each time.
    tmp = settings.tmp_root / pid / ("run" if settings.chain and stable_tmp(program) else stem)
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True, exist_ok=True)

    is_onnx = bool(passes) and passes[-1] == pl.ONNX_TERMINAL
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(settings.camel).resolve().parents[1]))
    for key, value in program.get("env", {}).items():
        env[key] = fill(value, tmp, settings)
    # The effect trace is a parameter of `std::profile`, not an environment variable (H81):
    # the pass observes the run, so it goes first in the pipeline and keeps its own report
    # quiet with `report=false` so the program's output is what the run is measuring.
    trace = f'std::profile(effect_trace="{effects_path.as_posix()}",report=false)'
    cmd_passes = [trace, *passes]
    if is_onnx:
        # The model file is a parameter of the pass (design principles 9.3).
        cmd_passes = [*passes[:-1], f'{pl.ONNX_TERMINAL}(out="{(tmp / "pass.onnx").as_posix()}")']

    cmd = [settings.camel, "--output-format", "json", program["path"], *cmd_passes]
    started = time.time()
    timed_out = False
    try:
        proc = subprocess.run(cmd, cwd=REPO_ROOT, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, timeout=kill_s)
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
    output = unfill(output, tmp, settings)

    effects: List[dict] = []
    if effects_path.exists():
        for line in effects_path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                event = {"unparsed": line}
            if isinstance(event.get("payload"), str):
                event["payload"] = unfill(event["payload"], tmp, settings)
            effects.append(event)

    files = digest_files(tmp, lambda text: unfill(text, tmp, settings))
    if (program.get("role", "agree") == "agree" and pl.kind(pipeline_id, passes) == "native"
            and pipeline_id != pl.REFERENCE and ref_info):
        compare_models_with_reference(files, tmp, ref_info, settings)
        compare_arrays_with_reference(files, tmp, ref_info)
    record = base_record(program, pipeline_id, passes, repeat, settings)
    record.update({
        "command": [unfill(c, tmp, settings) for c in cmd],
        "exit_code": exit_code,
        "timed_out": timed_out,
        "time_limit_s": round(limit_s, 3),
        "kill_after_s": round(kill_s, 3),
        "timeout_s": round(kill_s, 3),
        "ref_wall_ms": ref_wall_ms,
        "over_limit": (not timed_out) and wall_ms > limit_s * 1000.0,
        "wall_ms": round(wall_ms, 3),
        "output": output,
        "output_sha1": sha1_text(output),
        "stderr_sha1": sha1_text(stderr),
        "stderr_bytes": len(err_b),
        "diagnostics": diags,
        "effects": effects,
        "files": files,
        "raw": {"stdout": str((raw_dir / f"{stem}.stdout").relative_to(out)).replace("\\", "/"),
                "stderr": str((raw_dir / f"{stem}.stderr").relative_to(out)).replace("\\", "/"),
                "effects": str(effects_path.relative_to(out)).replace("\\", "/")},
    })

    keep = is_onnx or (pipeline_id == pl.REFERENCE and repeat == 0 and any(
        f["name"].endswith(".onnx") or "sample" in (f.get("npy") or {}) for f in files))
    kept_dir = out / "files" / pid / stem
    shutil.rmtree(kept_dir, ignore_errors=True)
    if is_onnx and program.get("role", "agree") == "agree" and pl.kind(pipeline_id, passes) == "onnx":
        record["onnx"] = onnx_gate(program, tmp, ref_info, settings) if (tmp / "pass.onnx").exists() \
            else {"model_written": False}
    if keep and files:
        kept_dir.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(tmp), str(kept_dir))
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    write_record(out, record)
    return record


def compare_arrays_with_reference(files: List[dict], tmp: Path, ref_info: dict) -> None:
    """D7: a written `.npy` file too large to be recorded element by element is compared, on all
    its elements, with the file of the same name of the zero-pass run (repeat 0) when the two
    differ by hash. Adds `vs_ref` to the file's `npy` digest: dtype and shape equality, the
    largest |a-b| and the largest |a-b| / max(1, |a|) (a the reference; NaN equals NaN only),
    and whether that is within the strict (1e-5) and the near (1e-3) tolerance; integer and
    boolean arrays must be equal. Needs numpy; without it the sample stays the only evidence."""
    ref_sha, ref_dir = ref_info.get("file_sha1") or {}, ref_info.get("files_dir", "")
    todo = [e for e in files if "sample" in (e.get("npy") or {}) and e["name"] in ref_sha
            and ref_sha[e["name"]] != e["sha1"] and (Path(ref_dir) / e["name"]).exists()]
    if not todo:
        return
    try:
        import numpy as np  # noqa: PLC0415
    except ImportError:
        return
    for entry in todo:
        try:
            a, b = np.load(Path(ref_dir) / entry["name"]), np.load(tmp / entry["name"])
            verdict = {"dtype_equal": a.dtype == b.dtype, "shape_equal": a.shape == b.shape, "elements": int(a.size)}
            if not (verdict["dtype_equal"] and verdict["shape_equal"]):
                verdict.update(within_strict=False, within_near=False)
            elif not np.issubdtype(a.dtype, np.floating):
                same = bool(np.array_equal(a, b))
                verdict.update(max_abs_err=0.0 if same else None, max_scaled_err=0.0 if same else None,
                               within_strict=same, within_near=same)
            else:
                x, y = a.astype(np.float64).ravel(), b.astype(np.float64).ravel()
                diff = np.abs(x - y)
                diff = np.where(np.isnan(x) & np.isnan(y), 0.0, diff)
                diff = np.where(np.isnan(diff), np.inf, diff)       # NaN on one side only
                diff = np.where(x == y, 0.0, diff)                   # equal infinities
                scaled = diff / np.maximum(1.0, np.where(np.isfinite(x), np.abs(x), 1.0))
                worst = float(scaled.max()) if scaled.size else 0.0
                verdict.update(max_abs_err=float(diff.max()) if diff.size else 0.0, max_scaled_err=worst,
                               within_strict=worst <= 1e-5, within_near=worst <= 1e-3)
            entry["npy"]["vs_ref"] = verdict
        except Exception as e:  # noqa: BLE001 - recorded; the sample stays the evidence
            entry["npy"]["vs_ref_error"] = str(e)[:200]


def compare_models_with_reference(files: List[dict], tmp: Path, ref_info: dict, settings: Settings) -> None:
    """D7: a model a program wrote under a native pipeline is compared by content with the model
    of the same name its zero-pass run (repeat 0) wrote, when the two differ by hash. Adds
    `vs_ref` to the file's entry: `equivalent` is True when every output of the two models agrees
    within the strict tolerance on the gate's seeded inputs, False when one differs or the model
    does not check, load or run, None when the comparison could not be made."""
    ref_sha, ref_dir = ref_info.get("onnx_sha1") or {}, ref_info.get("files_dir", "")
    for entry in files:
        name = entry["name"]
        if not name.endswith(".onnx") or name not in ref_sha or ref_sha[name] == entry["sha1"]:
            continue
        req_path, res_path = tmp / "gate-request.json", tmp / "gate-result.json"
        req_path.write_text(json.dumps({"model": str(tmp / name), "native": None, "ref_output": "",
                                        "ref_model": str(Path(ref_dir) / name)}), encoding="utf-8")
        try:
            subprocess.run([settings.gate_python, str(HERE / "onnx_gate.py"), str(req_path), str(res_path)],
                           capture_output=True, text=True, timeout=300)
            result = json.loads(res_path.read_text(encoding="utf-8"))
        except Exception as e:  # noqa: BLE001 - recorded; the file then stays "differs by hash"
            result = {"available": False, "error": f"onnx_gate.py did not produce a result: {e}"}
        for leftover in (req_path, res_path):
            leftover.unlink(missing_ok=True)
        cmps = result.get("comparisons", [])
        broken = result.get("checker_ok") is False or result.get("load_ok") is False or result.get("run_ok") is False
        if broken:
            verdict = {"equivalent": False, "within_near": False}
        elif not result.get("available", True) or result.get("oracle") != "export_model_artifact" or not cmps:
            verdict = {"equivalent": None, "within_near": None}
        else:
            verdict = {"equivalent": all(c.get("within_strict") for c in cmps),
                       "within_near": all(c.get("within_near") for c in cmps)}
        errors = [c.get("max_abs_err") for c in cmps if c.get("max_abs_err") is not None]
        verdict.update({"max_abs_err": max(errors) if errors else None, "comparisons": len(cmps),
                        "bit_identical_outputs": bool(cmps) and all(c.get("bit_identical") for c in cmps),
                        "oracle": result.get("oracle", ""), "ref_sha1": ref_sha[name],
                        "detail": str(result.get("error", "") or (f"max |diff| {max(errors)}" if errors else ""))[:300]})
        entry["vs_ref"] = verdict


def onnx_gate(program: dict, tmp: Path, ref_info: Optional[dict], settings: Settings) -> dict:
    """Runs the model a pipeline wrote on ONNX Runtime and compares it with the zero-pass run:
    with its printed native values when the corpus declares them (`native`), otherwise with the
    model the zero-pass run itself exported through `export_model` (`export_model_artifact`)."""
    request = {"model": str(tmp / "pass.onnx"), "native": (program.get("onnx") or {}).get("native"),
               "ref_output": (ref_info or {}).get("output", ""),
               "ref_model": (ref_info or {}).get("model", "")}
    req_path, res_path = tmp / "gate-request.json", tmp / "gate-result.json"
    req_path.write_text(json.dumps(request), encoding="utf-8")
    try:
        proc = subprocess.run([settings.gate_python, str(HERE / "onnx_gate.py"), str(req_path), str(res_path)],
                              capture_output=True, text=True, timeout=300)
        result = json.loads(res_path.read_text(encoding="utf-8"))
    except Exception as e:  # noqa: BLE001 - reported in the record, classified UNDECIDED
        result = {"available": False, "error": f"onnx_gate.py did not produce a result: {e}"}
        if "proc" in locals():
            result["stderr"] = proc.stderr[-2000:]
    result["model_written"] = True
    for name in ("gate-request.json",):
        (tmp / name).unlink(missing_ok=True)
    return result


def skipped_record(program: dict, pipeline_id: str, passes: List[str], repeat: int, settings: Settings,
                   reason: str) -> dict:
    record = base_record(program, pipeline_id, passes, repeat, settings)
    record.update({"skipped": reason, "command": [], "exit_code": None, "timed_out": False,
                   "time_limit_s": None, "kill_after_s": None, "timeout_s": None, "ref_wall_ms": None,
                   "over_limit": False, "wall_ms": 0.0, "output": "", "output_sha1": sha1_text(""),
                   "stderr_sha1": sha1_text(""), "stderr_bytes": 0, "diagnostics": None, "effects": [],
                   "files": [], "raw": {}})
    write_record(settings.out, record)
    return record


# ---------------------------------------------------------------------------------------------
# Benchmark inputs
# ---------------------------------------------------------------------------------------------

def prepare_bench(models: List[str], bench: Path) -> None:
    """Generates the benchmark weights and inputs (benchmarks/common/weights.py, seeded) into
    `bench` instead of the repository's git-ignored benchmarks/artifacts/."""
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(REPO_ROOT / "benchmarks"))
    try:
        from common import weights  # noqa: PLC0415
        from common.models import MODELS  # noqa: PLC0415
    except ImportError as e:
        raise SystemExit(f"run.py: the benchmark models need numpy to generate their weights ({e}); "
                         f"install it or build the corpus with --skip-benchmarks")
    weights.ARTIFACTS = bench
    for model in models:
        weights.generate(MODELS[model])


# ---------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    global REPO_ROOT
    ap = argparse.ArgumentParser(description="Run Camel programs under pass pipelines.")
    ap.add_argument("--corpus", help="corpus JSON written by corpus.py")
    ap.add_argument("--program", action="append", default=[], help="a .cml file (repeatable)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--pipelines", default="", help="comma-separated pipeline ids (default: all)")
    ap.add_argument("--extra-pipeline", action="append", default=[],
                    help="ID=pass1,pass2,... (repeatable); adds a pipeline to the set")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--time-factor", type=float, default=20.0, help="k of the time limit k * reference")
    ap.add_argument("--time-floor", type=float, default=10.0, help="lower bound of the time limit, seconds")
    ap.add_argument("--hang-cap", type=float, default=60.0,
                    help="a run is killed at max(2 * limit, this), seconds")
    ap.add_argument("--ref-cap", type=float, default=300.0, help="the zero-pass run is killed after this")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--camel", default="")
    ap.add_argument("--repo-root", default=os.environ.get("CAMEL_REPO_ROOT", ""),
                    help="repository root (default: two levels above this file, or $CAMEL_REPO_ROOT)")
    ap.add_argument("--bench-artifacts", default="", help="where the benchmark weights go (default <out>/bench-artifacts)")
    ap.add_argument("--gate-python", default=sys.executable,
                    help="python with numpy, onnx and onnxruntime, for the ONNX cells")
    ap.add_argument("--only", default="", help="run only programs whose id or path contains one of these (comma-separated)")
    ap.add_argument("--shard", default="", help="K/N: run only the programs of shard K (0-based) of N")
    ap.add_argument("--no-refuse-set", action="store_true", help="leave out the must-refuse entries")
    args = ap.parse_args(argv)
    if args.repo_root:
        REPO_ROOT = Path(args.repo_root).resolve()
    camel = args.camel or default_camel()

    programs: List[dict] = []
    if args.corpus:
        corpus = json.loads(Path(args.corpus).read_text(encoding="utf-8"))
        programs += corpus["programs"]
        if not args.no_refuse_set:
            programs += corpus.get("must_refuse", [])
    for path in args.program:
        rel = os.path.relpath(Path(path).resolve(), REPO_ROOT).replace("\\", "/")
        programs.append({"id": program_id(rel), "path": rel, "env": {}, "corpus": "dev", "origin": "cli",
                         "role": "agree"})
    if args.only:
        needles = [n for n in args.only.split(",") if n]
        programs = [p for p in programs if any(n in p["path"] or n in p["id"] for n in needles)]
    programs.sort(key=lambda p: p["id"])
    shard_tag = ""
    if args.shard:
        k, n = (int(x) for x in args.shard.split("/"))
        if not 0 <= k < n:
            ap.error("--shard K/N needs 0 <= K < N")
        programs = [p for i, p in enumerate(programs) if i % n == k]
        shard_tag = f".shard-{k}-of-{n}"
    if not programs:
        ap.error("no programs: give --corpus or --program")

    pipelines: Dict[str, List[str]] = pl.all_pipelines()
    for spec in args.extra_pipeline:
        pid, _, passes = spec.partition("=")
        pipelines[pid] = [p for p in passes.split(",") if p]
    if args.pipelines:
        wanted = [p for p in args.pipelines.split(",") if p]
        wanted += [s.partition("=")[0] for s in args.extra_pipeline]
        if pl.REFERENCE not in wanted:
            wanted.insert(0, pl.REFERENCE)  # the time limit and the comparison both need it
        pipelines = {k: pipelines[k] for k in dict.fromkeys(wanted)}

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    bench = Path(args.bench_artifacts).resolve() if args.bench_artifacts else out / "bench-artifacts"
    models = sorted({p["bench_model"] for p in programs if p.get("bench_model")})
    if models:
        prepare_bench(models, bench)
    settings = Settings(camel, out, bench, git_commit(), args.gate_python, out / f"tmp{shard_tag}")
    version = subprocess.run([camel, "--version"], capture_output=True, text=True).stdout.strip()
    # camel reads `.opencmlrc` from the working directory and from the program's directory
    # (tools/camel-cli/opencmlrc.cpp); it adds passes before/after the ones on the command line.
    rc_dirs = {REPO_ROOT} | {(REPO_ROOT / p["path"]).parent for p in programs}
    rc_files = sorted(str(d / ".opencmlrc") for d in rc_dirs if (d / ".opencmlrc").exists())
    if rc_files:
        raise SystemExit(f"run.py: {rc_files} would add passes to the command lines (prefix/suffix/"
                         f"fallback); remove it before running the experiment")

    # The cells.
    ref_tasks, other_tasks = [], []
    for prog in programs:
        if prog.get("role", "agree") == "must_refuse":
            for rep in range(args.repeats):
                other_tasks.append((prog, "PLAN", list(prog["passes"]), rep))
            continue
        for pid, passes in pipelines.items():
            if not pl.applies(pid, passes, prog):
                continue
            for rep in range(args.repeats):
                (ref_tasks if pid == pl.REFERENCE else other_tasks).append((prog, pid, passes, rep))
    cells = [[p["id"], pid, rep] for p, pid, _, rep in ref_tasks + other_tasks]
    (out / f"cells{shard_tag}.json").write_text(json.dumps(cells), encoding="utf-8")

    meta = {
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": socket.gethostname(), "platform": platform.platform(), "python": sys.version,
        "cpu_count": os.cpu_count(), "camel": camel, "camel_version": version,
        "camel_commit": settings.commit, "repeats": args.repeats,
        "time_factor": args.time_factor, "time_floor_s": args.time_floor, "hang_cap_s": args.hang_cap,
        "ref_cap_s": args.ref_cap, "jobs": args.jobs, "pipelines": pipelines,
        "expect_refuse": pl.EXPECT_REFUSE, "programs": len(programs),
        "programs_agree": sum(1 for p in programs if p.get("role", "agree") == "agree"),
        "programs_must_refuse": sum(1 for p in programs if p.get("role") == "must_refuse"),
        "cells": len(cells), "shard": args.shard, "random_seed": pl.RANDOM_SEED, "argv": sys.argv,
        "label": "preliminary, container on a dev machine",
    }
    meta_path = out / f"run-meta{shard_tag}.json"
    meta_path.write_text(json.dumps(meta, indent=1), encoding="utf-8")

    done = 0
    total = len(cells)
    lock = threading.Lock()

    def progress() -> None:
        nonlocal done
        with lock:
            done += 1
            if done % 500 == 0 or done == total:
                print(f"[pipeline-runner] {done}/{total} runs", flush=True)

    # Phase 1: the references.
    ref_records: Dict[str, List[dict]] = {}

    def run_refs(unit: list) -> None:
        for prog, pid, passes, rep in unit:
            rec = run_one(prog, pid, passes, rep, settings, args.ref_cap, args.ref_cap, None, None)
            with lock:
                ref_records.setdefault(rec["program_id"], []).append(rec)
            progress()

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for fut in concurrent.futures.as_completed([pool.submit(run_refs, u) for u in units(ref_tasks)]):
            fut.result()

    # When every effect event of the references carries its payload text, the per-run path is
    # replaced by `{tmp}` in that text and payloads are compared as text: the runs of a program
    # need not share one directory path and all cells can run in parallel. With a trace that
    # has only payload hashes, they stay chained (same path, same hash).
    ref_events = [e for recs in ref_records.values() for r in recs for e in r["effects"]]
    settings.chain = not (ref_events and all(isinstance(e.get("payload"), str) for e in ref_events))
    meta["payload_text_in_trace"] = not settings.chain
    meta["chained_programs"] = sorted({p["id"] for p, _, _, _ in other_tasks if stable_tmp(p)}) if settings.chain else []

    # Phase 2: everything else, with the time limit derived from the reference.
    def run_cell(prog, pid, passes, rep):
        if prog.get("role", "agree") == "must_refuse":
            limit = args.time_floor
            return run_one(prog, pid, passes, rep, settings, limit, max(2 * limit, args.hang_cap), None, None)
        refs = ref_records.get(prog["id"], [])
        if any(r["timed_out"] for r in refs):
            return skipped_record(prog, pid, passes, rep, settings,
                                  f"the zero-pass run did not finish within {args.ref_cap} s")
        walls = [r["wall_ms"] for r in refs]
        ref_ms = statistics.median(walls) if walls else None
        limit = max(args.time_floor, args.time_factor * ref_ms / 1000.0) if ref_ms is not None else args.time_floor
        ref0 = next((r for r in refs if r["repeat"] == 0), None)
        ref_info = None
        if ref0 is not None:
            models_written = [f["name"] for f in ref0.get("files", []) if f["name"].endswith(".onnx")]
            ref_dir = out / "files" / prog["id"] / stem_of(pl.REFERENCE, 0)
            ref_info = {"output": ref0["output"],
                        "model": str(ref_dir / models_written[0]) if len(models_written) == 1 else "",
                        "files_dir": str(ref_dir),
                        "file_sha1": {f["name"]: f["sha1"] for f in ref0.get("files", [])},
                        "onnx_sha1": {f["name"]: f["sha1"] for f in ref0.get("files", [])
                                      if f["name"].endswith(".onnx")}}
        return run_one(prog, pid, passes, rep, settings, limit, max(2 * limit, args.hang_cap), ref_ms, ref_info)

    def run_unit(unit: list) -> None:
        for prog, pid, passes, rep in unit:
            run_cell(prog, pid, passes, rep)
            progress()

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for fut in concurrent.futures.as_completed([pool.submit(run_unit, u) for u in units(other_tasks, settings.chain)]):
            fut.result()

    shutil.rmtree(settings.tmp_root, ignore_errors=True)
    missing = [c for c in cells if not record_path(out, c[0], c[1], c[2]).exists()]
    meta["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    meta["runs"] = total
    meta["records_missing"] = len(missing)
    meta_path.write_text(json.dumps(meta, indent=1), encoding="utf-8")
    print(f"[pipeline-runner] records: {total - len(missing)} / expected {total}", flush=True)
    if missing:
        print(f"[pipeline-runner] MISSING, first: {missing[:5]}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
