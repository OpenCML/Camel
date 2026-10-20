"""Shared plumbing for the showcase comparison scripts.

Every showcase example runs a Camel program and a PyTorch program on the same input, then
prints a table.  This module holds the parts that must behave identically in all of them:
process measurement (peak working set of the whole tree), the environment variables that pin
thread counts, the line counter used for the side-by-side size table, and the section marker
conventions.

It is imported by the example `compare.py` scripts (they put `showcase/` on `sys.path`), not
run on its own.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import psutil
except ImportError:  # pragma: no cover - the plan's skip_unless reports the real error
    psutil = None


def tree_peak_bytes(pid: int) -> int:
    """Peak working set of a process and its descendants.

    A virtual-environment `python.exe` on Windows is a launcher that runs the real interpreter
    as a child, so measuring only the started pid would report the launcher's few MiB.
    `peak_wset` is the lifetime peak of each process; summing the peaks of a small tree can
    over-report by the shared runtime, which is noted in RESULTS.md.
    """
    if psutil is None:
        return 0
    try:
        root = psutil.Process(pid)
        processes = [root] + root.children(recursive=True)
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return 0
    total = 0
    for process in processes:
        try:
            info = process.memory_info()
            total += getattr(info, "peak_wset", 0) or info.rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return total


def run_command(command: list[str], env: dict[str, str], cwd: Path | None = None,
                timeout: float | None = None) -> dict:
    """Run a command, sampling the peak resident set of the child and its children."""
    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
        cwd=str(cwd) if cwd is not None else None,
    )
    peak = 0
    while process.poll() is None:
        peak = max(peak, tree_peak_bytes(process.pid))
        time.sleep(0.02)
    stdout, stderr = process.communicate(timeout=timeout)
    return {
        "exit": process.returncode,
        "stdout": stdout,
        "stderr": stderr,
        "seconds": time.perf_counter() - started,
        "peak_bytes": peak,
    }


def camel_env(repo: Path, threads: int) -> dict[str, str]:
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(repo / "out" / "latest")
    env["PATH"] = str(repo / "out" / "latest" / "bin") + os.pathsep + env.get("PATH", "")
    env["CAMEL_NUM_THREADS"] = str(threads)
    return env


def python_env(threads: int) -> dict[str, str]:
    env = os.environ.copy()
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        env[name] = str(threads)
    return env


def is_code_line(line: str, language: str) -> bool:
    stripped = line.strip()
    if not stripped:
        return False
    if language == "camel":
        return not stripped.startswith("//")
    if language == "python":
        return not stripped.startswith("#")
    raise ValueError(language)


def section_counts(path: Path, marker: str, language: str) -> dict[str, int]:
    """Count code lines per `== section ==` block.

    The count skips comments and, for Python, docstrings and block comments.  A section starts
    at a comment line of the form `// == name ==` (or `# == name ==`) and runs to the next one.
    Lines before the first marker are counted under `header`.
    """
    if not path.exists():
        return {}
    pattern = re.compile(r"^\s*(?://|#)\s*==\s*([A-Za-z0-9_-]+)\s*==\s*$")
    counts: dict[str, int] = {"header": 0}
    section = "header"
    in_docstring = False
    in_block = False
    doc_quote = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        match = pattern.match(raw)
        if match:
            section = match.group(1)
            counts.setdefault(section, 0)
            continue
        if language == "python":
            if in_block:
                if "*/" in line:
                    in_block = False
                continue
            if in_docstring:
                if doc_quote in line:
                    in_docstring = False
                continue
            if line.startswith('"""') or line.startswith("'''"):
                doc_quote = line[:3]
                if line.count(doc_quote) == 1:
                    in_docstring = True
                continue
            if line.startswith("/*"):
                if "*/" not in line:
                    in_block = True
                continue
            if line.startswith("#") or not line:
                continue
        elif not is_code_line(raw, language):
            continue
        counts[section] = counts.get(section, 0) + 1
    return counts


def table(rows: list[tuple], header: tuple) -> str:
    lines = ["| " + " | ".join(str(cell) for cell in header) + " |"]
    lines.append("|" + "|".join("---" for _ in header) + "|")
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def write_results(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def parse_fields(stdout: str, prefix: str) -> list[dict[str, str]]:
    """Parse `prefix key=value key=value` lines into dicts."""
    fields = []
    for line in stdout.splitlines():
        if not line.startswith(prefix + " "):
            continue
        record = {}
        for item in line[len(prefix):].split():
            if "=" in item:
                key, value = item.split("=", 1)
                record[key] = value
        fields.append(record)
    return fields