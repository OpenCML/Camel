"""`std::cml` prints a program that compiles back to the same graph.

`std::cml` is a printing pass: it turns the runtime program into Camel source, and the property
that makes it a pass rather than a report is round trip. For every program in the corpus the
printed source must compile to a graph isomorphic to the original. This case pins:

  * round trip through the canonical version 2 document (`test/tools/cml_roundtrip.py`) for the
    straight-line corpus (the module-value capture in `linear.cml`, a small call program, and
    `hello`), for branches (`if`, `match`, nested branches, a `return` in an arm), for loops
    (`for`, `while`, `for ... while`, two states, a struct state, a body with effects), for the
    interval `a..b` in the sequence of a `for` and in a value position (`interval.cml`: the loop is
    fused by the compiler and its graph holds the two bounds, so the printed source has to spell
    the interval again), for
    closures (a lambda as a value, a block body, captured source names, a returned lambda, a
    closure over a cell), for `with` parameters (including one that also reads a module value),
    and for a `var` parameter;
  * execution: the printed program prints the same stdout and exits with the same status as the
    original;
  * determinism: printing the same program twice gives byte-identical source;
  * source names survive: every named node in the source graph appears in the recompiled graph
    and in the printed text, captured names included; the only new names are the printer's
    temporaries. Anonymous lambda graphs (`__g_<id>__`) are compared globally, because their
    graph name is an allocation, not a source name;
  * the constructs outside S2 are refused with `SemanticDiag::CmlUnsupportedConstruct` and
    nothing of the program is written before the refusal;
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
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it (the author's ruling of 2026-10-25
# replaced the `CAMEL_EXE` environment switch with that argument); without it,
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py.
EXE, ARGV = camel_from_argv(ROOT)
CHECKER = ROOT / "test/tools/cml_roundtrip.py"
PYTHON = sys.executable
CASES = ROOT / "test/cases"
CML = CASES / "trans/cml"
TMP = Path(tempfile.mkdtemp(prefix="cml-contract-"))

CORPUS = [
    CML / "linear.cml",
    CML / "branch.cml",
    CML / "loop.cml",
    CML / "interval.cml",
    CML / "closure.cml",
    CML / "with_param.cml",
    CML / "var_param.cml",
    CASES / "trans/jgir_duplicate.cml",
    CASES / "linear/nvm/hello.cml",
]
# The printer's temporaries, and a parameter name it has to invent.
GENERATED = re.compile(r"^(cml|p|w)\d+$")
# The compiler names a lambda's graph after its own id; recompiling may allocate another id.
ANONYMOUS_GRAPH = re.compile(r"^__g_\d+__$")
# A name a source declaration can actually spell. A loop's state port may carry a path (`p.x`)
# or a compiler marker (`$tagged0`); those are not source names, and the printer may replace them.
SOURCE_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# What the printed text must spell, per corpus program. The checker proves equivalence; these
# assert that the program was reached through the construct it exists to pin.
MARKERS = {
    "branch": ["if ", "else ", "match ", "case "],
    "loop": ["loop ", "for ", "while ", "sync {"],
    "interval": ["..", "for ", "while ", "let "],
    "closure": ["=>", "sync", "offset", "base"],
    "with_param": ["with <", "scale", "clamp"],
    "var_param": ["var "],
}
VERDICTS = 0


def invoke(*args):
    return subprocess.run([str(a) for a in args], cwd=str(ROOT), capture_output=True, text=True)


def canonical(source):
    done = invoke(EXE, source, "std::jgir canonical=true")
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


def run_source(source):
    """What the program does: its stdout and its exit status."""
    done = invoke(EXE, source, "std::nvm")
    return done.stdout, done.returncode


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


def check_names(source, printed, printed_path):
    original = named_nodes(source)
    recompiled = named_nodes(printed_path)
    original_names = set().union(*original.values()) if original else set()
    recompiled_names = set().union(*recompiled.values()) if recompiled else set()
    required = {name for name in original_names if SOURCE_NAME.match(name)}

    missing = required - recompiled_names
    assert not missing, (source.name, sorted(missing))
    for name in required:
        assert re.search(rf"\b{re.escape(name)}\b", printed), (source.name, name)

    # A named graph is a source function and keeps its identity; a lambda's graph name is an
    # allocation, so its parameter and capture names are checked in the aggregate above.
    for graph, names in original.items():
        if ANONYMOUS_GRAPH.match(graph):
            continue
        assert graph in recompiled, (source.name, graph, sorted(recompiled))
        absent = {name for name in names if SOURCE_NAME.match(name)} - recompiled[graph]
        assert not absent, (source.name, graph, sorted(absent))

    extra = recompiled_names - required
    unexpected = {name for name in extra if not GENERATED.match(name)}
    assert not unexpected, (source.name, sorted(unexpected))


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

    check_names(source, printed, printed_path)

    original_run = run_source(source)
    printed_run = run_source(printed_path)
    assert original_run == printed_run, (
        source.name,
        "the printed program does not run like the original",
        original_run,
        printed_run,
    )

    for marker in MARKERS.get(source.stem, []):
        assert marker in printed, (source.name, marker)


def check_refusal(source, diagnostic, passes=("std::cml",), contains=()):
    done = invoke(EXE, source, *passes)
    assert done.returncode != 0, (source, done.stdout, done.stderr)
    message = done.stdout + done.stderr
    assert diagnostic in message, (source, message)
    for text in contains:
        assert text in message, (source, text, message)
    assert "func " not in done.stdout, (
        "a refusal must not leave half a program on the stream",
        source,
        done.stdout,
    )


def main():
    for source in CORPUS:
        check_round_trip(source)
    assert VERDICTS >= len(CORPUS), VERDICTS

    # S1 keeps its big-constant refusal: a literal the printer will not write.
    big = TMP / "big.cml"
    big.write_text(
        "let text = '" + "a" * 80 + "'\n"
        "func main(): int sync {\n"
        "    println(text)\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )
    check_refusal(
        big, "CmlBigConstant", ("std::cml max_literal_bytes=64",), ("over max_literal_bytes",)
    )
    done = invoke(EXE, big, "std::cml max_literal_bytes=64")
    message = done.stdout + done.stderr
    assert "max_literal_bytes (64)" in message, message

    # The constructs S2 deliberately leaves to a later step stay refusals with a diagnostic.
    check_refusal(CML / "refuse/named_type.cml", "CmlUnsupportedConstruct", contains=("named type",))
    check_refusal(
        CML / "refuse/tagged_node.cml", "CmlUnsupportedConstruct", contains=("attributes",)
    )
    check_refusal(CML / "refuse/operator.cml", "CmlUnsupportedConstruct", contains=("operator",))
    check_refusal(CML / "refuse/partial_value.cml", "CmlUnsupportedConstruct")
    check_refusal(
        CML / "refuse/inline_region.cml",
        "CmlUnsupportedConstruct",
        ("std::opt::inline", "std::cml"),
    )

    print("cml round trip: PASS")


if __name__ == "__main__":
    main()