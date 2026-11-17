"""Is a rewritten body one graph? The author's criterion, made measurable.

The criterion is the region rule's acceptance item 4 (`fd75209cc`, and `812afad5c` says how to read
it): "swap `println(x)` and `fs::write(p, y)` and `std::gir` comes out the same", where "the same"
means **one canonical form** -- `std::jgir canonical=true`, the document `same_graph.py` compares
byte for byte, and never the text of a `std::gir` dump, which numbers its nodes in the order they
were written.

Four readings, and the later ones are what keep the earlier from being vacuous (`test/tools/
same_graph.py` says what a reading is; this case builds the pairs and states the expectation):

  `pure_swapped`     two writings that swap two **pure** statements. Nothing orders them (design
      principles 3.4: a pure, infallible computation is arranged freely), so the two canonical
      forms must be one -- and they are the reading that the canonical form is order-independent
      at all, which is what makes a byte comparison the yardstick and not a renumbering lottery.

  `two_writes_swapped`  the criterion's own pair: a `println` and a write to a file, two different
      resources outside the program, written in the two orders. One canonical form exactly when no
      edge joins the two writes (the ruling: they are inputs of the region's one `GATE` and "these
      writes get no edge between them"). On a build where the two writes still stand on the body's
      chain in written order, the reading is two forms, and the symmetric difference names the
      ordering edge that divides them. The case prints which of the two states this build is in and
      requires the reason either way.

  `write_deleted` / `write_is_a_read`  the same body with the write taken out, and with the write
      made a read of a variable. Both change what the body does to the outside world, so both must
      read two forms, with the difference naming the write that changed: a comparator that cannot
      say `false` is not a comparator.

The programs are built here from one template by swapping two adjacent lines, and the swap is
verified mechanically before anything is compared. They are not committed `.cml` files: a generated
pair cannot drift, and adding `.cml` files would move the front-end AST golden for a reason that has
nothing to do with the front end.

Run: python test/cases/trans/same_graph_writings.py --camel out/latest/bin/camel
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
TOOL = ROOT / "test" / "tools" / "same_graph.py"

PRINT = "println(x)"
WRITE = "let _ = 'written'->write_text<path>"
PURE_A = "let a = 2 * 3"
PURE_B = "let b = 4 + 5"


def body(statements, result="0"):
    """One function body with `statements` as its middle lines: the only thing a reading varies."""
    lines = [
        "import { write_text } from fs",
        "",
        "func main(): int {",
        "    let path = 'same_graph_out.txt'",
        "    let x = 1",
    ]
    lines += ["    " + statement for statement in statements]
    lines += [f"    return {result}", "}"]
    return "\n".join(lines) + "\n"


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def adjacent_swap(left, right):
    """The 1-based pair of lines `left` and `right` differ by, when they differ by a swap and nothing else."""
    if len(left) != len(right):
        return None
    differing = [index for index, (a, b) in enumerate(zip(left, right)) if a != b]
    if len(differing) != 2 or differing[1] != differing[0] + 1:
        return None
    first, second = differing
    if left[first] == right[second] and left[second] == right[first]:
        return first + 1, second + 1
    return None


def same_graph(left, right, swapped):
    """The tool's own report, verbatim: the verdict, the counts and, when it says no, the diff."""
    run = subprocess.run(
        [sys.executable, str(TOOL), "--camel", EXE, str(left), str(right), "--swapped", swapped],
        capture_output=True,
        text=True,
    )
    return run.returncode, (run.stdout or "") + (run.stderr or "")


def verdict_of(report):
    if " = true" in report:
        return True
    if " = false" in report:
        return False
    return None


def say(text=""):
    print(text)
    sys.stdout.flush()


def compare(scratch, name, left_text, right_text, failures):
    """Write the pair, prove it is one adjacent swap, and read the tool's report of it."""
    left = write(scratch / f"{name}_first.cml", left_text)
    right = write(scratch / f"{name}_second.cml", right_text)
    swap = adjacent_swap(
        left_text.splitlines(),
        right_text.splitlines(),
    )
    if swap is None:
        failures.append(f"{name}: the two writings differ by more than one swap of two lines")
        return None
    low, high = swap
    say(f"{name}: the two writings differ by exactly this swap of two adjacent lines "
        f"({low} <-> {high}):")
    say(f"  {low}: {left_text.splitlines()[low - 1].strip()}")
    say(f"  {high}: {left_text.splitlines()[high - 1].strip()}")
    code, report = same_graph(left, right, f"{low},{high}")
    say(report.rstrip())
    say()
    return code, verdict_of(report), report


