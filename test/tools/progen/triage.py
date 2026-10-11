#!/usr/bin/env python3
"""Lists the bad cells of a progen run, grouped, with the source line a diagnostic points at.

    triage.py DIR [--outcome MISMATCH] [--limit 5] [--cell SUBSTR]

DIR is the --out directory of `progen.py selfcheck` or `progen.py diff` (it reads
DIR/failures/*.json and the sources beside them). Groups are keyed by outcome, diagnostic name
and the message with numbers and names blanked, so that one cause shows as one group.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path


def key_of(info: dict) -> str:
    detail = info.get("detail", "")
    if info["outcome"] == "MISMATCH":
        detail = ""
    detail = re.sub(r"'[^']*'", "'_'", detail)
    detail = re.sub(r"\b[a-z]+\d+\b", "_", detail)
    detail = re.sub(r"-?\d+", "N", detail)
    return f"{info['outcome']} {info.get('diag', '')} {detail[:150]}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--outcome")
    ap.add_argument("--cell")
    ap.add_argument("--limit", type=int, default=4)
    args = ap.parse_args()
    groups = collections.defaultdict(list)
    for path in sorted(Path(args.dir, "failures").glob("*.json"), key=lambda p: int(re.sub(r"\D", "", p.stem) or 0)):
        case = json.loads(path.read_text(encoding="utf-8"))
        for cid, info in case["bad"].items():
            if args.outcome and info["outcome"] != args.outcome:
                continue
            if args.cell and args.cell not in cid:
                continue
            groups[key_of(info)].append((case["seed"], cid, info, path.with_suffix(".cml")))
    for key, items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        seeds = sorted({s for s, *_ in items})
        cells = collections.Counter(c for _, c, *_ in items)
        print(f"== {len(items)} cells, {len(seeds)} programs: {key}")
        print(f"   seeds: {' '.join(map(str, seeds[:20]))}")
        print(f"   cells: {', '.join(f'{c} {n}' for c, n in cells.most_common(10))}")
        for seed, cid, info, src in items[:args.limit]:
            line = ""
            text = src.read_text(encoding="utf-8").splitlines() if src.exists() else []
            where = info.get("where")
            if where and 0 < where <= len(text):
                line = text[where - 1].strip()[:200]
            print(f"   - seed {seed} [{cid}] {info.get('detail', '')[:260]}")
            if line:
                print(f"       line {where}: {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
