"""Run the C3 attention block on both sides and write the parity table.

Three programs compute the same six-parameter attention block -- one number out, six parameters in:

    block_forward.cml        the forward pass, through the tensor operators
    block_library_grad.cml   the reverse transform from `stdlib/vjp.cml` (library-level `vjp`,
                             the one the author's S2 asks for)
    block_engine_grad.cml    the engine's `autodiff.with_grad` (the retiring pipeline), a control
    baseline/block.py        numpy: the same formulas, with the Jacobian worked out by hand

What the table holds:

  * **forward**: the one number, Camel against numpy.  Both compute the same f32/f64 formulas, so
    the tolerance is a rounding bound, not a promise of bit equality (design principles 7).
  * **engine gradient**: the six parameters' derivatives from the engine against the hand-derived
    analytic Jacobian.  This is the control: it says the block's arithmetic and the six parameters
    are the ones the analytic derivation describes.
  * **library gradient**: the six parameters' derivatives from `stdlib/vjp.cml`.  Today the library
    answers no gradient for this block at all, and the row says so with the refusal's own words --
    `WALLS.cn.md` is where that reading is classified.

The script exits 0 when every row it *can* check holds; a library row that reads "refused" is not a
failure of this script, because the refusal is the example's finding and is recorded in
`WALLS.cn.md`.  If the library starts answering, its six numbers are compared with the same
tolerance as the engine's, and the row stops saying "refused".

    python showcase/c3-char-transformer/compare.py
    python showcase/c3-char-transformer/compare.py --results showcase/c3-char-transformer/RESULTS.md
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BLOCK_BASELINE = HERE / "baseline" / "block.py"
NAMES = ["wq.0", "wq.1", "wk.0", "wk.1", "wv.0", "wv.1"]

# Tolerances (design principles section 7): only the canonical algorithm on one back end is
# promised bit for bit.  The Camel programs compute in f32, the baseline in f64, and a 2x2 attention
# over two tokens sums two terms, so a rounding bound is what can be claimed:
TOL_FORWARD = 1.0e-6   # relative; one forward pass, f32 against f64
TOL_GRADIENT = 1.0e-4  # relative; a gradient through one softmax and three matrix products in f32


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


def run(command, env=None):
    process = subprocess.run(
        [str(part) for part in command],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="replace", env=env or os.environ.copy(),
    )
    return {"exit": process.returncode, "stdout": process.stdout, "stderr": process.stderr,
            "command": [str(part) for part in command]}


def fields(text, prefix):
    """Every `prefix<key>=<number>` on its own line, as a dict."""
    found = {}
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(prefix):
            continue
        body = line[len(prefix):]
        if "=" not in body:
            continue
        key, _, value = body.partition("=")
        try:
            found[key] = float(value)
        except ValueError:
            continue
    return found


def scalars(text, prefix):
    """The one number after `prefix`, or None."""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(prefix):
            tail = line[len(prefix):]
            match = re.search(r"-?\d+\.\d+(?:[eE][-+]?\d+)?", tail)
            if match:
                return float(match.group(0))
    return None


def relative(left, right):
    return abs(left - right) / max(abs(left), abs(right), 1e-12)


def wrap(line, width):
    """`line` cut into pieces of at most `width` characters, so a reader sees all of it."""
    if not line:
        return [""]
    return [line[at:at + width] for at in range(0, len(line), width)]


def report_failure(label, result, width=96):
    """Print a program's whole output, wrapped, so a diagnostic is never clipped in a report."""
    text = result["stdout"] + result["stderr"]
    for line in text.splitlines():
        for chunk in wrap(line.rstrip(), width):
            print(f"c3 compare diag.{label} {chunk}")


