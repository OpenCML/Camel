"""Does a draft's node have an `id`? The acceptance of ② of the spelling handover.

`id` is the node's *canonical number* -- its position in the order `nodes` gives, which the graph's
structure decides (design principles 9.5). A draft is not a graph: it is the copy a transaction is
editing, its nodes' places are the order they were made in, and the commit replaces that order with
the canonical one. So a node of a draft has no canonical number, and the handover's ② left the
reading answering the draft's position instead: a number of the right shape and the wrong value,
which is the one thing a reading of the canonical order exists to replace
(`internal/reports/2026-11-18-commit-checks-and-spelling-spelling.cn.md`, item 2).

The lane answered it by **refusing the reading**: a node of a draft raises the runtime fault
`RuntimeDiag::InvalidArgument` of `graph.id`, with a message that says why and what to read instead
(`modules/graph/read.cpp`, `nodeCanonicalNumber`). This case is that answer made measurable, and it
is two runs of **one program with one line moved**, so that the reading cannot be satisfied by a
program that never commits or by one that never opens a transaction:

  1. `before`: open a transaction, add a constant to the draft, ask `id` of the node the draft just
     made. The run ends with the fault, and the fault's text is printed verbatim -- the message is
     the answer, so a case that only counted the exit code would not be reading it.

  2. `after`: the same body with the commit in it, asking `id` of the published graph's nodes.
     The numbers are the positions in `nodes`, read back as a list and compared with `0..n-1`: the
     definition of the reading, not a sample of it.

The programs are generated into a scratch directory rather than committed, for the reason
`test/cases/trans/canon_unreachable.py` gives: a new `.cml` under `test/cases/` would move the
front-end AST golden for a reason that has nothing to do with the front end.

Run: python test/cases/modules/graph_draft_id.py --camel out/latest/bin/camel
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)

# The body both runs share: a transaction, and a constant written into the draft. What the two
# writings differ in is one line -- where the reading of `id` stands relative to the commit.
BEFORE = """\
import { Graph, Node, add_constant, graph_of, id, open } from graph

func main() => int sync {
    let p = main
    let t = open(p)
    let added = add_constant(t, graph_of(p), 7)
    println(id(added))
    return 0
}
"""

AFTER = """\
import { Graph, Node, add_constant, commit, graph_of, id, nodes, open } from graph
import { is_function } from types

func report::<F>(published: F) => int when is_function::<F>() sync {
    let all = nodes(graph_of(published))
    println(len(all))
    println(all->map<func (m: Node) => int id(m)>)
    return 0
}

func main() => int sync {
    let p = main
    let t = open(p)
    let added = add_constant(t, graph_of(p), 7)
    return match commit(t) {
        case error e => sync { println('refused ', e.kind); return 1 }
        case q => report(q)
    }
}
"""


def say(*parts):
    print(*parts)
    sys.stdout.flush()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def run(path):
    """`(exit, stdout, stderr)` of one run of the compiler under test."""
    done = subprocess.run(
        [EXE, str(path), "std::nvm"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=dict(os.environ),
    )
    return done.returncode, done.stdout.decode("utf-8", "replace"), done.stderr.decode("utf-8", "replace")


def main():
    failures = []
    scratch = Path(tempfile.mkdtemp(prefix="camel-draft-id-"))
    try:
        before = write(scratch / "before.cml", BEFORE)
        after = write(scratch / "after.cml", AFTER)

        # 1. Before the commit the reading is refused, and the message is the answer.
        code, out, err = run(before)
        report = (out + err).strip()
        say("--- the draft's node, before the commit ---")
        for line in report.splitlines():
            say(f"  {line}")
        say(f"  exit {code}")
        if code == 0:
            failures.append("reading `id` of a draft's node was answered instead of refused")
        if "graph.id" not in report:
            failures.append("the refusal does not name the reading (`graph.id`)")
        if "a draft has no canonical number" not in report:
            failures.append("the refusal does not say why the number is not there")
        if not re.search(r"creation_id", report):
            failures.append("the refusal does not say what to read for the draft's own order")
        # The fault is a diagnostic and not a value: it names the position in the program, which a
        # number printed in place of one would not.
        if "before.cml:7:13" not in report:
            failures.append("the refusal does not locate the reading at the call of `id`")
        say()

        # 2. After the commit the same reading answers, and the numbers are the positions.
        code, out, err = run(after)
        say("--- the published graph, after the commit ---")
        for line in (out + err).strip().splitlines():
            say(f"  {line}")
        say(f"  exit {code}")
        if code != 0:
            failures.append(f"reading `id` after the commit failed (exit {code})")
            say()
            for failure in failures:
                say(f"FAIL: {failure}")
            return 1
        lines = [line for line in out.strip().splitlines() if line.strip()]
        if len(lines) < 2:
            failures.append("the after-the-commit run printed no node count and no id list")
        else:
            try:
                count = int(lines[0].strip())
                ids = [int(word) for word in lines[1].strip().strip("[]").split(",")]
            except ValueError:
                failures.append(f"the after-the-commit run printed no two lists: {lines[:2]}")
                count, ids = -1, []
            if count != len(ids):
                failures.append(f"the graph has {count} nodes and {len(ids)} ids")
            # The definition: a node's id is its position in the order `nodes` gives.
            if ids != list(range(len(ids))):
                failures.append(f"the ids are not the positions in `nodes`: {ids}")
            if len(ids) < 2:
                failures.append("the published graph has fewer than two nodes, so nothing is read")
            say()
            say(f"ids are the positions in `nodes` over {len(ids)} nodes: {ids}")

        say()
        if failures:
            for failure in failures:
                say(f"FAIL: {failure}")
            return 1
        say("reading `id` of a draft's node is refused, and the refusal says why: PASS")
        say("reading `id` after the commit is the position in `nodes`: PASS")
        return 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())