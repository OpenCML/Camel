#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""One refusal cause, one table, and the two halves that read it.

The readings (`REINTERP jvp function=f skip=body`) used to spell their cause at every site, so the
word was the fact: a plan that pinned the line was red for a rename, and nothing could say "this
cause" without also saying which sentence. `modules/transforms/refusal_readings.h` makes the cause a
value (`RefusalCause`), spells it once (`refusalCauseName`), and has every refusing site record it
(`noteRefusal`) as the machine-readable half a plan asserts (`READING_REFUSAL {json}`,
`test/tools/reading-refusal.mjs`, the `expect_refusal` fields).

This check holds the places that have to agree, as relations and never as counts:

* **the header declares the causes**, and `None` -- "no refusal recorded" -- has no word, so a site
  that names no cause cannot spell one;
* **the word table** spells every cause exactly once, and the words are distinct: two causes spelled
  the same are one cause a reader cannot tell from the other;
* **no site spells a cause itself**: `skip=` is a literal nowhere in the three walks, every
  `skip="` is followed by the table's call, and a file that spells through the table includes it --
  without that last one the other relations are green on a file that does not compile;
* **every printed cause is recorded and every record names the printed cause**: the number of
  `refusalCauseName` calls in a file equals the number of `noteRefusal` calls, and the cause in the
  record beside a line is the cause that line printed. Without this the two halves could say two
  things, which is the defect the record exists to make impossible;
* **every cause has a site**: a word in the table that nothing prints is a promise nothing keeps
  (the one exception is written out below, so the day it is kept the check asks for the entry to go).

No count is pinned: adding a cause with its word, its site and its record is green here.

Run: `python test/tools/reading_refusal/check.py`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEADER_FILE = ROOT / "modules" / "transforms" / "refusal_readings.h"
WALK_FILES = [
    ROOT / "modules" / "transforms" / "jvp.cpp",
    ROOT / "modules" / "transforms" / "split.cpp",
    ROOT / "modules" / "transforms" / "vmap.cpp",
]

# The causes the table declares and no walk records yet. Pinned rather than deleted, so that a
# *second* such cause is a red instead of a habit, and so the day this one gets its site this entry
# has to say so. Empty: every word below is printed by one of the three walks.
WAITS_FOR_A_SITE: set[str] = set()


def strip_comments(text: str) -> str:
    """Comments blanked, offsets and newlines kept: a word in prose is not a spelling."""
    out = list(text)
    i, n = 0, len(text)

    def blank(a: int, b: int) -> None:
        for k in range(a, min(b, n)):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
        elif ch == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            i = j + 1
        else:
            i += 1
    return "".join(out)


def literals(text: str) -> list[str]:
    """The bodies of the string literals of `text`, in order."""
    found: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif ch == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            found.append(text[i + 1 : j])
            i = j + 1
        else:
            i += 1
    return found


