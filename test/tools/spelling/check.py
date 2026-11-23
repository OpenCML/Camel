#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Every generator whose output is Camel source, handed back to the parser.

The notation ruling of 2026-11-23 (design principles 4.4, 5.9) changed what a person writes:

    1. a result type after `=>`, never after `:`      `func f(x: int) => int`
    2. a composition `<>`; `..` is the interval        `f <> g`
    3. a named type argument after `:`                 `f::<T: int>`
    4. a lambda starts with `func`                     `func (x: int) => int { x }`
    5. a slice `t[a..b]`; `:` is not a slice separator `xs[1..3]`

The corpus was rewritten; the code that *writes* programs for a person was not, and a generator
that still writes one of the five old forms produces text the front end refuses. This check is the
one the ruling asks for: each generator is asked for a sample that covers the forms, and the
sample is compiled back -- `camel check -s`, which reads the syntax tree and nothing after it.

A sample is also counted. A generator that answers nothing would otherwise "parse" (there is no
error in an empty file), so every sample must have at least MIN_LINES lines and MIN_TOKENS tokens,
and both numbers are printed for every emitter.

    python test/tools/spelling/check.py --camel out/latest/bin/camel
    python test/tools/spelling/check.py --camel <exe> --format <camel-format>

What is a sample, and what is a lint. A generator the check can run is asked for text and the text
is compiled: the printer (`std::cml`), the formatter (`camel-format`) and the program generator
(`test/tools/progen`). A generator whose output cannot be produced without a whole install is
handed the other reading: its source is grepped for the three old forms a grep can tell apart from
the new ones. `..` is both the interval and what a composition used to be written with, and `:`
divides a slice's bounds as well as a Python dict, so those two forms are not linted -- they are
measured instead by the formatter fixture below, which writes all five the way the ruling writes
them and fails if the formatter changes one.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MIN_LINES = 5
MIN_TOKENS = 20

TOKEN = re.compile(r"[A-Za-z_]\w*|\d+(?:\.\d+)?|'(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\"|[^\s]")

# The three old forms a grep tells apart from the new spelling. A `func` head cannot be
# confused with anything: no new form writes `)` then `:` then a type. There is no word
# boundary before `func`: inside a string literal the text before it is often the two
# characters `\n`, whose `n` is a word character, and that is exactly the case this catches.
LINT = [
    ("result-type colon", re.compile(r"func\s+(?:operator\s+\S+|\w+)\s*(?:::)?\s*(?:<[^>]*>)?\s*\([^)]*\)\s*:\s*[A-Za-z_(]")),
    ("named type argument =", re.compile(r"::<[^>]*\b[A-Za-z_]\w*\s*=\s*[^=>]")),
    ("paren lambda", re.compile(r"(?<!func )\([^()]*\)\s*:\s*[\w(\[]+\s*=>")),
]

# The files that write Camel source for a person or for a test to compile.
LINT_FILES = [
    "src/kernel/diagnostics/registry.tsv",
    "src/stdlib/std/cml/cml.cpp",
    "scripts/stdlib-reference.mjs",
    "test/tools/progen",
    "test/tools/scale",
    "test/tools/perfbaseline",
    "test/cases/trans",
    "test/cases/measured",
    "test/cases/tools",
    "benchmarks",
]

# The five forms, written the way the ruling writes them. The formatter must hand them back
# unchanged (it may reindent): the check is that it invents none of the old ones.
FIXTURE = """func make(base: int) => (x: int) => int {
    return func (x: int) => int { x + base }
}

func twice(n: int) => int {
    let f = func (x: int) => int { x * 2 }
    return f(n)
}

func main() => int sync {
    let xs: int[] = 0..4
    let part: int[] = xs[1..3]
    let g = twice <> make(1)
    let h = twice::<int>
    println(part)
    println(g(2))
    return 0
}
"""

NEW_FORMS = ["<>", "[1..3]", "::<int>", "func (x: int) => int", "func main() => int"]


class Row:
    def __init__(self, name, text, detail=""):
        self.name = name
        self.text = text
        self.detail = detail

    def lines(self):
        return len([one for one in self.text.splitlines() if one.strip()])

    def tokens(self):
        return len(TOKEN.findall(self.text))


def run(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd or ROOT, capture_output=True, text=True)


def parse_only(camel, text, workdir, name="sample"):
    path = Path(workdir) / f"{name}.cml"
    path.write_text(text, encoding="utf-8", newline="\n")
    return subprocess.run(
        [str(camel), "check", "-s", str(path)], cwd=workdir, capture_output=True, text=True
    )