def main():
    scratch = Path(os.environ.get("CAMEL_TEST_TMPDIR") or tempfile.mkdtemp(prefix="same-graph-"))
    scratch.mkdir(parents=True, exist_ok=True)
    say(f"programs: {scratch}")
    failures = []

    # -- 1. two pure statements, written in the two orders: one canonical form -----------------
    result = compare(
        scratch,
        "pure_swapped",
        body([PURE_A, PURE_B], "a + b"),
        body([PURE_B, PURE_A], "a + b"),
        failures,
    )
    if result is not None:
        code, decided, report = result
        if decided is not True:
            failures.append(
                "pure_swapped: two pure statements swapped read as two forms, and nothing orders "
                "them (design principles 3.4) -- the canonical form is not order-independent, so "
                "no byte comparison can be the yardstick"
            )
        if "nodes=" not in report or "edges=" not in report:
            failures.append("pure_swapped: a `true` without the counts it is a reading of")
        if code != 0:
            failures.append(f"pure_swapped: exit {code} for a `true` verdict")

    # -- 2. the criterion's own pair: println and a write to a file, swapped --------------------
    result = compare(
        scratch,
        "two_writes_swapped",
        body([PRINT, WRITE]),
        body([WRITE, PRINT]),
        failures,
    )
    if result is not None:
        code, decided, report = result
        if decided is None:
            failures.append("two_writes_swapped: the criterion's pair was not decided at all")
        elif decided:
            say("reading: the two writes are ONE canonical form on this build "
                "(the region rule of the ruling of 2026-11-17 is in force)")
            if code != 0:
                failures.append(f"two_writes_swapped: exit {code} for a `true` verdict")
        else:
            say("reading: the two writes are TWO canonical forms on this build "
                "(the region rule is not in force yet)")
            if code != 1:
                failures.append(f"two_writes_swapped: exit {code} for a `false` verdict")
            if "symmetric difference" not in report:
                failures.append("two_writes_swapped: a `false` without the symmetric difference")
            if "in:ordering" not in report:
                failures.append(
                    "two_writes_swapped: the difference is not an ordering edge, so it is not the "
                    "region rule that divides these two writings"
                )
        if "nodes=" not in report or "edges=" not in report:
            failures.append("two_writes_swapped: no counts")

    # -- 3. what must be different -------------------------------------------------------------
    base = body([PRINT, WRITE])
    for name, text, why in (
        ("write_deleted", body([PRINT]), "the write taken out of the body"),
        ("write_is_a_read", body([PRINT, "let _ = path"]), "the write made a read of a variable"),
    ):
        left = write(scratch / f"{name}_base.cml", base)
        right = write(scratch / f"{name}.cml", text)
        code, report = same_graph(left, right, "-")
        say(f"{name}: {why}")
        say(report.rstrip())
        say()
        decided = verdict_of(report)
        if decided is None:
            failures.append(f"{name}: not decided at all")
            continue
        if decided:
            failures.append(
                f"{name}: read as one form, and {why} changes what the body does -- a comparator "
                f"that says `true` here says `true` to everything"
            )
            continue
        if code != 1:
            failures.append(f"{name}: exit {code} for a `false` verdict")
        if "symmetric difference" not in report:
            failures.append(f"{name}: a `false` without the symmetric difference")
        if "write" not in report:
            failures.append(f"{name}: the difference does not name the write that changed")
        if not any(line.strip().startswith(("- ", "+ ")) for line in report.splitlines()):
            failures.append(f"{name}: the symmetric difference printed no node and no edge")

    say()
    if failures:
        for failure in failures:
            say(f"FAIL: {failure}")
        return 1
    say("two pure writings are one canonical form: PASS")
    say("a difference is reported by the nodes and the edges it is: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())