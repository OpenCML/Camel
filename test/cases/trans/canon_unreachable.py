"""Does a graph with an unreachable node have a canonical form? The acceptance of ① of the
spelling handover.

The question the handover left open (`docs/handover/reports/2026-11-18-commit-checks-and-spelling-
spelling.cn.md`, item 1) is not "add a rule for the nodes nobody reaches". It is **where an
unreachable node stands in the canonical order**, and that is a question about what the canonical
order *is*: `nodes` is the order the graph's own structure decides, and a node no root reaches has
no such place. The lane answered it with the conservative reading -- keep the unreachable nodes in
`nodes`, last, and order the tail by the same tie rule as everything else
(`include/camel/runtime/canonical.h`) -- and this case is what makes that answer measurable
instead of a claim.

Three readings, and the case is written so that a `true` cannot be vacuous:

  1. The program **has** an unreachable node. The one this repository produces is a region port
     nothing in its region reads: `loop total = 0, step = 0 while step < scale do (total + twice,
     step + 1)` reads `step` in the condition and never the other accumulator, so the condition
     region's port for `total` has no reader. That is read off the canonical document itself -- a
     node named in a region's `ports` that no node lists as an input -- so the reading is about the
     graph the dump was made from and not about a comment. Without this the rest of the case would
     pass on a program with no tail at all.

  2. Two writings of it are **one canonical form**: two statements swapped and every local
     renamed, `std::jgir canonical=true` on each, compared byte for byte. Both sha256 are printed,
     so the reading is not a claim. When the two differ the case runs `test/tools/same_graph.py`
     over the two documents and prints the nodes and edges that one side has and the other does
     not -- "the unreachable node moved" is a sentence a reader has to be able to check.

  3. The comparator can say **false** on this same program: one constant changed (`* 3` becomes
     `* 4`) is two graphs, and the case requires that reading and the difference that names it. A
     comparison that answers `true` to everything is not a comparison, and without this reading the
     `true` above would not tell a reader anything.

The programs are generated into a scratch directory rather than committed: a generated pair cannot
drift, and a new `.cml` under `test/cases/` would move the front-end AST golden for a reason that
has nothing to do with the front end (`test/cases/trans/same_graph_writings.py` says the same).

Run: python test/cases/trans/canon_unreachable.py --camel out/latest/bin/camel
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
TOOL = ROOT / "test" / "tools" / "same_graph.py"

# One program, three writings. `target` folds over a range with two accumulators, and the
# condition reads only `step`: `total`'s condition port is the node nothing reaches. The two
# statements the writings swap are the two multiplications, which nothing orders -- one feeds the
# loop's condition, the other its body -- so a canonical form that kept them in the order they were
# written would be a form that sees the writing.
WRITING_A = """\
func target(n: int): int {
    let first = n * 3
    let second = n * 2
    let pair = loop total = 0, step = 0 while step < first do (total + second, step + 1)
    return pair.0
}

func main(): int {
    println(target(5))
    return 0
}
"""

WRITING_B = """\
func target(n: int): int {
    let twice = n * 2
    let scale = n * 3
    let walked = loop acc = 0, at = 0 while at < scale do (acc + twice, at + 1)
    return walked.0
}

