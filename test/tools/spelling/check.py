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
handed the other reading: its source is grepped for the old forms a grep can tell apart from the
new ones -- and so is Camel source itself (`.cml`), because a generator is not the only place that
writes a program: a test case, a benchmark and a doc example are Camel too. `..` is both the
interval and what a composition used to be written with, and `:` divides a slice's bounds as well
as a Python dict, so those two forms are not linted -- they are measured instead by the formatter
fixture below, which writes all five the way the ruling writes them and fails if the formatter
changes one.

The lint reads *code positions*. A comment and a string literal are prose about the language, not
the language: a note that says the old spelling used to be `func f(x: P): int` must keep saying it,
and `println("(x) => y")` says nothing about how a lambda is written. Two exceptions, each because
there the text is code nevertheless, are in `code_lines`: a `/// @example` line is the Camel the
doc-snippet check compiles, and in a generator's own source `.cpp`/`.h` the string is what it
prints.

The reading set is a list of directories and files, read whole: `scripts` and `test/tools` are read
as directories rather than through a hand-picked few of their leaves, because a generator added
beside the others is exactly the one nobody remembers to add here. `LINT_EXEMPT` names the files
whose whole text is a list of spellings -- the parse check's probes, this file's own prose, and the
law check's probes, which its own reader parses back -- and prints them as `ok   exempt` so that
they are counted in the output rather than silently missing from it.
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

# The old forms a grep tells apart from the new spelling. A `func` head cannot be
# confused with anything: no new form writes `)` then `:` then a type. There is no word
# boundary before `func`: inside a string literal the text before it is often the two
# characters `\n`, whose `n` is a word character, and that is exactly the case this catches.
#
# The fourth entry is the untyped lambda `(x) => e`. It has no distinguishing punctuation, so a
# grep can only read it where the text is Camel: `(x) => e` is also a JavaScript arrow function,
# and `(x, y) => ...` is also a function type whose parameters carry no names. It is therefore
# limited to the file kinds that hold Camel as data (`.cml`, `.tsv`, `.cpp`, `.h`) and to a paren
# group that is not in a type position (not after `:`, and not after a name, which is what tells
# `f(x)` from `(x)`). A function type's parameters are typed, so `(x: int) => int` does not match;
# an unnamed one is written after `:`, so it does not match either.
#
# The parens must hold a parameter list of bare names, one or more. An empty one is the function
# type with no parameters (`commit::<() => int>`, `f: <x: float>() => float`), and a group that
# holds a number or an operator is an expression: `x * (-47.0) => x` is a rule's rewrite, and the
# `=>` after it divides the pattern from the replacement. Neither is an untyped lambda, and the old
# spelling this entry is for cannot be written without at least one parameter.
#
# A generator that writes its sample from a template -- `func f{i}(x: int)` for the i-th function
# -- spells the name with a hole, so `\w+` alone does not reach it and a colon written there read
# as "no hit". The name therefore takes one `{...}` hole: `0 hits` on such a generator is then a
# reading about the text, not about the shape of the pattern.
LINT = [
    (
        "result-type colon",
        re.compile(r"func\s+(?:operator\s+\S+|\w+(?:\{[^}]*\})?)\s*(?:::)?\s*(?:<[^>]*>)?\s*\([^)]*\)\s*:\s*[A-Za-z_(]"),
        None,
    ),
    ("named type argument =", re.compile(r"::<[^>]*\b[A-Za-z_]\w*\s*=\s*[^=>]"), None),
    ("paren lambda", re.compile(r"(?<!func )\([^()]*\)\s*:\s*[\w(\[]+\s*=>"), None),
    (
        "paren lambda (untyped)",
        re.compile(r"(?<![\w:])(?<!func )(?<!func)(?<!: )\([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*\)\s*=>"),
        {".cml", ".tsv", ".cpp", ".h"},
    ),
]

# The files that write Camel source for a person or for a test to compile.
LINT_FILES = [
    "src/kernel/diagnostics/registry.tsv",
    "src/stdlib/std/cml/cml.cpp",
    "scripts",
    "test/tools",
    "test/cases/trans",
    "test/cases/measured",
    "test/cases/tools",
    "test/cases/modules",
    "benchmarks",
]

# Two files whose whole text is a list of spellings. The parse check's probes name the forms the
# parser must refuse beside the ones it must accept, and this check's own prose quotes the old
# spellings it lints; a hit there is the reading, not a defect. They are printed as `ok   exempt`,
# so the two are visible in the output instead of being silently absent from it.
LINT_EXEMPT = {
    "test/tools/parsecheck/probes.py": "names the old spellings as probes, for the parser to refuse",
    "test/tools/spelling/check.py": "quotes the old spellings it lints",
    # The probe modules this check writes are read back by its own `parseDeclarations`, which takes
    # the result type after a top-level `:` in the `func` head. Until that reader takes `=>` as
    # well, the probes have to stay in the spelling it parses; changing them here would make the
    # reader see no result type and the check compare nothing.
    "scripts/law-cotangent-ports.mjs": "its probes are read back by its own signature reader",
}

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


