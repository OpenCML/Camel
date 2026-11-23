"""Two writings, one transform, one canonical form -- and a transform that breaks that.

The acceptance of the spelling lane (design principles 9.5, "the graph interface does not see the
spelling"), made measurable:

  1. `greedy::fuse`, a greedy fusion written in Camel (`test/cases/trans/spelling/greedy.cml`),
     pairs the multiplications of a graph two at a time in the order `nodes` gives and fuses a pair
     into one node when the two compute the same product. The order decides which pairs are
     offered, so the transform's *answer* is a fact about the order and nothing else.

  2. The same program in two writings -- two statements swapped and every local renamed -- goes
     through it once each. The two results have to be one canonical form: the documents
     `std::jgir canonical=true` writes, compared byte for byte (`test/tools/same_graph.py` says
     what a canonical form is; the comparison is done here because the pass has to run before the
     dump). Both documents' sha256 are printed, so the reading is not a claim.

  3. The counterfactual, in the same file: `greedy::fuse_by_creation` is the same pairing written
     against `creation_id`, the number a node was created with. It reads the spelling, so its
     declaration writes the `spelling` capability -- and the two writings then pair differently
     and part as two graphs. That is what `fuse` must not be, and it is why `nodes` had to stop
     answering in NodeId order.

  4. A declaration that reads the spelling and does not write the capability is refused: the
     program in `undeclared` below is compiled and the diagnostic is read verbatim.

Run: python test/cases/trans/spelling/canonical_pairing.py --camel out/latest/bin/camel
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
HERE = Path(__file__).resolve().parent
PASS_MODULE = HERE / "greedy.cml"

# The one program, in two writings: the same graph, `(x*2 + x*2) + y*3 + x*y`, summed in the same
# order, with every local named differently. The two equal products are the two inputs of the first
# sum, so they are neighbours in the canonical order -- the graph's own structure puts them there,
# and no writing changes that. They are *not* neighbours in the order the source wrote them in, and
# that is what the two writings differ in: `A` makes `first` and `again` with `scaled` between them,
# `B` makes the two equal products one after the other.
WRITING_A = """\
func target(x: int, y: int) => int {
    let first = x * 2
    let scaled = y * 3
    let again = x * 2
    let product = x * y
    return (first + again) + scaled + product
}

func main() => int {
    return target(3, 5)
}
"""

WRITING_B = """\
func target(x: int, y: int) => int {
    let scaled = y * 3
    let base = x * 2
    let same = x * 2
    let product = x * y
    return (base + same) + scaled + product
}

func main() => int {
    return target(3, 5)
}
"""

# Reads the spelling of the program -- the name of a graph -- and declares the capability, which is
# the other half of the rule: what one declaration may do, another may not.
DECLARED = """\
import { Graph, graph_of, name } from graph

func isTarget(g: Graph) => bool spelling {
    return name(g) == 'main'
}

func main() => int spelling {
    return if isTarget(graph_of(main)) then 0 else 1
}
"""

# Reads the spelling and declares nothing. `main` calls it so that the body is built, and the
# propagation makes the refusal reach `main` too.
UNDECLARED = """\
import { Graph, graph_of, name } from graph

func entry_name(g: Graph) => string {
    return name(g)
}

func main() => int {
    let text = entry_name(graph_of(main))
    return len(text)
}
"""


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def run(camel, program, passes):
    """The canonical document of `program` after `passes`, as its exact bytes."""
    command = [camel, str(program)] + list(passes) + ["std::jgir", "canonical=true"]
    done = subprocess.run(command, capture_output=True)
    return done.returncode, done.stdout or b"", (done.stderr or b"").decode("utf-8", "replace")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def say(text=""):
    print(text)
    sys.stdout.flush()


def say_diagnostics(text, limit=3):
    """The error lines of a refusal, cut short: the runner drops the middle of a long output, and a
    message nobody can read is a message that is not there. A refusal that carries a reason after the
    error value's name is printed from that name on, so the reason is what survives the cut."""
    joined = " ".join(text.split())
    marker = joined.find("PassNotApplicable")
    if marker < 0:
        marker = joined.find("SpellingNotDeclared")
    if marker < 0:
        marker = joined.find("[Error]")
    if marker < 0:
        if joined:
            say(f"  out: {joined[:150]}")
        return
    rest = joined[marker:]
    say(f"  out: {rest[:180]}")
    if len(rest) > 180:
        say(f"  out: ... {rest[180:340]}")


