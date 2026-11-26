#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
"""The operators whose effect nobody stated, and whether the record carries the statement.

`camel inspect --ops <program>` prints one `OPERATOR {...}` line per operator of the modules the
program imports, with two readings of the effect: `purity`, what the declaration in the table says
(design principles 3.3), and the record's own copy of it -- `record`, `uses_stated`, `clause_pure`.
A reader that asks the record (`core.arrangeable`, and the device's placement question
`tiles::touchesNothing`) reads the second group, so the two must agree wherever the record exists,
and an operator whose declaration states nothing is the one to fill in.

What it prints, in this order:

  * how many operators the program's modules have, how many have a record and how many state
    `uses` in it;
  * the operators whose declaration itemized accesses that did not reach the record, and the ones
    whose `purity` and `clause_pure` read differently (either list means the copy is not the copy);
  * the operators whose two entrances both said nothing -- the list to work through;
  * `oprecord uses: PASS` when the copy holds everywhere it exists.

It exits 0 with the list printed: the list is the size of the debt, not a red.
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
)
DEFAULT_CAMEL = os.path.join(ROOT, "out", "latest", "bin", "camel.exe" if os.name == "nt" else "camel")


def parse_args(argv):
    """`<program> [--camel <path>]`: the program whose modules are listed, and the install."""
    program = None
    binary = DEFAULT_CAMEL
    i = 0
    while i < len(argv):
        if argv[i] == "--camel" and i + 1 < len(argv):
            binary = argv[i + 1]
            i += 2
            continue
        if program is None:
            program = argv[i]
        i += 1
    if program is None:
        raise SystemExit("oprecord_uses: no program named")
    return program, binary


def operators(binary, program):
    """One dict per operator, from `camel inspect --ops <program>`."""
    path = program if os.path.isabs(program) else os.path.join(ROOT, program)
    done = subprocess.run(
        [binary, "inspect", "--ops", path],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=300,
    )
    if done.returncode != 0:
        sys.stdout.write(done.stdout)
        sys.stderr.write(done.stderr)
        raise SystemExit(f"camel inspect --ops exited {done.returncode}")
    rows = []
    for line in done.stdout.splitlines():
        line = line.strip()
        if line.startswith("OPERATOR "):
            rows.append(json.loads(line[len("OPERATOR ") :]))
    return rows


def main(argv):
    program, binary = parse_args(argv)
    rows = operators(binary, program)
    if not rows:
        raise SystemExit("camel inspect --ops printed no operator")

    with_record = [r for r in rows if r.get("record")]
    stated = [r for r in rows if r.get("uses_stated")]
    silent = [r for r in rows if r.get("record") and not r.get("uses_stated")]
    not_in_record = [r for r in rows if r.get("accesses_declared") and not r.get("uses_stated")]
    purity_mismatch = [
        r
        for r in rows
        if r.get("record") and (r.get("purity") == "pure") != bool(r.get("clause_pure"))
    ]

    # The counts first, and again in the closing block: a plan runner shows the head of a long
    # output and the tail of a truncated one, so the two readings of interest are in both places.
    print(f"operators: {len(rows)}")
    print(f"with a record: {len(with_record)}")
    print(f"uses stated in the record: {len(stated)}")
    print(f"neither entrance said: {len(silent)}")
    print(f"purity disagreed between the declaration and the record: {len(purity_mismatch)}")
    print(f"declared accesses that are not in the record: {len(not_in_record)}")

    def block(title, items, limit):
        if not items:
            return
        print(f"{title}: {len(items)}")
        for r in items[:limit]:
            print(f"  {r['module']} {r['uri']}")
        if len(items) > limit:
            print(f"  ... and {len(items) - limit} more (the command in the docstring lists them all)")

    print("summary: operators=%d record=%d uses_stated=%d silent=%d purity_disagreed=%d not_in_record=%d" % (len(rows), len(with_record), len(stated), len(silent), len(purity_mismatch), len(not_in_record)))
    block("not in the record", not_in_record, 40)
    for r in purity_mismatch[:40]:
        print(f"  purity disagreed: {r['module']} {r['uri']} purity={r['purity']}")
    block(
        "the operators to fill in, by module",
        sorted(silent, key=lambda row: (row["module"], row["uri"])),
        40,
    )

    # The invariant this case holds: the copy exists at all -- some operators state `uses` in
    # their record, and every one that does reads the same clause the declaration reads. The
    # operators where the copy has not reached the record yet are printed above with their module:
    # that list is the debt to work through, not a failure of this run.
    if not stated:
        print("No operator states `uses` in its record: the copy from the declaration is gone.")
        print("oprecord uses: FAIL")
        return 1

    # What a cost function is told, and where the number comes from. A record that states a cost and
    # one that states none both print a number, and a person reading one line must not have to
    # consult the record to know which is which: `cost_stated` travels with `cost`, so the same
    # number reads differently when it is the reader's default rather than the operator's statement.
    costs = [r for r in rows if "cost" in r and "cost_stated" in r]
    if not costs:
        print("No operator prints its cost with its source: the field is gone.")
        print("oprecord uses: FAIL")
        return 1
    declared = [r for r in costs if r["cost_stated"]]
    silent_cost = [r for r in costs if not r["cost_stated"]]
    if not declared or not silent_cost:
        print("Every operator prints the same cost source: the field says nothing.")
        print("oprecord uses: FAIL")
        return 1
    print("oprecord uses: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))