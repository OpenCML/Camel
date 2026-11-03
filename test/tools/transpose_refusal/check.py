#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""One refusal is one cause, and the cause is read in four places that have to agree.

`transpose` used to answer a null graph and name its cause only as a word in the reading log, which
the caller then searched back out of the log text to choose the sentence a reader was handed. The
fact lived in the spelling, so a word renamed on one side and not the other was a sentence about a
cause nothing named -- and no program could see it: eight of the causes cannot be reached from any
program this tree can run, so a case that runs `camel` cannot pin them.

The cause is now a value, `TransposeRefusalCause`, and this check holds the four places that say it
together:

* **the header** declares the causes (`enum class TransposeRefusalCause`);
* **the word table** (`transposeRefusalWord`, `transpose.cpp`) spells each cause into the log, and is
  the only place that spells one: each word appears as a literal exactly once in `transpose.cpp` and
  nowhere in the file that renders the sentence;
* **the sentence** (`refusalSentence`, `transpose_operator.cpp`) is rendered from the cause -- its own
  word for the cause, or the sentence that names the shape a cause has no sentence for;
* **the refusal sites** decide the cause (`refuse(TransposeRefusalCause::X)`), so a cause with a
  sentence and no site is a promise nothing keeps.

Every assertion is a relation between those sets. No count is pinned: adding a cause with its word,
its sentence and its site is green here, and a count would ask for an edit to a number that measures
nothing a reader cares about (measured 2026-11-03: splitting one word into two made the plan red on
`17` before `18`, with both trees satisfying every relation).

Two lists are written out here because they are decisions, not measurements:

* `NAMES_THE_SHAPE` -- the causes that take the sentence naming the shape instead of one of their own.
  A cause cannot quietly join it, and a cause that gains its own sentence has to leave it.
* `WAITS_FOR_A_SITE` -- the cause the transform can name and no shape reaches yet. It keeps its word
  and its sentence and has no site; the day a site records it, this list has to say so.

Run: `python test/tools/transpose_refusal/check.py`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEADER_FILE = ROOT / "modules" / "transforms" / "transpose.h"
WORDS_FILE = ROOT / "modules" / "transforms" / "transpose.cpp"
SENTENCE_FILE = ROOT / "modules" / "transforms" / "transpose_operator.cpp"

# The causes whose sentence is the one that names the shape. This is not a defect list: a sentence
# that would repeat the word adds nothing, and the catch-all still prints the word. It is pinned so
# that the list is a decision someone made, not the place an unanswered cause happens to fall.
NAMES_THE_SHAPE = {
    "Arity",
    "Arm",
    "ArmTangents",
    "Component",
    "Join",
    "Kind",
    "Output",
    "Range",
}

# The cause that has its word and its sentence and waits for a shape to record it. One entry, and it
# is older than this check: the sentence exists in the dispatch and waits for the shape that produces
# it. Pinned rather than deleted, so that the day the site is written the check asks for the entry to
# go, and so that a *second* such cause is a red instead of a habit.
WAITS_FOR_A_SITE = {"NeedsAValueOutsideTheClosure"}

ENUM_START = "enum class TransposeRefusalCause {"
WORD_FN = "const char *transposeRefusalWord(TransposeRefusalCause cause) {"
SENTENCE_FN = "std::string refusalSentence(const TransposeRefusal &refusal) {"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(text: str, start_marker: str, where: str) -> str:
    """The body of the function `start_marker` opening, up to the line that closes it at column 0."""
    try:
        start = text.index(start_marker)
    except ValueError:
        raise SystemExit(f"transpose refusals: {where} has no `{start_marker}`")
    end = text.find("\n}\n", start)
    if end < 0:
        raise SystemExit(f"transpose refusals: {where}: `{start_marker}` is never closed")
    return text[start:end]


