"""Checks the allocation row of `std::profile` (camel/profile/allocation.h).

  count  two `zeros` of 1024 f32 and their `add` allocate exactly three data blocks of 4096
         bytes. The profile must report exactly three block payloads and 12288 bytes, and the
         per-operator rows must carry two for `tensor:zeros` and one for `tensor:add`. The
         counts are deterministic; the test asserts no duration.

  step   one loop round allocates exactly one block of 4096 bytes, and the `zeros` before the
         loop is the ninth block of the run. The aggregate row, the per-step row and the sum
         of the operator rows must all agree; this is the aggregate guard.

  off    no pass, and `std::profile report=false hints=false`, produce byte-identical stdout:
         the instrument is off by default and the program's own output does not change.

A broken instrument that reports zero allocations fails `count` and `step`: both check the
expected positive counts, not merely the shape of the row.

Usage: python profile_alloc.py count|step|off [--camel PATH]   (needs only the standard library)
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402


# The install this case measures: `--camel <path>` names it; without it this tree's `out/latest/bin/camel`. One rule, in one place: test/tools/camel_arg.py.
# `ARGV` is this process's arguments without the `--camel` pair.
CAMEL, ARGV = camel_from_argv(REPO_ROOT)


def camel_exe() -> str:
    return CAMEL


def run(program: Path, passes: list[str], report: Path | None = None) -> tuple[int, str]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(CAMEL).resolve().parents[1]))
    if report is not None and report.exists():
        report.unlink()
    args = [camel_exe(), str(program)]
    if report is not None:
        args.extend(["std::profile", "report=true", f'json="{report.as_posix()}"', "top=0"])
    args.extend(passes)
    proc = subprocess.run(args, env=env, capture_output=True, text=True, cwd=REPO_ROOT)
    return proc.returncode, proc.stdout


def profile_json(program: Path, passes: list[str]) -> tuple[int, str, dict]:
    with tempfile.TemporaryDirectory() as t:
        report = Path(t) / "profile.json"
        code, out = run(program, passes, report)
        if not report.exists():
            raise SystemExit(f"std::profile wrote no JSON report:\n{out}")
        return code, out, json.loads(report.read_text(encoding="utf-8"))


def rows_by_uri(doc: dict) -> dict[str, dict]:
    return {row["uri"]: row for row in doc["operator_rows"]}


def text_value(text: str, label: str) -> str:
    match = re.search(rf"^\s+{re.escape(label)}\s+(.+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else ""


def main() -> int:
    mode = ARGV[0]
    failures: list[str] = []

    def check(cond: bool, what: str) -> None:
        print(f"{'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    count_program = HERE / "alloc_count.cml"
    step_program = HERE / "alloc_step.cml"

    if mode == "count":
        code, out, doc = profile_json(count_program, ["std::nvm"])
        check(code == 0, f"exit code 0 (got {code})")
        rows = rows_by_uri(doc)
        zeros = rows.get("tensor:zeros", {})
        add = rows.get("tensor:add", {})
        check(zeros.get("alloc_blocks") == 2, f"tensor:zeros allocated 2 blocks (got {zeros.get('alloc_blocks')})")
        check(zeros.get("alloc_bytes") == 2 * 4096, f"tensor:zeros asked for 8192 bytes (got {zeros.get('alloc_bytes')})")
        check(add.get("alloc_blocks") == 1, f"tensor:add allocated 1 block (got {add.get('alloc_blocks')})")
        check(add.get("alloc_bytes") == 4096, f"tensor:add asked for 4096 bytes (got {add.get('alloc_bytes')})")
        alloc = doc["allocations"]
        check(alloc["inside_blocks"] == 3, f"aggregate: 3 block payloads inside operators (got {alloc['inside_blocks']})")
        check(alloc["inside_bytes"] == 3 * 4096, f"aggregate: 12288 bytes (got {alloc['inside_bytes']})")
        check(alloc["inside_calls"] > 0, "aggregate: the allocators were actually entered")
        check(
            "block payloads" in text_value(out, "allocations"),
            "the text report prints the aggregate allocation row",
        )
    elif mode == "step":
        code, out, doc = profile_json(step_program, ["std::nvm"])
        check(code == 0, f"exit code 0 (got {code})")
        alloc = doc["allocations"]
        check(alloc["inside_blocks"] == 9, f"aggregate: 9 block payloads (got {alloc['inside_blocks']})")
        check(alloc["inside_bytes"] == 9 * 4096, f"aggregate: 36864 bytes (got {alloc['inside_bytes']})")
        step = doc["step"]["allocations_per_round"]
        check(step["blocks"] == 1.0, f"one block payload per step (got {step['blocks']})")
        check(step["bytes"] == 4096.0, f"4096 payload bytes per step (got {step['bytes']})")
        blocks = sum(row["alloc_blocks"] for row in doc["operator_rows"])
        check(blocks == alloc["inside_blocks"], f"the rows sum to the aggregate ({blocks} vs {alloc['inside_blocks']})")
        line = text_value(out, "allocations per step")
        check(
            line.startswith("4.0 calls (1.0 block payloads, 4.0 KiB payload"),
            f"the text step row reports one 4 KiB block (got {line!r})",
        )
        check(
            "block payloads" in text_value(out, "allocations"),
            "the text report prints the aggregate allocation row",
        )
    elif mode == "off":
        plain = run(count_program, [])
        quiet = run(count_program, ["std::profile", "report=false", "hints=false"])
        loud = run(count_program, ["std::profile"])
        check(plain[0] == 0 and quiet[0] == 0 and loud[0] == 0, "all three runs exit 0")
        check(plain[1] == quiet[1], "no pass and report=false hints=false print identical stdout")
        check("PROFILE camel" not in quiet[1], "the off session prints no report")
        check("allocations" not in quiet[1], "the off session prints no allocation row")
        check(loud[1].startswith(plain[1]), "with the pass, the program's own output comes first and is unchanged")
        check("PROFILE camel" in loud[1], "a session that is on does print the report")
    else:
        raise SystemExit(f"unknown mode {mode!r}")

    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("profile allocations: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())