func main(): int {
    println(target(5))
    return 0
}
"""

# The counterfactual: writing B with one constant changed. Same shape, same unreachable node, and
# a different graph -- the answer the comparison has to be able to give.
WRITING_C = WRITING_B.replace("n * 3", "n * 4")


def say(*parts):
    print(*parts)
    sys.stdout.flush()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def run(args):
    """`(exit, stdout bytes, stderr text)` of one run of the compiler under test."""
    env = dict(os.environ)
    env.pop("CAMEL_LOG_DEBUG", None)
    done = subprocess.run(
        [EXE, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    return done.returncode, done.stdout, done.stderr.decode("utf-8", "replace")


def unreachable_nodes(document):
    """The nodes a region names as its ports that no node of that graph reads.

    That is the shape an unreachable node has in this graph model: a region port nothing in the
    region reads. Read off the dumped document, so the reading is about the graph and not about
    what this file believes the graph should look like.
    """
    found = []
    for graph in document.get("graphs", []):
        users = {}
        for node in graph.get("nodes", []):
            for port in node.get("inputs", []):
                users.setdefault(port["source"], []).append(node["id"])
        for region in graph.get("regions", []) or []:
            for port in region.get("ports", []) or []:
                if port in users:
                    continue
                found.append((graph.get("id"), region.get("kind"), port))
    return found


def main():
    failures = []
    scratch = Path(tempfile.mkdtemp(prefix="camel-canon-unreachable-"))
    try:
        paths = {}
        for name, text in (("a", WRITING_A), ("b", WRITING_B), ("c", WRITING_C)):
            paths[name] = write(scratch / f"{name}.cml", text)

        say("the program has to have an unreachable node, or the rest of this reads nothing")

        # 1. Every writing runs, and the two writings of one program print one value.
        results = {}
        for name in ("a", "b", "c"):
            code, out, err = run([str(paths[name]), "std::nvm"])
            if code != 0:
                say(f"FAIL: writing {name} did not run (exit {code}): {err.strip()[:400]}")
                return 1
            results[name] = out
        say(f"  run: a={results['a'].decode().strip()} b={results['b'].decode().strip()} "
            f"c={results['c'].decode().strip()}")
        if results["a"] != results["b"]:
            failures.append("two writings of one program printed two values")
        if results["a"] == results["c"]:
            failures.append("the counterfactual printed the value the pair does, so it changes nothing")

        # 2. The canonical documents, and the unreachable node read off each of them.
        documents, hashes = {}, {}
        for name in ("a", "b", "c"):
            code, out, err = run([str(paths[name]), "std::jgir", "canonical=true"])
            if code != 0:
                say(f"FAIL: the canonical dump of writing {name} failed (exit {code}): "
                    f"{err.strip()[:400]}")
                return 1
            if not out.strip():
                say(f"FAIL: the canonical dump of writing {name} was empty")
                return 1
            try:
                documents[name] = json.loads(out.decode("utf-8"))
            except ValueError as bad:
                say(f"FAIL: the canonical dump of writing {name} is not one JSON document: {bad}")
                return 1
            hashes[name] = hashlib.sha256(out).hexdigest()
            write(scratch / f"{name}.json", out.decode("utf-8"))

        for name in ("a", "b", "c"):
            holes = unreachable_nodes(documents[name])
            say(f"  unreachable nodes in writing {name}: {len(holes)}"
                + "".join(f" [{graph}/{kind}/{port}]" for graph, kind, port in holes))
            if not holes:
                failures.append(
                    f"writing {name} has no unreachable node: the reading is about nothing")

        # A document has to be one document: dumping the same file twice is the same bytes.
        code, again, err = run([str(paths["a"]), "std::jgir", "canonical=true"])
        if code != 0 or hashlib.sha256(again).hexdigest() != hashes["a"]:
            failures.append("two dumps of one file are two documents")

        # 3. The verdicts. `same_graph` is the canonical form, byte for byte.
        same_by_nodes = hashes["a"] == hashes["b"]
        same_by_counterfactual = hashes["a"] == hashes["c"]
        say()
        say(f"canonical by nodes     a={hashes['a']} b={hashes['b']} same_graph={same_by_nodes}")
        say(f"counterfactual         a={hashes['a']} c={hashes['c']} "
            f"same_graph={same_by_counterfactual}")

        if not same_by_nodes:
            failures.append("two writings of one graph are two canonical forms")
        if same_by_counterfactual:
            failures.append("the counterfactual is the same graph, so the comparison is vacuous")

        # 4. When an answer is `false`, say what differs -- the nodes and the edges, not an offset.
        for label, other, want_difference in (("b", "b", not same_by_nodes),
                                              ("c", "c", not same_by_counterfactual)):
            if not want_difference:
                continue
            code, out, err = run_tool([str(scratch / "a.json"), str(scratch / f"{other}.json")])
            report = out.decode("utf-8", "replace") + err
            say()
            say(f"difference a vs {label}:")
            for line in report.splitlines()[:40]:
                say(f"  {line}")
            if code != 1:
                failures.append(f"a vs {label}: same_graph.py exited {code} for a `false` verdict")
            if not any(line.strip().startswith(("- ", "+ ")) for line in report.splitlines()):
                failures.append(f"a vs {label}: the difference names no node and no edge")

        say()
        if failures:
            for failure in failures:
                say(f"FAIL: {failure}")
            return 1
        say("two writings of a graph with an unreachable node are one canonical form: PASS")
        say("the comparison says false when the graph differs: PASS")
        return 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run_tool(args):
    done = subprocess.run(
        [sys.executable, str(TOOL), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return done.returncode, done.stdout, done.stderr.decode("utf-8", "replace")


if __name__ == "__main__":
    sys.exit(main())