def string_literals(text: str) -> list[str]:
    """Every double-quoted literal of a C++ source, escapes kept as written.

    Words are read out of literals and not out of the file: a comment that says a cause teaches a
    reader something and produces no reading. The literals are scanned, not matched: a regular
    expression cannot tell a literal from a quotation in a comment, and this file is about exactly
    that difference.
    """
    return [literal for _, literal in scanned_literals(text)]


def scanned_literals(text: str) -> list[tuple[int, str]]:
    """Every string literal of a C++ source with the offset it starts at, tokens walked in order.

    Line comments, block comments, character literals and raw strings are states, so text that only
    looks like a literal -- a quotation in a comment, a `'` in a digit separator -- is not one.
    """
    found: list[tuple[int, str]] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            newline = text.find("\n", i)
            i = n if newline < 0 else newline
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            close = text.find("*/", i + 2)
            i = n if close < 0 else close + 2
        elif ch == '"':
            j, body = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    body.append(text[j])
                    body.append(text[j + 1])
                    j += 2
                elif text[j] == "\n":
                    break
                else:
                    body.append(text[j])
                    j += 1
            found.append((i, "".join(body)))
            i = j + 1
        elif ch == "'":
            close = text.find("'", i + 1, min(n, i + 8))
            i += 1 if close < 0 or "\n" in text[i:close] else close + 1 - i
        elif ch == "R" and i + 1 < n and text[i + 1] == '"':
            opening = text.find("(", i + 2)
            if opening < 0:
                i += 1
                continue
            delimiter = text[i + 2 : opening]
            end = text.find(")" + delimiter + '"', opening + 1)
            if end < 0:
                i = n
            else:
                found.append((i, text[opening + 1 : end]))
                i = end + len(delimiter) + 2
        else:
            i += 1
    return found


def code_only(text: str) -> str:
    """The source with comments and literal bodies blanked, line breaks and offsets kept.

    Brace matching has to see code and not strings, and keeping the length makes an offset here an
    offset in the original -- which is how a literal is placed inside a function.
    """
    blank = list(text)
    i, n = 0, len(text)

    def blank_range(start: int, stop: int) -> None:
        for k in range(start, min(stop, n)):
            if blank[k] != "\n":
                blank[k] = " "

    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            end = n if end < 0 else end
            blank_range(i, end)
            i = end
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            blank_range(i, end)
            i = end
        elif ch == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            blank_range(i, j + 1)
            i = j + 1
        elif ch == "'":
            close = text.find("'", i + 1, min(n, i + 8))
            if close < 0 or "\n" in text[i:close]:
                i += 1
            else:
                blank_range(i, close + 1)
                i = close + 1
        elif ch == "R" and i + 1 < n and text[i + 1] == '"':
            opening = text.find("(", i + 2)
            if opening < 0:
                i += 1
                continue
            delimiter = text[i + 2 : opening]
            end = text.find(")" + delimiter + '"', opening + 1)
            end = n if end < 0 else end
            blank_range(i, end)
            i = end
        else:
            i += 1
    return "".join(blank)