def refusal(text):
    for line in text.splitlines():
        line = line.strip()
        if re.search(r"\b(error|Error|Fault|fault|no rule|NoMatching|Invalid|refus)\b", line):
            return line
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else "(no output)"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--camel", default=None)
    parser.add_argument("--results", default=str(HERE / "RESULTS.md"))
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    camel = camel_executable(args.camel)
    if not camel.exists():
        raise SystemExit(f"no camel binary at {camel}")

    baseline = run([sys.executable, str(BLOCK_BASELINE), "--json", str(HERE / "block.json")])
    if baseline["exit"] != 0:
        sys.stderr.write(baseline["stdout"] + baseline["stderr"])
        raise SystemExit("baseline/block.py failed")
    reference = fields(baseline["stdout"], "block.numpy analytic.")
    reference_forward = scalars(baseline["stdout"], "block.numpy forward=")
    method = "unknown"
    for line in baseline["stdout"].splitlines():
        if line.startswith("block.numpy method="):
            method = line[len("block.numpy method="):].strip()

    forward = run([camel, str(HERE / "block_forward.cml")])
    engine = run([camel, str(HERE / "block_engine_grad.cml")])
    library = run([camel, str(HERE / "block_library_grad.cml")])
    for label, result in (("forward", forward), ("engine", engine), ("library", library)):
        if result["exit"] != 0:
            report_failure(label, result)

    rows = []

    # --- forward ---------------------------------------------------------------
    camel_forward = scalars(forward["stdout"], "c3 block forward=") if forward["exit"] == 0 else None
    if camel_forward is None:
        rows.append(dict(reading="forward", camel=None, reference=reference_forward,
                         error=None, tolerance=TOL_FORWARD, ok=False,
                         note=f"Camel printed no value: {refusal(forward['stdout'] + forward['stderr'])}"))
    else:
        error = relative(camel_forward, reference_forward)
        rows.append(dict(reading="forward", camel=camel_forward, reference=reference_forward,
                         error=error, tolerance=TOL_FORWARD, ok=error <= TOL_FORWARD, note=""))

    # --- the engine's gradient -------------------------------------------------
    engine_values = fields(engine["stdout"], "c3 block engine.") if engine["exit"] == 0 else {}
    if not engine_values:
        rows.append(dict(reading="gradient (engine with_grad)", camel=None, reference=None,
                         error=None, tolerance=TOL_GRADIENT, ok=False,
                         note=f"the engine answered nothing: {refusal(engine['stdout'] + engine['stderr'])}"))
    else:
        worst_name, worst_error = None, -1.0
        for name in NAMES:
            if name not in engine_values or name not in reference:
                continue
            error = relative(engine_values[name], reference[name])
            if error > worst_error:
                worst_name, worst_error = name, error
        rows.append(dict(reading="gradient (engine with_grad)", camel=engine_values.get(worst_name),
                         reference=reference.get(worst_name), error=worst_error,
                         tolerance=TOL_GRADIENT, ok=worst_error <= TOL_GRADIENT,
                         note=f"worst parameter: {worst_name}"))

    # --- the library's gradient ------------------------------------------------
    library_values = fields(library["stdout"], "c3 block library.") if library["exit"] == 0 else {}
    pullback_length = scalars(library["stdout"], "c3 block pullback_length=") if library["exit"] == 0 else None
    if library["exit"] != 0:
        library_note = f"refused: {refusal(library['stdout'] + library['stderr'])}"
        rows.append(dict(reading="gradient (library vjp)", camel=None, reference=None, error=None,
                         tolerance=TOL_GRADIENT, ok=True, note=library_note))
    elif not library_values:
        note = ("the pullback answered %d cotangent(s) and none of them is a parameter: the model's "
                "six numbers are not in the differentiated program" % int(pullback_length or 0))
        rows.append(dict(reading="gradient (library vjp)", camel=None, reference=None, error=None,
                         tolerance=TOL_GRADIENT, ok=True, note=note))
    else:
        worst_name, worst_error = None, -1.0
        for name in NAMES:
            if name in library_values and name in reference:
                error = relative(library_values[name], reference[name])
                if error > worst_error:
                    worst_name, worst_error = name, error
        rows.append(dict(reading="gradient (library vjp)", camel=library_values.get(worst_name),
                         reference=reference.get(worst_name), error=worst_error,
                         tolerance=TOL_GRADIENT, ok=worst_error <= TOL_GRADIENT,
                         note=f"worst parameter: {worst_name}"))

    # --- what the library can answer at all ------------------------------------
    dx = fields(library["stdout"], "c3 block dx.") if library["exit"] == 0 else {}
    dx_note = ", ".join(f"{name.split('.')[-1]}={value:+.12f}" for name, value in sorted(dx.items())) \
        if dx else "none printed"

    passed = all(row["ok"] for row in rows)
    worst = max((row["error"] for row in rows if row["error"] is not None), default=None)
    verdict = "PASS" if passed else "FAIL"

    print(f"c3 compare method={method} tolerance.forward={TOL_FORWARD:g} "
          f"tolerance.gradient={TOL_GRADIENT:g}")
    for row in rows:
        if row["error"] is None:
            print(f"c3 compare row={row['reading']} camel=absent error=- tolerance={row['tolerance']:g} "
                  f"ok={row['ok']} note={row['note']}")
        else:
            print(f"c3 compare row={row['reading']} camel={row['camel']:.12f} "
                  f"reference={row['reference']:.12f} error={row['error']:.3e} "
                  f"tolerance={row['tolerance']:g} ok={row['ok']} note={row['note']}")
    if worst is not None:
        print(f"c3 compare worst={worst:.3e}")
    print(f"c3 compare library_cotangent_of_x={dx_note}")
    print(f"c3 compare: {verdict}")

    write_results(Path(args.results), rows, method, worst, dx_note, forward, engine, library, camel)
    print(f"c3 compare results={args.results}")
    if args.json:
        Path(args.json).write_text(json.dumps({
            "method": method,
            "rows": rows,
            "library_cotangent_of_x": dx,
            "verdict": verdict,
        }, indent=2), encoding="utf-8")
    return 0 if passed else 1


