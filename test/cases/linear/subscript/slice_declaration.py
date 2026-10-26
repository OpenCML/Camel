"""The core's slice node states the positions it takes, and it takes them.

slice_positions.cml holds its own positions ([0, 1, ..., 7]), so what the program prints is the
positions each slice took. This checker reads the same program's declaration for every slice node
(`camel inspect --instances`; the tensor module publishes the declaration of the core's
`:op/slice_sub`, because a slice reads the shape facts of a tensor), works out the positions that
declaration states -- the length of its domain and the access `start + step*d` -- and checks the
kernel printed exactly those. Then it runs the program with a bound that leaves the axis and
checks the interval the declaration states for a bound only the run knows against the fault: the
length in the fault message is the declaration's extent parameter and the bounds are the ones the
call passed.

Why this exists: the declaration and the kernel are written apart (the module states the map, the
core's scheduler takes the positions) and after the tensor module's own slice kernel was deleted
nothing compared the two. Design principles 5.4, docs/design/op-semantics section 12.

Usage: python slice_declaration.py [--camel PATH]
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "slice_positions.cml"
SLICE = ":op/slice_sub"

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

CAMEL, ARGV = camel_from_argv(REPO_ROOT)
HOME = str(Path(CAMEL).resolve().parents[1])

# The call the program makes with bounds only the run knows, and what the fault run makes: the
# checker passes these through the program's own main (below), so they are the bounds the
# declaration's interval has to accept or report.
OPEN_CALL = (1, 7)
FAULT_CALL = (-1, 3)

BAD = 0


def wrong(where: str, why: str) -> None:
    global BAD
    BAD += 1
    print(f"FAIL {where}: {why}")


def camel(*args: str, fault: bool = False):
    env = {**os.environ, "CAMEL_HOME": HOME, "SLICE_FAULT": "1" if fault else "0"}
    return subprocess.run(
        [CAMEL, *args], env=env, capture_output=True, text=True, timeout=180
    )


def instances() -> dict[str, list[str]]:
    """The instance text of every slice node of the program, by graph."""
    proc = camel("inspect", "--instances", str(PROGRAM))
    if proc.returncode != 0:
        raise SystemExit(f"inspect failed (exit {proc.returncode}): {(proc.stdout + proc.stderr)[-400:]}")
    out: dict[str, list[str]] = {}
    for line in proc.stdout.splitlines():
        if line.startswith("INSTANCE "):
            row = json.loads(line[len("INSTANCE ") :])
            if row["uri"] == SLICE and row["instance"]:
                out[row["graph"]] = row["instance"]
    return out


def line_of(inst: list[str], prefix: str) -> str | None:
    return next((l for l in inst if l.startswith(prefix)), None)


def params(inst: list[str]) -> dict[str, tuple[str, int | None]]:
    """{name: (source, value)} from the instance's parameter list: `start = static(2) = 1`."""
    line = line_of(inst, "params")
    if line is None:
        return {}
    out: dict[str, tuple[str, int | None]] = {}
    for item in line[len("params") :].strip().split(";"):
        item = item.strip()
        if not item:
            continue
        name, _, rest = item.partition(" = ")
        known = re.fullmatch(r"(.*) = (-?\d+)", rest)
        out[name] = (known.group(1), int(known.group(2))) if known else (rest, None)
    return out


def access(inst: list[str]) -> str | None:
    """The coordinate expression of the slice's own access, without the parentheses."""
    line = line_of(inst, "norm 0")
    if line is None:
        return None
    match = re.fullmatch(r"norm 0\s+\((.*)\)", line)
    return match.group(1) if match else None


def coefficient(where: str, inst: list[str]) -> int | None:
    """The step the declaration's own access states: `start + step*d0`, `start - d0`, `start + d0`.

    The step stays the named parameter when the declaration gave it a value, and a step of 1 is
    no factor at all (the pool folds it), so the sign is read from the text and the factor from
    the parameter's value: the two are the same statement about the map, and a slice is where a
    step can be negative.
    """
    expr = access(inst)
    if expr is None:
        wrong(where, "the declaration states no access for the sliced operand")
        return None
    match = re.fullmatch(r"start(?:\s*([+-])\s*(.+))?", expr)
    if match is None:
        wrong(where, f"the access reads '{expr}', not a run of positions from the start")
        return None
    if match.group(1) is None:
        return 0
    sign = -1 if match.group(1) == "-" else 1
    rest = match.group(2).strip()
    if rest == "d0":
        return sign
    factor = re.fullmatch(r"([A-Za-z_0-9]+)\*d0", rest)
    if factor is None:
        wrong(where, f"the access reads '{expr}', not a step times the dimension")
        return None
    name = factor.group(1)
    if name.isdigit():
        return sign * int(name)
    step = params(inst).get(name, ("", None))[1]
    if step is None:
        wrong(where, f"'{name}' has no value in the declaration, so the step is not fixed")
        return None
    return sign * step


def positions(where: str, inst: list[str], start: int, step: int, count: int) -> list[int]:
    """The positions the declaration states: its own start and step, over its own length."""
    declared = coefficient(where, inst)
    if declared is None:
        return []
    if declared != step:
        wrong(where, f"the access states a step of {declared}, the declaration says {step}")
        return []
    return [start + step * i for i in range(count)]