def render_span(text: str) -> tuple[int, int]:
    """Where the one place that prints a refusal's word starts and ends, in `text`'s offsets."""
    marker = "const auto refuse = [&](TransposeRefusalCause cause,"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(
            "transpose refusals: transpose.cpp has no `const auto refuse = [&](` lambda, so nothing "
            "is the single place that prints a refusal's word"
        )
    code = code_only(text)
    opening = code.find("{", start)
    if opening < 0:
        raise SystemExit("transpose refusals: the `refuse` lambda has no body")
    depth = 0
    for i in range(opening, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i
    raise SystemExit("transpose refusals: the `refuse` lambda is never closed")


def enum_members(path: Path) -> list[str]:
    """The causes the header declares, in the order it declares them."""
    text = read(path)
    try:
        start = text.index(ENUM_START)
    except ValueError:
        raise SystemExit(f"transpose refusals: {path.name} declares no `{ENUM_START}`")
    end = text.index("};", start)
    body = text[start + len(ENUM_START) : end]
    names = [m.group(1) for m in re.finditer(r"^\s{4}([A-Za-z][A-Za-z0-9]*),\s*$", body, re.M)]
    if not names:
        raise SystemExit(f"transpose refusals: {path.name} declares a cause enum with no members")
    return names


def word_table(path: Path) -> tuple[dict[str, str], bool]:
    """The word each cause is spelled as, and whether `None` is spelled as nothing at all."""
    body = function_body(read(path), WORD_FN, path.name)
    table = {
        m.group(1): m.group(2)
        for m in re.finditer(
            r"case TransposeRefusalCause::(\w+):\s*\n\s*return \"([^\"]+)\";", body
        )
    }
    none_is_silent = (
        re.search(r"case TransposeRefusalCause::None:\s*\n\s*return nullptr;", body) is not None
    )
    return table, none_is_silent


def recorded_causes(path: Path) -> set[str]:
    """The causes a refusal site decides, read from the sites themselves."""
    return set(re.findall(r"refuse\(TransposeRefusalCause::(\w+)", read(path)))


def sentence_dispatch(path: Path) -> tuple[set[str], set[str], bool]:
    """The causes answered in their own words, the ones that name the shape, and the catch-all.

    The switch is read the way C++ reads it: a run of labels with no statement of its own falls
    through to the next one, so the sentence belongs to every label in the run.
    """
    body = function_body(read(path), SENTENCE_FN, path.name)
    tail = "\n    }\n    return namedShape();"
    catch_all = tail in body
    if catch_all:
        body = body[: body.rindex(tail)]
    own: set[str] = set()
    named: set[str] = set()
    pending: list[str] = []
    segments = re.findall(r"case TransposeRefusalCause::(\w+):(.*?)(?=case TransposeRefusalCause::|\Z)", body, re.S)
    for name, chunk in segments:
        pending.append(name)
        if "return " not in chunk:
            continue
        target = named if "namedShape()" in chunk else own
        target.update(pending)
        pending.clear()
    return own, named, catch_all


def main() -> int:
    for path in (HEADER_FILE, WORDS_FILE, SENTENCE_FILE):
        if not path.is_file():
            print(f"transpose refusals: {path} is not here", file=sys.stderr)
            return 2

    causes = enum_members(HEADER_FILE)
    if len(set(causes)) != len(causes):
        print("transpose refusals: the header declares a cause twice", file=sys.stderr)
        return 1
    declared = set(causes)

    table, none_is_silent = word_table(WORDS_FILE)
    own, named, catch_all = sentence_dispatch(SENTENCE_FILE)
    recorded = recorded_causes(WORDS_FILE)

    broken = False

    def red(message: str) -> None:
        nonlocal broken
        print(f"transpose refusals: {message}", file=sys.stderr)
        broken = True

    # The cause with no spelling of its own: it is "refused and recorded nothing", and a word for it
    # would be a cause a reader could be told about a build that named none.
    if "None" not in declared:
        red("the header declares no `None` cause, so a build that refused before any site ran has "
            "nothing to record")
    elif not none_is_silent:
        red("`None` has a word of its own: it is the cause that means the transform named none, so "
            "the table has to answer `nullptr` for it")
    if set(table) != declared - {"None"}:
        for name in sorted((declared - {"None"}) - set(table)):
            red(f"`{name}` has no word in the table, so the log cannot spell what the walk recorded")
        for name in sorted(set(table) - (declared - {"None"})):
            red(f"the table spells `{name}`, which the header does not declare: the word outlives "
                f"the cause it named")
        if broken:
            return 1

    # One place spells a word. If the sentence file spelled any of them, the sentence would be a
    # second spelling of the fact, which is the shape this change removed.
    words_file_literals = string_literals(read(WORDS_FILE))
    sentence_literals = string_literals(read(SENTENCE_FILE))
    for name in sorted(table):
        word = table[name]
        if words_file_literals.count(word) != 1:
            red(f"`{name}` is spelled `{word}` {words_file_literals.count(word)} times in "
                f"{WORDS_FILE.name}: the word belongs in the table and nowhere else")
        if sentence_literals.count(word) != 0:
            red(f"`{name}`'s word `{word}` is spelled in {SENTENCE_FILE.name}: the sentence is "
                f"rendered from the cause, so it reads the word from the table instead of writing it")

    if not catch_all:
        red("`refusalSentence` has no catch-all that names the shape, so a cause with no sentence of "
            "its own would be answered with nothing")

    # A word is printed in one place, and it is printed by asking the table. A literal that spells
    # `skip=` anywhere else is a second spelling of the same fact, whether or not it agrees with the
    # table today -- and a site that writes its own log line is exactly how the one on
    # `UnreadParameter` survived the change that removed the rest.
    span = render_span(read(WORDS_FILE))
    for path in (WORDS_FILE, SENTENCE_FILE, HEADER_FILE):
        text = read(path)
        for offset, literal in scanned_literals(text):
            if "skip=" not in literal:
                continue
            if path == WORDS_FILE and span[0] <= offset <= span[1]:
                continue
            line = text.count("\n", 0, offset) + 1
            red(f"{path.name}:{line}: a string literal spells `{literal.strip()}` outside the one "
                f"place that renders a refusal's word: the word belongs in the table and is printed "
                f"by `refuse`, so a hand-written line is a second spelling of it")

    if own & named:
        red(f"`{', '.join(sorted(own & named))}` has its own sentence and names the shape at once")
    if own | named != declared:
        for name in sorted(declared - (own | named)):
            red(f"`{name}` has no sentence: `refusalSentence` does not answer it")
        for name in sorted((own | named) - declared):
            red(f"`{name}` has a sentence, and the header does not declare it")
        if broken:
            return 1

    if named != NAMES_THE_SHAPE:
        for name in sorted(named - NAMES_THE_SHAPE):
            red(f"`{name}` lands on the sentence that names the shape, and that list is not where an "
                f"unanswered cause should fall: write its sentence, or add it to NAMES_THE_SHAPE "
                f"with the reason it adds nothing")
        for name in sorted(NAMES_THE_SHAPE - named):
            red(f"NAMES_THE_SHAPE names `{name}`, which has a sentence of its own or is not declared "
                f"any more: take it out of the list")

    if recorded - declared:
        for name in sorted(recorded - declared):
            red(f"a refusal site records `{name}`, which the header does not declare")
    unreached = declared - {"None"} - recorded
    if unreached != WAITS_FOR_A_SITE:
        for name in sorted(unreached - WAITS_FOR_A_SITE):
            red(f"`{name}` has a word and a sentence and no refusal site records it: either the "
                f"cause was renamed or the site is gone, and the sentence is a promise nothing keeps")
        for name in sorted(WAITS_FOR_A_SITE - unreached):
            red(f"WAITS_FOR_A_SITE names `{name}`, which a site records now: take it out of the list")

    if broken:
        return 1

    print(
        "transpose refusals: every cause has one word, one sentence, and a site that records it -- "
        f"except `{', '.join(sorted(WAITS_FOR_A_SITE))}`, which has the first two and waits for a shape"
    )
    print(
        "transpose refusals: a cause with no sentence of its own lands on the one that names the "
        f"shape, and that list is written out here (`{', '.join(sorted(NAMES_THE_SHAPE))}`)"
    )
    print(
        "transpose refusals: the word a cause is spelled as appears in one file only -- the table -- "
        "so the caller renders the sentence from the cause and never from the log"
    )
    print(
        "transpose refusals: no literal spells `skip=` outside the one place that renders it, so a "
        "refusal point cannot hand-write the line the table is there to spell"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())