def lint_hits():
    hits = []
    for item in LINT_FILES:
        base = ROOT / item
        paths = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file())
        for path in paths:
            if path.suffix not in (".py", ".mjs", ".cpp", ".tsv", ".h"):
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            for number, line in enumerate(text.splitlines(), 1):
                for label, pattern in LINT:
                    if pattern.search(line):
                        hits.append(f"{path.relative_to(ROOT)}:{number}: {label}: {line.strip()[:80]}")
    return hits


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--camel", required=False)
    parser.add_argument("--format", default=None)
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("--lint-only", action="store_true", help="the static half, without a compiler")
    args = parser.parse_args()

    if args.lint_only:
        hits = lint_hits()
        for hit in hits:
            print(f"FAIL lint {hit}")
        print(f"SOURCE-SPELLING lint={len(LINT)} files={len(LINT_FILES)} hits={len(hits)}")
        return 1 if hits else 0

    if not args.camel:
        parser.error("--camel is required unless --lint-only is given")

    camel = Path(args.camel)
    # The formatter is built beside the compiler; a run of the plan names the compiler only.
    format_exe = Path(args.format) if args.format else camel.with_name("camel-format")
    if not format_exe.exists():
        format_exe = None
    rows = []
    failures = 0
    samples = 0
    generators = 0

    cases = [
        "test/cases/trans/cml/closure.cml",
        "test/cases/trans/cml/interval.cml",
        "test/cases/trans/cml/map_with_closure.cml",
        "test/cases/trans/cml/linear.cml",
    ]

    with tempfile.TemporaryDirectory(prefix="spelling-") as work:
        work = Path(work)

        # 1. The printer.
        generators += 1
        for case in cases:
            samples += 1
            name = f"std::cml {Path(case).stem}"
            done = run([str(camel), str(ROOT / case), "std::cml"])
            if done.returncode != 0:
                failures += 1
                rows.append(f"FAIL {name}: std::cml refused it: {(done.stdout + done.stderr).strip().splitlines()[:1]}")
                continue
            rows.append(check_one(camel, name, done.stdout, work, failures=0))
            if rows[-1].startswith("FAIL"):
                failures += 1

        # 2. The formatter: the same programs, and the fixture that writes the five forms.
        if format_exe is not None:
            generators += 1
            for case in cases:
                samples += 1
                name = f"camel-format {Path(case).stem}"
                done = run([str(format_exe), str(ROOT / case)])
                if done.returncode != 0 or not done.stdout.strip():
                    failures += 1
                    rows.append(f"FAIL {name}: it wrote nothing: {done.stderr.strip()[:100]}")
                    continue
                rows[-1:] = [check_one(camel, name, done.stdout, work, failures=0)]
                if rows[-1].startswith("FAIL"):
                    failures += 1
            samples += 1
            name = "camel-format fixture (five forms)"
            fixture = work / "fixture.cml"
            fixture.write_text(FIXTURE, encoding="utf-8", newline="\n")
            done = run([str(format_exe), str(fixture)])
            if done.returncode != 0 or not done.stdout.strip():
                failures += 1
                rows.append(f"FAIL {name}: it wrote nothing: {done.stderr.strip()[:100]}")
            else:
                missing = [form for form in NEW_FORMS if form not in done.stdout]
                row = check_one(camel, name, done.stdout, work, failures=0)
                if missing:
                    failures += 1
                    rows.append(row.split(" -- ")[0] + f" -- the formatter dropped {missing}")
                else:
                    rows.append(row)

        # 3. The program generator.
        generators += 1
        samples += 1
        name = f"progen seed {args.seed}"
        done = run([sys.executable, str(ROOT / "test/tools/progen/progen.py"), "show", "--seed", str(args.seed)])
        if done.returncode != 0:
            failures += 1
            rows.append(f"FAIL {name}: {done.stderr.strip()[:120]}")
        else:
            text = done.stdout
            cut = text.find("// environment:")
            text = text if cut < 0 else text[:cut].rstrip() + "\n"
            rows.append(check_one(camel, name, text, work, failures=0))
            if rows[-1].startswith("FAIL"):
                failures += 1

    # 4. The lint over the generators that cannot be run here.
    hits = lint_hits()
    failures += len(hits)
    for hit in hits:
        rows.append(f"FAIL lint {hit}")

    print(f"SOURCE-SPELLING generators={generators} samples={samples} lint={len(LINT)} files={len(LINT_FILES)}")
    for row in rows:
        print(row)
    if failures:
        print(f"SOURCE-SPELLING FAIL: {failures} of {samples + len(LINT)} readings")
        return 1
    print("SOURCE-SPELLING PASS: every sample parses, and no generator writes one of the five old forms")
    return 0


def check_one(camel, name, text, work, failures=0):
    row = Row(name, text)
    done = parse_only(camel, text, work, name=re.sub(r"\W+", "_", name))
    if done.returncode != 0:
        detail = (done.stdout + done.stderr).strip().splitlines()
        return f"FAIL {name}: lines={row.lines()} tokens={row.tokens()} -- {detail[0] if detail else 'no output'}"
    if row.lines() < MIN_LINES or row.tokens() < MIN_TOKENS:
        return f"FAIL {name}: lines={row.lines()} tokens={row.tokens()} -- too small to be a reading"
    return f"ok   {name}: lines={row.lines()} tokens={row.tokens()}"


if __name__ == "__main__":
    sys.exit(main())