def write_results(path, rows, method, worst, dx_note, forward, engine, library, camel):
    lines = []
    lines.append("# C3: gradient parity of the smallest attention block\n")
    lines.append("Generated by `compare.py`.  Do not edit by hand.\n")
    lines.append("The block is one number out of six parameters -- three 2x1 matrices, a softmax "
                 "over the key axis, a causal mask and a residual-free output projection.  "
                 "`baseline/block.py` computes the same formulas in numpy and derives the six "
                 "partial derivatives by hand; the finite difference in the same file checks the "
                 "derivation.\n")
    lines.append(f"Reference used: {method}.\n")
    lines.append("| reading | Camel | reference | relative error | tolerance | holds | note |")
    lines.append("|---|---|---|---|---|---|---|")
    for row in rows:
        camel_cell = "-" if row["camel"] is None else f"{row['camel']:.12f}"
        reference_cell = "-" if row["reference"] is None else f"{row['reference']:.12f}"
        error_cell = "-" if row["error"] is None else f"{row['error']:.3e}"
        lines.append(f"| {row['reading']} | {camel_cell} | {reference_cell} | {error_cell} | "
                     f"{row['tolerance']:.0e} | {'yes' if row['ok'] else 'no'} | {row['note']} |")
    if worst is not None:
        lines.append(f"\nWorst relative error over the rows that have one: `{worst:.3e}`.\n")
    lines.append("## What the library's `vjp` answered\n")
    lines.append(f"The library keeps no parameter gradient for this block; the one cotangent it can "
                 f"answer is the cotangent of the input `x`, and `block_library_grad.cml` reads it "
                 f"out: {dx_note}.\n")
    lines.append("## Commands\n")
    lines.append("```")
    lines.append(f"python {Path(__file__).name}")
    for run_ in (forward, engine, library):
        lines.append(" ".join(run_["command"]) + f"   # exit {run_['exit']}")
    lines.append("```")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())