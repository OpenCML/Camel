"""`std::cml` prints a program that compiles back to the same graph (plan S1).

S1 is a printing pass: it turns the runtime program into Camel source. The property that makes
it a pass rather than a report is round trip: for straight-line programs, the printed source
must compile to a graph isomorphic to the original. This case pins:

  * round trip through the canonical version 2 document (`test/tools/cml_roundtrip.py`) for the
    S1 corpus: the module-value capture in `linear.cml`, a small call program, and `hello`;
  * determinism: printing the same program twice gives byte-identical source;
  * `let` names survive: every name in the source graph appears in the recompiled graph and in
    the printed text; the only new names are the printer's `cmlN` temporaries;
  * a construct outside S1 is refused with `SemanticDiag::CmlUnsupportedConstruct`, and nothing
    of the program is written before the refusal;
  * a constant over `max_literal_bytes` is refused with `SemanticDiag::CmlBigConstant` and the
    message names the size and the limit.

The checker is the authority: a dump that merely looks readable does not count.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXE = os.environ.get(
    "CAMEL_EXE", str(ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel"))
)
CHECKER = ROOT / "test/tools/cml_roundtrip.py"
PYTHON = sys.executable
CASES = ROOT / "test/cases"
TMP = Path(tempfile.mkdtemp(prefix="cml-contract-"))

CORPUS = [
    CASES / "trans/cml/linear.cml",
    CASES / "trans/jgir_duplicate.cml",
    CASES / "linear/nvm/hello.cml",
]
GENERATED = re.compile(r"^(cml|p)\d+$")
VERDICTS = 0


def invoke(*args):
    return subprocess.run([str(a) for a in args], cwd=str(ROOT), capture_output=True, text=True)


def canonical(source):
    done = invoke(EXE, source, "std::jgir(canonical=true)")
    assert done.returncode == 0, (source, done.stdout, done.stderr)
    body = "\n".join(
        line for line in done.stdout.splitlines() if not line.startswith("GRAPH_STATS")
    )
    document = json.loads(body)
    assert document["format"] == "camel.gir" and document["version"] == 2, document.keys()
    return body, document


def print_source(source):
    done = invoke(EXE, source, "std::cml")
    assert done.returncode == 0, (source, done.stdout, done.stderr)
    assert done.stdout.strip(), "std::cml printed nothing"
    return done.stdout


def compare(left, right):
    global VERDICTS
    done = invoke(PYTHON, CHECKER, left, right)
    VERDICTS += 1
    assert done.returncode == 0, (left, right, done.stdout, done.stderr)
    assert done.stdout.startswith("EQUAL:"), done.stdout


def named_nodes(source):
    """The node names `std::jgir` version 1 reports, per graph."""
    done = invoke(EXE, source, "std::jgir")
    assert done.returncode == 0, (source, done.stdout, done.stderr)
    document = json.loads(done.stdout)
    assert document["version"] == 1, document["version"]
    result = {}
    for graph in document["graphs"]:
        result[graph["name"]] = {
            node["name"] for node in graph["nodes"] if node.get("name")
        }
    return result


def check_round_trip(source):
    before_text, before = canonical(source)
    printed = print_source(source)
    assert printed == print_source(source), f"{source.name}: printing is not deterministic"

    printed_path = TMP / (source.stem + ".printed.cml")
    printed_path.write_text(printed, encoding="utf-8")
    after_text, after = canonical(printed_path)

    # The canonical documents are the input, not the rendered text: compare the files so a
    # failure points at the exact document pair.
    before_path = TMP / (source.stem + ".before.json")
    after_path = TMP / (source.stem + ".after.json")
    before_path.write_text(before_text, encoding="utf-8")
    after_path.write_text(after_text, encoding="utf-8")
    compare(before_path, after_path)
    assert before["entry"] == after["entry"], (before["entry"], after["entry"])

    # Canonical v2 deliberately has no names, so name preservation is checked on the version 1
    # dump and on the printed text itself.
    original = named_nodes(source)
    recompiled = named_nodes(printed_path)
    for graph, names in original.items():
        assert graph in recompiled, (source.name, graph, sorted(recompiled))
        missing = names - recompiled[graph]
        assert not missing, (source.name, graph, sorted(missing))
        for name in names:
            assert re.search(rf"\b{re.escape(name)}\b", printed), (
                source.name,
                graph,
                name,
            )
    for graph, names in recompiled.items():
        extra = names - original.get(graph, set())
        unexpected = {name for name in extra if not GENERATED.match(name)}
        assert not unexpected, (source.name, graph, sorted(unexpected))


def check_refusal(source, diagnostic, settings=""):
    pass_name = "std::cml" + (f"({settings})" if settings else "")
    done = invoke(EXE, source, pass_name)
    assert done.returncode != 0, (source, done.stdout, done.stderr)
    message = done.stdout + done.stderr
    assert diagnostic in message, (source, message)
    assert "func " not in done.stdout, (
        "a refusal must not leave half a program on the stream",
        source,
        done.stdout,
    )


def main():
    for source in CORPUS:
        check_round_trip(source)
    assert VERDICTS >= len(CORPUS), VERDICTS

    branch = TMP / "branch.cml"
    branch.write_text(
        "func main(): int {\n"
        "    let x = 1\n"
        "    return if x > 0 then 1 else 0\n"
        "}\n",
        encoding="utf-8",
    )
    check_refusal(branch, "CmlUnsupportedConstruct")

    loop = TMP / "loop.cml"
    loop.write_text(
        "func main(): int {\n"
        "    return loop acc = 0 for x in [1, 2, 3] do acc + x\n"
        "}\n",
        encoding="utf-8",
    )
    check_refusal(loop, "CmlUnsupportedConstruct")

    big = TMP / "big.cml"
    big.write_text(
        "let text = '" + "a" * 80 + "'\n"
        "func main(): int sync {\n"
        "    println(text)\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )
    check_refusal(big, "CmlBigConstant", "max_literal_bytes=64")
    done = invoke(EXE, big, "std::cml(max_literal_bytes=64)")
    message = done.stdout + done.stderr
    assert "max_literal_bytes (64)" in message, message
    assert "over max_literal_bytes" in message, message

    print("cml round trip: PASS")


if __name__ == "__main__":
    main()