def balanced(text: str, start: int) -> str:
    """The argument list that begins at `start` (a `(`), braces included."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    raise SystemExit("unbalanced parentheses in a noteRefusal call")


def declared_causes(header: str) -> list[str]:
    body = re.search(r"enum class RefusalCause\s*\{(.*?)\};", header, re.S)
    if body is None:
        raise SystemExit(f"{HEADER_FILE}: no `enum class RefusalCause`")
    members = [word for word in re.findall(r"\b([A-Z][A-Za-z0-9]*)\b", body.group(1))]
    return [m for m in members if m != "None"]


def word_table(header: str) -> dict[str, str]:
    body = re.search(r"inline const char \*refusalCauseName\(RefusalCause cause\)\s*\{(.*?)\n\}", header, re.S)
    if body is None:
        raise SystemExit(f"{HEADER_FILE}: no `refusalCauseName`")
    table: dict[str, str] = {}
    for cause, word in re.findall(r'case RefusalCause::(\w+):\s*\n\s*return "([^"]*)";', body.group(1)):
        table[cause] = word
    return table


def main() -> int:
    problems: list[str] = []
    header = HEADER_FILE.read_text(encoding="utf-8")
    causes = declared_causes(header)
    table = word_table(header)

    if not causes:
        problems.append(f"{HEADER_FILE.name}: the table declares no cause")

    # One word per cause, one cause per word, a word a log line can hold.
    for cause in causes:
        if cause not in table:
            problems.append(f"cause `{cause}` has no word: a refusal that names it cannot be spelled")
    for cause, word in table.items():
        if cause not in causes:
            problems.append(f"the table spells `{cause}`, which the enum does not declare")
        if not re.fullmatch(r"[a-z][a-z0-9-]*", word):
            problems.append(f"cause `{cause}` is spelled `{word}`, which is not a lowercase word")
    seen: dict[str, str] = {}
    for cause, word in table.items():
        if word in seen:
            problems.append(f"`{word}` spells both `{seen[word]}` and `{cause}`")
        seen[word] = cause
    if "None" in table:
        problems.append("`None` is spelled; a line that names no cause must not be able to spell one")

    # No site spells a cause: the words live in the table, the sites call it.
    used: set[str] = set()
    for walk in WALK_FILES:
        text = walk.read_text(encoding="utf-8")
        for body in literals(text):
            for word in re.findall(r"skip=([a-z][a-z0-9-]*)", body):
                problems.append(f"{walk.name}: a literal spells `skip={word}`: the table is the only spelling")

        code = strip_comments(text)
        # A file that spells through the table has to include it: without this relation the other
        # relations are green on a file that does not compile (measured 2026-11-04 -- a rewrite of
        # `split.cpp` from the parent commit dropped the include, and only the build said so).
        if "refusalCauseName(" in code and '#include "refusal_readings.h"' not in text:
            problems.append(f"{walk.name}: spells a cause through the table and does not include it")
        printed = [m for m in re.finditer(r'skip="\s*<<\s*refusalCauseName\(RefusalCause::(\w+)\)', code)]
        causes_in_file = re.findall(r"refusalCauseName\(RefusalCause::(\w+)\)", code)
        records = re.findall(r"noteRefusal\(", code)
        for match in printed:
            used.add(match.group(1))
        # A `skip="` that the table did not answer is a hand-written word or a broken chain.
        for match in re.finditer(r'skip="', code):
            if not re.match(r'skip="\s*<<\s*refusalCauseName\(', code[match.start() :]):
                problems.append(f"{walk.name}: `skip=` at offset {match.start()} is not answered by the table")
        if len(causes_in_file) != len(records):
            problems.append(
                f"{walk.name}: {len(causes_in_file)} printed cause(s) and {len(records)} record(s): "
                "a line printed without a record, or a record without a line"
            )
        # Beside each printed cause, the record has to name the same cause.
        for match in re.finditer(r"refusalCauseName\(RefusalCause::(\w+)\)", code):
            cause = match.group(1)
            after = code[match.end() : match.end() + 800]
            at = after.find("noteRefusal(")
            if at < 0:
                problems.append(f"{walk.name}: `{cause}` is printed with no record beside it")
                continue
            call = balanced(after, at + len("noteRefusal"))
            named = re.search(r",\s*RefusalCause::(\w+)\s*\)", call)
            if named is None or named.group(1) != cause:
                problems.append(
                    f"{walk.name}: the line prints `{cause}` and the record beside it says "
                    f"`{named.group(1) if named else 'no cause'}`"
                )

    for cause in causes:
        if cause not in used and cause not in WAITS_FOR_A_SITE:
            problems.append(f"cause `{cause}` has a word and no site: a promise nothing keeps")
    for cause in sorted(WAITS_FOR_A_SITE):
        if cause in used:
            problems.append(f"`WAITS_FOR_A_SITE` still lists `{cause}`, which a site records now")

    if problems:
        for problem in problems:
            print(f"[RED] {problem}")
        return 1

    print(f"refusal readings: {len(causes)} cause(s), one word each, spelled in one file only -- the table")
    print("refusal readings: no literal spells `skip=`, so a refusal site cannot hand-write the line")
    print("refusal readings: every printed cause has a record beside it, and the record names the printed cause")
    print(
        "refusal readings: every cause has one word and one site that records it -- "
        + (
            "the causes that wait for a site are written out here"
            if WAITS_FOR_A_SITE
            else "the list of causes that wait for a site is empty"
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())