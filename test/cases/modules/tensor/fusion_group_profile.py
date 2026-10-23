"""Checks what `std::profile` reports for a fusion group, and that the off path is unchanged.

  group  a training step whose VJP chain is a private `vjp_div_neg` group, for three rounds on
         std::nvm. The profile must report exactly one group row: its attribute and value, its
         three members in execution order, three runs and nine operator executions, with a
         positive time of its own. The members' operators must have no operator row (`tensor:neg`
         in particular), the operator rows plus the group's executions must equal the reported
         operator executions, the step must account for the group once per round, and the same
         program on std::fvm (which has no group kernel) must call the three operators exactly
         three more times each: the executions are counted once, not twice and not zero times.

  none   a program with no division and so no group: the report must be the report of the base
         commit, byte for byte after every measured duration, percentage and byte count is
         replaced by a placeholder and the operator rows are put in a stable order (the table is
         sorted by measured time, so its row order is not a fact). The JSON must have no fusion
         key at all, and `report=false` must print exactly what no pass prints.

A guard that examines zero groups fails `group`: the first check is that the case has a group.

Usage: python fusion_group_profile.py group|none   (needs only the standard library)
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
STEP = HERE / "fusion_group_step.cml"
NONE = HERE / "fusion_group_none.cml"
GOLDEN_TEXT = HERE / "fusion_group_none.profile.golden.txt"
GOLDEN_JSON = HERE / "fusion_group_none.profile.golden.json"
# The goldens were captured from the base commit (b43dd5c68). `origin` in the JSON is the
# internal node-origin id, not a source position: it moved when stdlib/tensor.cml gained its
# pass declaration (design principles 9.3), which adds nodes to the module's declaration, and
# the golden records the id this tree produces. The source positions the text report prints are
# unchanged and are checked there.

# The measured numbers a report prints: durations, shares and byte counts. Everything else in the
# report is a count or a name, and must not change.
TIME = re.compile(r"\b\d+(?:\.\d+)?\s?(?:ns|µs|ms|s)\b")
PERCENT = re.compile(r"\b\d+(?:\.\d+)?%")
BYTES = re.compile(r"\b\d+(?:\.\d+)?\s?(?:KiB|MiB|GiB|B)\b")


def camel_exe() -> str:
    override = os.environ.get("CAMEL_EXE")
    if override:
        return override
    return str(REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def run(program: Path, passes: list[str], report: Path | None = None, args: list[str] | None = None):
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    if report is not None and report.exists():
        report.unlink()
    # Relative to the repository: the profile prints the program's path (source position, hints),
    # and the golden was captured with the same relative path.
    shown = os.path.relpath(program, REPO_ROOT).replace(os.sep, "/")
    argv = [camel_exe(), shown]
    argv.extend(args or [])
    if report is not None:
        argv.extend(["std::profile", "report=true", f'json="{report.as_posix()}"', "top=0"])
    argv.extend(passes)
    proc = subprocess.run(argv, env=env, capture_output=True, text=True, cwd=REPO_ROOT, encoding="utf-8")
    return proc.returncode, proc.stdout


def profile_json(program: Path, passes: list[str], args: list[str] | None = None):
    with tempfile.TemporaryDirectory() as t:
        report = Path(t) / "profile.json"
        code, out = run(program, passes, report, args)
        if not report.exists():
            raise SystemExit(f"std::profile wrote no JSON report:\n{out}")
        return code, out, json.loads(report.read_text(encoding="utf-8"))


def rows_by_uri(doc: dict) -> dict[str, dict]:
    return {row["uri"]: row for row in doc.get("operator_rows", [])}


def text_value(text: str, label: str) -> str:
    match = re.search(rf"^\s+{re.escape(label)}\s+(.+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else ""


def row_lines(text: str) -> list[str]:
    """The lines of the operator table, without its two header lines."""
    lines = text.splitlines()
    rows: list[str] = []
    inside = False
    for line in lines:
        if line.startswith("operators ("):
            inside = True
            continue
        if inside:
            if not line.strip():
                break
            if line.startswith("  ") and not line.startswith("   "):
                rows.append(line)
    return rows


def normalize_text(text: str) -> str:
    """The report with its measured numbers replaced and its time-sorted table put in order."""
    masked = [
        BYTES.sub("<bytes>", PERCENT.sub("<pct>", TIME.sub("<time>", line))).replace("\\", "/")
        for line in text.splitlines()
    ]
    result: list[str] = []
    i = 0
    while i < len(masked):
        line = masked[i]
        if line.startswith("operators ("):
            result.append(line)
            i += 1
            result.append(masked[i])  # the second header line
            i += 1
            rows: list[str] = []
            trailing: list[str] = []
            while i < len(masked) and masked[i].strip():
                row = masked[i]
                (trailing if row.startswith("  ...") else rows).append(row)
                i += 1
            # The table is sorted by measured time: its row order is not a fact of the report.
            result.extend(sorted(rows))
            result.extend(trailing)
            continue
        result.append(line)
        i += 1
    # The columns are right-aligned to the width of the measured number, so the padding around a
    # masked value is not a fact of the report either: runs of spaces are collapsed on both sides.
    return re.sub(r"[ \t]{2,}", " ", "\n".join(result)).strip() + "\n"


def normalize_json(node):
    if isinstance(node, dict):
        out = {}
        for key, value in node.items():
            if key in ("ns", "first_ns", "overhead_ns", "run_ns", "wall_per_round_ns") or key.endswith("_ns"):
                out[key] = 0
            else:
                out[key] = normalize_json(value)
        return out
    if isinstance(node, list):
        return [normalize_json(v) for v in node]
    return node


def normalize_doc(doc: dict) -> dict:
    out = normalize_json(doc)
    if isinstance(out.get("operator_rows"), list):
        out["operator_rows"].sort(key=lambda row: row["uri"])
    # `origin` is the internal node-origin id, not a source position. Its absolute value is not a
    # property of this program: it moved 615 -> 626 -> 637 -> 659 across three unrelated merges in
    # one day, each time because a stdlib declaration added nodes, while the report said the same
    # thing about the same graph, and each time this golden was the only red. What is a property is
    # the *identity*: the hint and the loop must carry the same id, i.e. the hint is about that
    # loop. Renumber every distinct id by first appearance (`L0`, `L1`, ...), so the relationship is
    # still compared and an unrelated declaration no longer turns this case red. The absolute id
    # stays in the golden file as a record of what the tree emitted when it was regenerated.
    ids: dict[object, str] = {}

    def renumber(node) -> None:
        if isinstance(node, dict):
            for key, value in node.items():
                if key == "origin":
                    node[key] = ids.setdefault(value, f"L{len(ids)}")
                else:
                    renumber(value)
        elif isinstance(node, list):
            for value in node:
                renumber(value)

    renumber(out)
    return out


def main() -> int:
    mode = sys.argv[1]
    failures: list[str] = []

    def check(cond: bool, what: str) -> None:
        print(f"{'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    if mode == "group":
        steps = ["--arg", "steps=3"]
        nvm_code, nvm_text, nvm = profile_json(STEP, ["std::nvm"], steps)
        fvm_code, _, fvm = profile_json(STEP, ["std::fvm"], steps)
        check(nvm_code == 0, f"nvm exit code 0 (got {nvm_code})")
        check(fvm_code == 0, f"fvm exit code 0 (got {fvm_code})")

        groups = nvm.get("fusion_groups", [])
        check(len(groups) == 1, f"exactly one fusion group ran (got {len(groups)})")
        group = groups[0] if groups else {}
        check(group.get("key") == "tensor.fusion", f"the row names the attribute (got {group.get('key')})")
        check(group.get("value") == "vjp_div_neg", f"and its value (got {group.get('value')})")
        check(
            group.get("members") == ["tensor:divide", "tensor:multiply", "tensor:neg"],
            f"its members in execution order (got {group.get('members')})",
        )
        check(group.get("runs") == 3, f"one run per round (got {group.get('runs')})")
        check(
            group.get("operator_executions") == 9,
            f"three members on three runs is nine executions (got {group.get('operator_executions')})",
        )
        check(group.get("ns", 0) > 0, f"the group has a time of its own (got {group.get('ns')})")
        check(
            0 < group.get("first_ns", 0) <= group.get("ns", 0),
            f"the first run's time is part of the total (got {group.get('first_ns')} of {group.get('ns')})",
        )

        executions = nvm.get("operator_executions", {})
        calls = nvm["operators"]["calls"]
        check(executions.get("calls") == calls, f"the executions' call part is the operator calls ({executions.get('calls')} vs {calls})")
        check(executions.get("fusion_runs") == 3, f"three group runs (got {executions.get('fusion_runs')})")
        check(executions.get("fusion_operators") == 9, f"nine group executions (got {executions.get('fusion_operators')})")
        check(
            executions.get("executions") == calls + 9,
            f"the total is the calls plus the group's nine (got {executions.get('executions')} vs {calls} + 9)",
        )

        rows = rows_by_uri(nvm)
        check("tensor:neg" not in rows, "the chain's neg has no operator row: it did not run")
        counted = sum(row["calls"] for row in nvm.get("operator_rows", []))
        check(
            counted + executions.get("fusion_operators", 0) == executions.get("executions"),
            f"the rows plus the group equal the executions ({counted} + {executions.get('fusion_operators')} vs {executions.get('executions')})",
        )

        step = nvm["step"]
        step_fusion = nvm.get("step_fusion", {})
        check(step_fusion.get("runs_per_round") == 1.0, f"one group run per step (got {step_fusion.get('runs_per_round')})")
        check(step_fusion.get("operators_per_round") == 3.0, f"three group executions per step (got {step_fusion.get('operators_per_round')})")
        check(
            step_fusion.get("executions_per_round") == step["operators_per_round"] + 3.0,
            f"the step's executions add the group once ({step_fusion.get('executions_per_round')} vs {step['operators_per_round']} + 3)",
        )

        # The reference backend has no group kernel: it ran the members as calls. The difference,
        # over the operators the program itself calls, is exactly the group's executions.
        reference = rows_by_uri(fvm)
        delta = 0
        for uri, row in reference.items():
            if uri.startswith(":op/"):
                continue  # the loop's condition: std::fvm makes it an instruction, not a call
            delta += row["calls"] - rows.get(uri, {}).get("calls", 0)
        check(
            delta == executions.get("fusion_operators"),
            f"the unfused backend calls the members exactly as often as the group claims ({delta} vs {executions.get('fusion_operators')})",
        )
        check("fusion_groups" not in fvm, "a backend with no kernel reports no group")

        check(
            "fusion groups" in nvm_text and "tensor.fusion=\"vjp_div_neg\"" in nvm_text,
            "the text report has a row naming the group's attribute and value",
        )
        check(
            "tensor:divide, tensor:multiply, tensor:neg" in nvm_text,
            "the row lists the members' operators",
        )
        check(
            "3 runs (9 operator executions)" in nvm_text,
            "the row says how many times the group ran and what it stood for",
        )
        check("of the run, first" in nvm_text, "the row has a time of its own")
        check(
            text_value(nvm_text, "fusion groups").startswith("3 runs in 1 group (9 operator executions)"),
            f"the header agrees ({text_value(nvm_text, 'fusion groups')!r})",
        )
        step_line = text_value(nvm_text, "operator executions per step")
        per_round = step["operators_per_round"]
        expected = f"{per_round + 3:.0f} ({per_round:.0f} calls, 3 in fusion groups"
        check(
            step_line.startswith(expected),
            f"the step line counts the group once per round (got {step_line!r}, wanted {expected!r})",
        )
        check(
            not any(re.match(r"^  (cpu|cuda)\s+tensor:neg\s", line) for line in row_lines(nvm_text)),
            "the operator table has no neg row",
        )
    elif mode == "none":
        # The same command line the golden was captured with: no scheduling pass named, so the
        # default back end (std::nvm) runs and the report's last phase line is the same.
        steps = ["--arg", "steps=3"]
        code, text = run(NONE, [], None, steps + ["std::profile", "top=0"])
        check(code == 0, f"exit code 0 (got {code})")
        check(
            normalize_text(text) == GOLDEN_TEXT.read_text(encoding="utf-8"),
            "the report is the base commit's report, byte for byte apart from the measured numbers",
        )

        _, _, doc = profile_json(NONE, [], steps)
        check("fusion_groups" not in doc, "the JSON has no fusion group")
        check("operator_executions" not in doc, "the JSON has no operator-executions key")
        check(
            normalize_doc(doc) == json.loads(GOLDEN_JSON.read_text(encoding="utf-8")),
            "the JSON is the base commit's, apart from the measured durations",
        )

        plain = run(NONE, [], None, steps)
        quiet = run(NONE, ["std::profile", "report=false", "hints=false"], None, steps)
        check(plain[0] == 0 and quiet[0] == 0, "both runs exit 0")
        check(plain[1] == quiet[1], "report=false prints exactly what no pass prints")
        check("PROFILE camel" not in quiet[1], "the off session prints no report")
        check("fusion" not in quiet[1], "the off session mentions no fusion")
    else:
        raise SystemExit(f"unknown mode {mode!r}")

    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("fusion group profile: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())