def printed_values(where: str, line: str, label: str) -> list[int]:
    """The positions the run printed, from a line `label [a, b, ...]`."""
    rest = line.strip()
    if not rest.startswith(label + " "):
        wrong(where, f"line '{line}' does not start with '{label} '")
        return []
    body = rest[len(label) + 1 :].strip()
    if not (body.startswith("[") and body.endswith("]")):
        wrong(where, f"'{body}' is not a printed array")
        return []
    values = []
    for token in body[1:-1].split(","):
        token = token.strip()
        if token:
            try:
                values.append(int(float(token)))
            except ValueError:
                wrong(where, f"'{token}' is not a number")
                return []
    return values


def compare(where: str, want: list[int], got: list[int]) -> None:
    if want != got:
        wrong(where, f"the declaration states {want}, the kernel took {got}")


def check_declared_bounds(where: str, inst: list[str], count: int, upper: str) -> None:
    """`window` and `backward` write both bounds: the type states the extent, so the declaration
    states each bound's value and the length, and requires the interval instead of checking it."""
    p = params(inst)
    for name in ("start", "step", "len"):
        if name not in p or p[name][1] is None:
            wrong(where, f"'{name}' has no value in the declaration: {p.get(name)}")
    if p.get("step", ("", None))[0] != "static(4)":
        wrong(where, f"the step is not the node's constant operand: {p.get('step')}")
    line = line_of(inst, "domain")
    if line is None or not re.search(r"d0 < len parallel", line):
        wrong(where, f"the domain is not d0 < len parallel: {line}")
    if any(l.startswith("check") for l in inst):
        wrong(where, f"a bound with a value is checked at run time: {line_of(inst, 'check')}")
    where_line = line_of(inst, "where") or ""
    for want in ("0 <= start", f"start <= {upper}", "0 <= len"):
        if want not in where_line:
            wrong(where, f"the interval '{want}' is not required: {where_line}")
    if not any(l.startswith("write    (d0) unique") for l in inst):
        wrong(where, f"the write map is not (d0) unique: {line_of(inst, 'write')}")
    if p.get("len", ("", None))[1] != count:
        wrong(where, f"len is {p.get('len')}, the printed run has {count} positions")


def check_open_bounds(where: str, inst: list[str]) -> tuple[int, int]:
    """`range_of` writes no bound: the declaration states the interval, not the values."""
    p = params(inst)
    n = p.get("s0_0", ("", None))[1]
    if n is None:
        wrong(where, f"the axis' length has no value in the declaration: {p.get('s0_0')}")
        n = 0
    for operand in ("2", "3"):
        if not any(l.startswith(f"check    operand {operand} in [0, s0_0] else fault") for l in inst):
            wrong(where, f"operand {operand} is not checked against [0, s0_0]")
    for name in ("start", "end"):
        if p.get(name, ("runtime", None))[0] != f"runtime({2 if name == 'start' else 3})":
            wrong(where, f"'{name}' is not the run's value: {p.get(name)}")
    return n, p.get("step", ("", None))[1] or 1


def main() -> int:
    inst = instances()
    proc = camel(str(PROGRAM))
    if proc.returncode != 0:
        raise SystemExit(f"the run failed (exit {proc.returncode}): {(proc.stdout + proc.stderr)[-400:]}")
    lines = [l for l in proc.stdout.splitlines() if l.strip()]
    printed = {l.split(" ", 1)[0]: l for l in lines}

    # A constant slice: the declaration states the bounds and the length, the kernel prints the
    # positions those state. A backward slice's start is a position of the axis (n - 1 is the
    # end of its interval), a forward one runs to the end.
    for graph, count, upper in (("window", 3, "s0_0"), ("backward", 6, "s0_0 - 1")):
        if graph not in inst:
            wrong(graph, "no instance for the slice node")
            continue
        if graph not in printed:
            wrong(graph, f"nothing printed for '{graph}': {lines}")
            continue
        check_declared_bounds(graph, inst[graph], count, upper)
        p = params(inst[graph])
        start, step = p.get("start", ("", None))[1], p.get("step", ("", None))[1]
        if start is None or step is None:
            continue
        compare(graph, positions(graph, inst[graph], start, step, count), printed_values(graph, printed[graph], graph))

    # A bound only the run knows: the interval the declaration states, the bounds the call
    # passed, the positions those make, and the fault the run takes outside the interval.
    graph = "range_of"
    if graph not in inst:
        wrong(graph, "no instance for the slice node")
    else:
        n, step = check_open_bounds(graph, inst[graph])
        start, end = OPEN_CALL
        if not (0 <= start and end <= n):
            wrong(graph, f"the call {OPEN_CALL} leaves the interval [0, {n}] the declaration states")
        count = (end - start + step - 1) // step
        compare(
            graph,
            positions(graph, inst[graph], start, step, count),
            printed_values(graph, printed.get(graph, ""), graph),
        )
        if n:
            bound, _ = FAULT_CALL
            if 0 <= bound:
                wrong(graph, f"the fault's bound {bound} is inside [0, {n}]")
            fault = camel(str(PROGRAM), fault=True)
            text = fault.stdout + fault.stderr
            if fault.returncode == 0:
                wrong(graph, "a negative bound did not fault")
            for want in ("SliceOutOfRange", f"length {n}", str(bound), str(FAULT_CALL[1])):
                if want not in text:
                    wrong(graph, f"the fault does not name '{want}': {text.strip()[-200:]}")

    print(f"{len(inst)} slices, {BAD} wrong")
    print("FAIL" if BAD else "PASS")
    return 1 if BAD else 0


if __name__ == "__main__":
    sys.exit(main())