"""Does a transform read the **writing**? The ECOOP measurement of plan 丙 (2026-11-18), item (a).

The transform is `greedy::fuse`, a pass written in Camel (`greedy_fuse.cml.in`, copied into the
scratch directory as `greedy.cml` so the module is found beside the program): it takes the graph's
multiplications **in the order `nodes(g)` gives them** and pairs them two at a time, fusing each pair
into one node. It asks none of the writing-visible queries plan 丙.2 singles out -- no `id`, no
`name`, no `origin`, no `show` -- so the one thing besides the graph it can depend on is the order of
`nodes`.

The criterion (plan 丙.1): `nodes` must return the **canonical** order, so that the two writings of
one program fuse the same two multiplications and come out as one graph. The program is
`x*2 + y*3 + x*y`, and its two writings are the two plan 丙.1 names: **two adjacent statements
swapped**, and **every local renamed**. Neither changes what the program means, so the unfused pair
is one canonical form already (the reading that says the yardstick is order-independent -- measured
in `test/cases/trans/same_graph_writings.py` as well); whatever the fused pair then does is the
transform's doing and not the writing's.

Six readings, and the later ones are what keep the earlier from being vacuous:

  `raw_one_form`        the two writings, before any transform: ONE canonical form. Without this
                        the rest would be measuring the front end and not the transform.
  `changes_the_graph`   base fused and base unfused: TWO canonical forms, and the difference names
                        a multiplication. A transform that silently fused nothing would make every
                        reading below a `true` for the wrong reason -- this is the reading that
                        cannot be reached by doing nothing.
  `renamed`             base and renamed, both fused: ONE canonical form. A name is for a reader.
  `swapped`             base and swapped, both fused: **the criterion.** ONE canonical form exactly
                        when `nodes` gives the canonical order. Before plan 丙.1 lands it reads two,
                        and then the symmetric difference must name the two multiplications that
                        were paired differently -- the difference has to be the choice of pair, and
                        not "everything differs".
  `swapped_and_renamed` both changes at once: the same answer `swapped` gives.
  `different_constant`  `y * 4` for `y * 3`, both fused: TWO canonical forms. A comparator that
                        cannot say `false` about the transform's own output is not a comparator.

**Every reading prints both sides' canonical form: the byte count and the sha256.** The verdict is
the canonical form itself, byte for byte, the one `std::jgir canonical=true` writes and
`test/tools/same_graph.py` compares (author's ruling `812afad5c`: "same_graph compares canonical
forms, the one the block naming uses"). The tool also supplies the explanation -- the nodes and edges
one side has and the other does not, ids and spans erased -- and the readings that require a
*difference* require it to be named, so that "they differ" is never an absence of a report.

`--require-one-form` takes the strict reading: the `swapped` answer must be one canonical form, and
anything else is a failure. That is the line the criterion becomes once plan 丙.1 (canonical `nodes`)
is merged; the plan runs the measurement without it, because a reading taken before the change lands
is a reading and not a red to silence.

The programs are generated here from one template (a swap of two adjacent lines, a rename of the
four locals) and the generation is verified mechanically before anything is compiled. They are not
committed `.cml` files, for the reason `test/cases/trans/same_graph_writings.py` gives: every `.cml`
file is covered by the front-end AST golden, and a fixture must not move that golden for a reason
that has nothing to do with the front end.

Run: python test/cases/opt/writings/greedy_fusion_writings.py --camel out/latest/bin/camel
     python test/cases/opt/writings/greedy_fusion_writings.py --camel out/latest/bin/camel --require-one-form
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def repository_root(start):
    """The first ancestor holding `stdlib/std.cml`: neither cwd nor `HERE` is the root."""
    for directory in [start, *start.parents]:
        if (directory / "stdlib" / "std.cml").is_file():
            return directory
    raise SystemExit(f"no stdlib/std.cml above {start}")


ROOT = repository_root(HERE)
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
STRICT = "--require-one-form" in ARGV
TOOL = ROOT / "test" / "tools" / "same_graph.py"
PASS_SOURCE = HERE / "greedy_fuse.cml.in"

# The program of plan 丙's item (a): one function that computes `x*2 + y*3 + x*y`, and an entry that
# calls it. The multiplications are written as three statements so that swapping two of them swaps
# two lines and nothing else, and so that the order they are built in is the order they are read.
BASE = """func combine(x: int, y: int): int {
    let a = x * 2
    let b = y * 3
    let c = x * y
    return a + b + c
}