def main():
    scratch = Path(os.environ.get("CAMEL_TEST_TMPDIR") or tempfile.mkdtemp(prefix="spelling-"))
    scratch.mkdir(parents=True, exist_ok=True)
    say(f"programs: {scratch}")
    shutil.copyfile(PASS_MODULE, scratch / "greedy.cml")
    left = write(scratch / "writing_a.cml", WRITING_A)
    right = write(scratch / "writing_b.cml", WRITING_B)
    failures = []
    fused_hashes = ("", "")
    spelled_hashes = ("", "")
    same_by_nodes = None
    same_by_creation = None

    # -- 1. the two writings are one program before any pass -------------------------------
    code_l, plain_l, err_l = run(EXE, left, [])
    code_r, plain_r, err_r = run(EXE, right, [])
    for name, code, raw, err in (("writing_a", code_l, plain_l, err_l), ("writing_b", code_r, plain_r, err_r)):
        if code != 0:
            failures.append(f"{name}: camel exited {code} before the pass ran: {err.strip().splitlines()[:1]}")
    say(f"unfused  writing_a sha256 {digest(plain_l)}")
    say(f"unfused  writing_b sha256 {digest(plain_r)}")
    if plain_l != plain_r:
        failures.append(
            "the two writings are two canonical forms before any pass runs, so this case is "
            "measuring the front end and not the transform"
        )
    muls_before = plain_l.count(b":op/mul_")
    say(f"multiplications before the pass: {muls_before}")
    if muls_before < 4:
        failures.append(
            f"the program has {muls_before} multiplications and the case needs four: the front end "
            f"folded or merged one away"
        )

    # -- 2. the greedy fusion in `nodes` order: one canonical form -------------------------
    code_l, fused_l, err_l = run(EXE, left, ["greedy::fuse"])
    code_r, fused_r, err_r = run(EXE, right, ["greedy::fuse"])
    if code_l != 0 or code_r != 0:
        say(f"greedy::fuse exited {code_l} and {code_r}")
        say_diagnostics((err_l or "") + (err_r or ""))
        failures.append("greedy::fuse failed")
    else:
        hash_l, hash_r = digest(fused_l), digest(fused_r)
        fused_hashes = (hash_l, hash_r)
        same_by_nodes = fused_l == fused_r
        if fused_l != fused_r:
            failures.append(
                "the two writings are two canonical forms after greedy::fuse: the transform read "
                "an order that depends on how the program was written"
            )
        muls_after = fused_l.count(b":op/mul_")
        say(f"multiplications after the pass: {muls_after}")
        if muls_after >= muls_before:
            failures.append(
                f"the pass fused nothing ({muls_before} -> {muls_after} multiplications), so the "
                f"reading is vacuous: the greedy pairing found no pair of equal products in the "
                f"canonical order"
            )

    # -- 3. the counterfactual: the same pairing read against the spelling -----------------
    code_l, spelled_l, err_l = run(EXE, left, ["greedy::fuse_by_creation"])
    code_r, spelled_r, err_r = run(EXE, right, ["greedy::fuse_by_creation"])
    if code_l != 0 or code_r != 0:
        say(f"greedy::fuse_by_creation exited {code_l} and {code_r}")
        say_diagnostics((err_l or "") + (err_r or ""))
        failures.append("greedy::fuse_by_creation failed")
    else:
        spelled_hashes = (digest(spelled_l), digest(spelled_r))
        same_by_creation = spelled_l == spelled_r
        if same_by_creation:
            failures.append(
                "the pairing read against `creation_id` gave one canonical form for the two "
                "writings, so this build's counterfactual is not one: the reading proves nothing"
            )

    # -- 4. reading the spelling without declaring it is refused --------------------------
    undeclared = write(scratch / "undeclared.cml", UNDECLARED)
    done = subprocess.run([EXE, str(undeclared), "std::nvm"], capture_output=True)
    text = (done.stdout or b"").decode("utf-8", "replace") + (done.stderr or b"").decode("utf-8", "replace")
    say()
    say("a body that reads the spelling and declares nothing:")
    say(f"  camel exited {done.returncode}")
    say_diagnostics(text, limit=4)
    if done.returncode == 0:
        failures.append("a body that reads `name` and declares no capability compiled")
    if "SpellingNotDeclared" not in text:
        failures.append("the refusal does not name the diagnostic SpellingNotDeclared")
    if "reads the spelling of the program, and its type does not declare it" not in text:
        failures.append("the refusal does not say what the promise was")

    # -- 5. the same reading, declared, is allowed ----------------------------------------
    declared = write(scratch / "declared.cml", DECLARED)
    done = subprocess.run([EXE, str(declared), "std::nvm"], capture_output=True)
    text = (done.stdout or b"").decode("utf-8", "replace") + (done.stderr or b"").decode("utf-8", "replace")
    say()
    say(f"a declaration that writes the capability: camel exited {done.returncode}, "
        f"output {text.strip()[:40]!r}")
    if done.returncode != 0:
        say_diagnostics(text, limit=3)
        for line in (done.stderr or b"").decode("utf-8", "replace").splitlines():
            if line.startswith(("SIG ", "SPELLCHECK ")):
                say(f"  {line}")
        failures.append("a declaration that writes `spelling` was refused")

    # The reading, in the last lines: the numbers are what the case exists to print, and the runner
    # keeps the end of a long output rather than all of it.
    say()
    say(f"canonical by nodes     a={fused_hashes[0]} b={fused_hashes[1]} same_graph={same_by_nodes}")
    say(f"canonical by creation  a={spelled_hashes[0]} b={spelled_hashes[1]} "
        f"same_graph={same_by_creation}")
    if failures:
        for failure in failures:
            say(f"FAIL: {failure}")
        return 1
    say("two writings through one greedy fusion are one canonical form: PASS")
    say("the same pairing read against the spelling is two: PASS")
    say("reading the spelling without the capability is refused: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())