def strip_comments(line, in_block, keep_strings):
    """One line with what is not a code position removed.

    `line` and `in_block` are the line and whether a `/* */` comment is still open, which is
    carried across lines and returned. A string literal is data -- `println("(x) => y")` says
    nothing about the spellings of lambdas -- so its contents go too, unless `keep_strings` says
    otherwise: in a *generator's* source (a `.cpp` of the printer, a `.mjs` of the reference) the
    string is where the Camel is written, and removing it would stop reading the very text the
    check exists to read.
    """
    out = []
    index = 0
    quote = ""
    while index < len(line):
        if in_block:
            end = line.find("*/", index)
            if end < 0:
                return "".join(out), True
            index = end + 2
            in_block = False
            continue
        char = line[index]
        if quote:
            if char == "\\" and index + 1 < len(line):
                if keep_strings:
                    out.append(line[index : index + 2])
                index += 2
                continue
            if char == quote:
                quote = ""
                out.append(char)
            elif keep_strings:
                out.append(char)
            index += 1
            continue
        if char in "\"'":
            quote = char
            out.append(char)
        elif line.startswith("//", index):
            break
        elif line.startswith("/*", index):
            in_block = True
            index += 2
            continue
        else:
            out.append(char)
        index += 1
    return "".join(out), in_block


def code_lines(path, text):
    """The lines of a file that hold code, as the lint reads them.

    A `.cml` file is code except where it is not: a `//` comment and a block comment are prose,
    and a string literal is data. A `///` block is prose too -- except an `@example` line, which is
    exactly the Camel the doc-snippet check compiles, so an old spelling written there is a defect
    and one quoted in a comment ("this used to be `func f(x: P): int`") is not.
    A `.cpp`/`.h` file keeps its strings: that is where the printer writes the Camel it prints.
    A `.tsv`, `.py` or `.mjs` file is read as it stands: the diagnostic registry's help text and a
    generator's sample are the material being measured, not the language it is written in.
    """
    lines = text.splitlines()
    if path.suffix == ".tsv":
        return lines
    in_block = False
    kept = []
    for line in lines:
        head = line.lstrip()
        if path.suffix == ".cml":
            if head.startswith("///"):
                payload = head[3:].lstrip()
                if not payload.startswith("@example"):
                    kept.append("")
                    continue
                line = payload[len("@example") :]
                kept.append(strip_comments(line.split("  //")[0] if "  //" in line else line, False, False)[0])
                continue
            if head.startswith("//"):
                kept.append("")
                continue
        stripped, in_block = strip_comments(line, in_block, keep_strings=path.suffix != ".cml")
        kept.append(stripped)
    return kept


def lint_hits():
    """The hits, and the exempt files that were read but not counted."""
    hits = []
    exempted = set()
    for item in LINT_FILES:
        base = ROOT / item
        paths = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file())
        for path in paths:
            if path.suffix not in (".py", ".mjs", ".cpp", ".tsv", ".h", ".cml"):
                continue
            relative = path.relative_to(ROOT).as_posix()
            if relative in LINT_EXEMPT:
                exempted.add(relative)
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            for number, line in enumerate(code_lines(path, text), 1):
                for label, pattern, kinds in LINT:
                    if kinds is not None and path.suffix not in kinds:
                        continue
                    if pattern.search(line):
                        hits.append(f"{path.relative_to(ROOT)}:{number}: {label}: {line.strip()[:80]}")
    return hits, sorted(exempted)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--camel", required=False)
    parser.add_argument("--format", default=None)
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("--lint-only", action="store_true", help="the static half, without a compiler")
    args = parser.parse_args()

    if args.lint_only:
        hits, exempted = lint_hits()
        for hit in hits:
            print(f"FAIL lint {hit}")
        for path in exempted:
            print(f"ok   exempt {path}: {LINT_EXEMPT[path]}")
        print(f"SOURCE-SPELLING lint={len(LINT)} files={len(LINT_FILES)} hits={len(hits)} exempt={len(exempted)}")
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
            done = run([str(camel), str(ROOT / case), "std::cml", "std::nop"])
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

    # 4. The lint over the generators that cannot be run here, and the two files that are lists of
    # spellings rather than text in one spelling.
    hits, exempted = lint_hits()
    failures += len(hits)
    for hit in hits:
        rows.append(f"FAIL lint {hit}")

    print(f"SOURCE-SPELLING generators={generators} samples={samples} lint={len(LINT)} files={len(LINT_FILES)}")
    for row in rows:
        print(row)
    for path in exempted:
        print(f"ok   exempt {path}: {LINT_EXEMPT[path]}")
    if failures:
        files_with_hits = len({hit.split(":")[0] for hit in hits})
        # The numerator is the hits, the denominator the readings that produced them; a sample that
        # failed is a reading that produced no text to lint, and it is named apart so that the two
        # numbers cannot be read as one.
        rest = failures - len(hits)
        apart = "" if rest == 0 else f", and {rest} sample(s) failing"
        print(
            f"SOURCE-SPELLING FAIL: {len(hits)} hits in {files_with_hits} files{apart} "
            f"({samples + len(LINT)} readings, {len(exempted)} exempt)"
        )
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