func main(): int {
    return combine(3, 5)
}
"""

# The two writings of 丙.1, and the checks that say they are exactly that.
SWAPPED_LINES = ("    let a = x * 2\n    let b = y * 3\n", "    let b = y * 3\n    let a = x * 2\n")
RENAMES = {"x": "left", "y": "right", "a": "first", "b": "second", "c": "third"}
DIFFERENT = ("    let b = y * 3\n", "    let b = y * 4\n")

# The writing-visible queries of plan 丙.2. A `**name(**` is not `name(`: the word boundary keeps
# `op_name(` out of the list.
WRITING_QUERIES = [r"(?<![\w])id\(", r"(?<![\w])name\(", r"(?<![\w])origin\(", r"(?<![\w])show\("]


def say(text=""):
    print(text)
    sys.stdout.flush()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def adjacent_swap(left, right):
    """The 1-based pair of lines `left` and `right` differ by, when a swap and nothing else."""
    if len(left) != len(right):
        return None
    differing = [index for index, (a, b) in enumerate(zip(left, right)) if a != b]
    if len(differing) != 2 or differing[1] != differing[0] + 1:
        return None
    first, second = differing
    if left[first] == right[second] and left[second] == right[first]:
        return first + 1, second + 1
    return None


def renamed(text):
    return re.sub(r"(?<![\w])(x|y|a|b|c)(?![\w])", lambda m: RENAMES[m.group(1)], text)


def run_camel(program, fused):
    """The canonical document of one program, as bytes, with or without the transform in front."""
    words = [EXE, str(program)]
    if fused:
        words.append("greedy::fuse")
    words += ["std::jgir", "canonical=true"]
    run = subprocess.run(words, capture_output=True, env=CHILD_ENV)
    text = (run.stdout or b"")
    if run.returncode != 0 or not text.strip():
        first = (text.decode("utf-8", "replace").strip().splitlines() or [""])[0]
        error = (run.stderr or b"").decode("utf-8", "replace").strip().splitlines()
        raise SystemExit(
            f"camel {' '.join(words[1:])} = unreadable: exit {run.returncode}; "
            f"stdout {first!r}; stderr {(error or [''])[0]!r}"
        )
    return text


def same_graph(left, right):
    """The tool's own report, verbatim: the verdict, the counts and, when it says no, the diff."""
    run = subprocess.run(
        [sys.executable, str(TOOL), str(left), str(right)],
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    return run.returncode, (run.stdout or "") + (run.stderr or "")


def verdict_of(report):
    if " = true" in report:
        return True
    if " = false" in report:
        return False
    return None


def reading(scratch, name, title, left, right, failures):
    """One reading: both canonical forms by their hashes, then the tool's verdict and explanation."""
    say(f"reading: {name} -- {title}")
    for label, key in (("left", left), ("right", right)):
        size, digest = FORMS[key]
        say(f"  canonical[{label}] {Path(key).name}: {size} bytes, sha256={digest}")
    code, report = same_graph(left, right)
    say(report.rstrip())
    say()
    return code, verdict_of(report), report


def keep(scratch, writing, fused):
    """Dump one canonical form to a file, and record its hash. The file is what the tool compares."""
    text = run_camel(scratch / f"{writing}.cml", fused)
    path = scratch / f"{writing}{'__fused' if fused else '__raw'}.json"
    with open(path, "wb") as handle:
        handle.write(text)
    FORMS[str(path)] = (len(text), hashlib.sha256(text).hexdigest())
    return str(path)


def main():
    global CHILD_ENV, FORMS
    scratch = Path(os.environ.get("CAMEL_TEST_TMPDIR") or tempfile.mkdtemp(prefix="greedy-fusion-"))
    scratch.mkdir(parents=True, exist_ok=True)
    CHILD_ENV = dict(os.environ)
    CHILD_ENV.setdefault("CAMEL_HOME", str(Path(EXE).resolve().parent.parent))
    FORMS = {}
    failures = []

    say(f"programs: {scratch}")
    say(f"transform: {PASS_SOURCE.relative_to(ROOT).as_posix()} -> {scratch / 'greedy.cml'}")

    # -- the transform asks no writing-visible query: read from its own source ------------------
    source = PASS_SOURCE.read_text(encoding="utf-8")
    code_lines = [line for line in source.splitlines() if not line.strip().startswith("//")]
    asked = [pattern for pattern in WRITING_QUERIES if any(re.search(pattern, line) for line in code_lines)]
    if asked:
        failures.append(
            f"the transform asks a writing-visible query ({', '.join(asked)}), so what it reads is "
            f"not only the graph and the order of `nodes`"
        )
    else:
        say("the transform asks no writing-visible query (id/name/origin/show): PASS")
    shutil.copyfile(PASS_SOURCE, scratch / "greedy.cml")

    # -- the writings, and the mechanical proof that they are the writings they claim to be ------
    writings = {"base": BASE, "swapped": BASE.replace(*SWAPPED_LINES)}
    swap = adjacent_swap(BASE.splitlines(), writings["swapped"].splitlines())
    if swap != (2, 3):
        failures.append(
            f"the swapped writing is not exactly the swap of lines 2 and 3 of the base "
            f"(read {swap}); the pair the transform is measured on would not be the pair 丙.1 names"
        )
    else:
        say("the two writings differ by exactly this swap of two adjacent lines (2 <-> 3):")
        say(f"  2: {BASE.splitlines()[1].strip()}")
        say(f"  3: {BASE.splitlines()[2].strip()}")
    writings["renamed"] = renamed(BASE)
    writings["swapped_renamed"] = renamed(writings["swapped"])
    writings["different"] = BASE.replace(*DIFFERENT)
    if writings["renamed"] == BASE or writings["swapped_renamed"] == writings["swapped"]:
        failures.append("the renamed writings are the originals; the rename substituted nothing")
    for name, text in writings.items():
        write(scratch / f"{name}.cml", text)
    say(f"writings: {', '.join(sorted(writings))}")
    say()

    forms = {}
    for name in writings:
        forms[(name, False)] = keep(scratch, name, False)
        forms[(name, True)] = keep(scratch, name, True)

    # -- 1. before any transform, the two writings are one canonical form ----------------------
    code, decided, report = reading(
        scratch,
        "raw_one_form",
        "the two writings, before the transform",
        forms[("base", False)],
        forms[("swapped", False)],
        failures,
    )
    if decided is not True:
        failures.append(
            "raw_one_form: swapping two adjacent `let` lines moved the canonical form before any "
            "transform ran, so this pair is not a pair of writings and nothing below is measuring "
            "the transform"
        )
    elif code != 0:
        failures.append(f"raw_one_form: exit {code} for a `true` verdict")

    # -- 2. the transform is not a no-op: fusing the same program changes its graph -------------
    code, decided, report = reading(
        scratch,
        "changes_the_graph",
        "the same program, fused and unfused -- a transform that fused nothing reads as a no-op",
        forms[("base", False)],
        forms[("base", True)],
        failures,
    )
    if decided is not False:
        failures.append(
            "changes_the_graph: the transform left the canonical form alone, so every `same graph` "
            "below would be a `true` about nothing"
        )
    elif ":op/mul" not in report:
        failures.append(
            "changes_the_graph: the two forms differ, but the difference names no multiplication, "
            "so what the transform changed is not the pair it is supposed to choose"
        )
    elif code != 1:
        failures.append(f"changes_the_graph: exit {code} for a `false` verdict")

    # -- 3. a name is for a reader: the renamed writing fuses to the same graph -----------------
    code, decided, report = reading(
        scratch,
        "renamed",
        "the same program with every local renamed, both fused",
        forms[("base", True)],
        forms[("renamed", True)],
        failures,
    )
    if decided is not True:
        failures.append(
            "renamed: renaming a local changed the graph the transform produced, and a name is for "
            "a reader (design principles 9)"
        )
    elif code != 0:
        failures.append(f"renamed: exit {code} for a `true` verdict")

    # -- 4. the criterion: the swapped writing fuses to the same graph --------------------------
    state = None
    answers = {}
    for name, other, title in (
        ("swapped", "swapped", "two adjacent statements swapped, both fused -- THE CRITERION"),
        ("swapped_and_renamed", "swapped_renamed", "both changes at once, both fused"),
    ):
        code, decided, report = reading(
            scratch, name, title, forms[("base", True)], forms[(other, True)], failures
        )
        answers[name] = (code, decided, report)
        if decided is None:
            failures.append(f"{name}: not decided at all")
            continue
        if decided:
            if code != 0:
                failures.append(f"{name}: exit {code} for a `true` verdict")
            if state is None:
                state = True
            elif state is not True:
                failures.append(f"{name}: one canonical form, and the other pair read two")
            say(f"  => the two writings of one program are ONE canonical form ({name})")
        else:
            if code != 1:
                failures.append(f"{name}: exit {code} for a `false` verdict")
            if "symmetric difference" not in report:
                failures.append(f"{name}: a `false` without the symmetric difference")
            if ":op/mul" not in report:
                failures.append(
                    f"{name}: the two forms differ, and the difference names no multiplication, so "
                    f"it is not the choice of pair that parts them"
                )
            # The argument this branch rests on, and it is not the size of the difference: the two
            # **unfused** documents are byte-identical (the `raw_one_form` reading above), so the
            # transform was given the same canonical graph twice. Whatever it produced next, it
            # did not read it off either document.
            say(f"  the two documents the transform was given are the same bytes "
                f"(sha256={FORMS[forms[('base', False)]][1]} on both sides), so the difference is "
                f"not in the program the transform read")
            if state is None:
                state = False
            elif state is not False:
                failures.append(f"{name}: two canonical forms, and the other pair read one")
            say(f"  => the two writings are TWO canonical forms ({name}): what parts them is the pair "
                f"the transform chose")
            if name == "swapped" and STRICT:
                failures.append(
                    f"{name}: --require-one-form was asked and the two writings of one program are "
                    f"two canonical forms: `nodes` does not give the canonical order yet (plan 丙.1)"
                )
        say()

    # -- 5. the comparator can say `false` about the transform's own output --------------------
    code, decided, report = reading(
        scratch,
        "different_constant",
        "`y * 4` for `y * 3`, both fused -- the comparator must be able to say no",
        forms[("base", True)],
        forms[("different", True)],
        failures,
    )
    if decided is not False:
        failures.append(
            "different_constant: a different constant fused to the same canonical form, so the "
            "`same graph` answers above are answers about nothing"
        )
    elif code != 1:
        failures.append(f"different_constant: exit {code} for a `false` verdict")

    say()
    if state is True:
        say("the two writings of one program are ONE canonical form: PASS")
    elif state is False:
        say(
            "the two writings of one program are TWO canonical forms: the transform reads the "
            "writing order, so `nodes` is not the canonical order yet (plan 丙.1 not in force)"
        )
    if failures:
        for failure in failures:
            say(f"FAIL: {failure}")
        return 1
    say("the greedy fusion is not a no-op, a rename fuses to the same graph, and a different "
        "constant does not: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())