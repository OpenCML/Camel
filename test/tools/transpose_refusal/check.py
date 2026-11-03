#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Every word `transpose` can write into its reading log has a readable landing.

`buildTransposeGraph` answers a null graph for every cause and names the cause in its log as
`skip=<word>`; `refusalCause` turns that word into the sentence a caller reads. The two live in
different files -- the words in `modules/transforms/transpose.cpp`, the sentences in
`modules/transforms/transpose_operator.cpp` -- so one can gain a word the other has never heard of,
or keep a sentence for a word that was renamed or deleted, and no program can see it: eight of the
words cannot be reached from any program this tree can run, so a case that runs `camel` cannot pin
them, and a case that pins the six reachable ones says nothing about the rest.

This check reads both files and holds three **relations** together -- no count of words is pinned,
because a count is what an assertion says when it means "these sets still line up": add a word with
its sentence and the sets still line up, while a count asks for an edit to a number that measures
nothing a reader cares about (measured 2026-11-03: splitting one word into two made the plan red on
`17` before `18`, with both trees satisfying every relation below).

* every written word lands on a non-empty sentence (inclusion, one way). A word with no sentence of
  its own lands on the one sentence that names the word -- `the shape this program used has no
  transpose (the transform named it `<word>`)`. It is not a sentence of its own, so the set of such
  words is written out below (`LANDS_ON_THE_NAMED_SHAPE`): a word cannot quietly join it, and a
  sentence deleted from the dispatch moves its word into the set, which is a red here.
* every sentence of its own is written by a word some file writes (inclusion, the other way). A
  sentence nobody reaches is a claim about a cause the transform cannot name.
* the catch-all exists and interpolates the word, so every word it is handed lands on it and says
  which word it was.

`refusalCause` is file-local to the module's `.cpp`, so no program calls it and no plan tool links
it; the properties checked are the ones its dispatch has, read from the two sources that hold them.
Run: `python test/tools/transpose_refusal/check.py`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WORDS_FILE = ROOT / "modules" / "transforms" / "transpose.cpp"
SENTENCE_FILE = ROOT / "modules" / "transforms" / "transpose_operator.cpp"

# The words that have no sentence of their own and therefore read the sentence that names them. This
# is not a defect list: a cause whose sentence would repeat the word adds nothing, and the catch-all
# still prints the word. It is pinned so that the list is a decision someone made, not the place an
# unanswered word happens to fall.
LANDS_ON_THE_NAMED_SHAPE = {
    "arity",
    "arm",
    "arm-tangents",
    "component",
    "join",
    "kind",
    "output",
    "range",
}

# Sentences whose word no file writes. One entry, and it is older than this check: the cause exists in
# the dispatch and waits for the shape that produces it. Pinned rather than deleted, so that the day
# the shape is written the check asks for the entry to go, and so that a *second* such sentence is a
# red instead of a habit.
WAITS_FOR_A_WORD = {"needs-a-value-outside-the-closure"}


def string_literals(text: str) -> list[str]:
    """Every double-quoted literal of a C++ source, escapes kept as written.

    The words are read out of literals and not out of the file: a comment that says `skip=state`
    teaches a reader something and produces no reading, and a check that read comments would keep a
    word alive that nothing writes.
    """
    return [m.group(1) for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', text)]


def emitted_words(path: Path) -> set[str]:
    words: set[str] = set()
    for literal in string_literals(path.read_text(encoding="utf-8")):
        words.update(m.group(1) for m in re.finditer(r"skip=([a-z][a-z0-9-]*)", literal))
    return words


def dispatch(path: Path) -> tuple[set[str], bool]:
    """The words `refusalCause` answers in their own words, and whether it names the shape."""
    text = path.read_text(encoding="utf-8")
    start = text.index("std::string refusalCause(")
    end = text.index("\n}\n", start)
    body = text[start:end]
    answered = set(re.findall(r'word == "([a-z][a-z0-9-]*)"', body))
    # The catch-all is the one return that puts the word into the sentence it answers with; without
    # it an unanswered word would arrive as an empty string.
    names_the_word = re.search(r'return\s+"[^"]*"\s*\+\s*word\s*\+\s*"', body) is not None
    return answered, names_the_word


def main() -> int:
    for path in (WORDS_FILE, SENTENCE_FILE):
        if not path.is_file():
            print(f"transpose refusals: {path} is not here", file=sys.stderr)
            return 2

    emitted = emitted_words(WORDS_FILE)
    answered, names_the_word = dispatch(SENTENCE_FILE)
    if not emitted:
        print("transpose refusals: no word is written into the log", file=sys.stderr)
        return 1
    if not answered:
        print("transpose refusals: the dispatch answers no word", file=sys.stderr)
        return 1
    if not names_the_word:
        print(
            "transpose refusals: the dispatch has no sentence that names the word it answers, so a "
            "word without a sentence of its own arrives as nothing",
            file=sys.stderr,
        )
        return 1

    # Every file that writes a `skip=` word, not only the one that is checked word by word: a
    # sentence is a lie whatever writes the word it names.
    written_anywhere: set[str] = set()
    for path in sorted((ROOT / "modules" / "transforms").glob("*.cpp")):
        written_anywhere |= emitted_words(path)

    named_shape = emitted - answered
    unreached = answered - written_anywhere
    broken = False

    for word in sorted(named_shape - LANDS_ON_THE_NAMED_SHAPE):
        print(
            f"transpose refusals: `{word}` lands on the sentence that names the shape, and that "
            f"list is not where an unanswered word should fall: write its sentence, or add it to "
            f"LANDS_ON_THE_NAMED_SHAPE with the reason it adds nothing",
            file=sys.stderr,
        )
        broken = True
    for word in sorted(LANDS_ON_THE_NAMED_SHAPE - named_shape):
        print(
            f"transpose refusals: LANDS_ON_THE_NAMED_SHAPE names `{word}`, which has a sentence of "
            f"its own or is not written any more: take it out of the list",
            file=sys.stderr,
        )
        broken = True
    for word in sorted(unreached - WAITS_FOR_A_WORD):
        print(
            f"transpose refusals: `{word}` has a sentence and no file writes the word: either the "
            f"cause was renamed or it is gone, and the sentence is a promise nothing keeps",
            file=sys.stderr,
        )
        broken = True
    for word in sorted(WAITS_FOR_A_WORD - unreached):
        print(
            f"transpose refusals: WAITS_FOR_A_WORD names `{word}`, which is written now: take it out "
            f"of the list",
            file=sys.stderr,
        )
        broken = True
    if broken:
        return 1

    # What is said here is what was decided above, as relations: each line is one of the three
    # properties, and none of them counts anything. A count would make every added word an edit here
    # and in the plan, which is exactly the assertion that reads like a property and measures a
    # number -- and the two trees either side of the 2026-11-03 split satisfied both relations and
    # printed two different totals.
    print(
        "transpose refusals: every word the log writes lands on a sentence -- its own, or the "
        "catch-all that names it; every sentence of its own is written by a file that writes the "
        "word; and a word with no sentence of its own lands nowhere but the catch-all, which names "
        f"it (`{', '.join(sorted(WAITS_FOR_A_WORD))}` waits for a shape, not for a sentence)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())