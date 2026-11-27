#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Holds what the command line says about a space after a comma.

An option that reads `key=value` words (`--gc`, `--spaces`, `--gc-stress`, `--gc-check`) is
refused when a word carries a space around it, and the run does not start. The refusal has to
say what the writer did -- a comma separates the words and no space follows it -- and print the
word as it should be written.

The plan runner validates the words of these options itself, so a plan test whose `args` hold a
spaced word never reaches camel: the plan is refused at load time. The words therefore live here,
under a name, and this tool starts camel itself.

  option_spelling.py refuse <program> <name>     -- the spaced spelling under that name
  option_spelling.py accept <program> <name>     -- the compact spelling of the same option
  option_spelling.py accept-words <program> <option> <compact words>
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]

SENTENCE = "A comma separates the words and no space follows it"

# name -> (option, the words as a writer typed them, the compact word the message must print)
SPELLINGS = {
    "gc": ("--gc", "kind=generational, large_threshold_kb=64", "--gc large_threshold_kb=64"),
    "spaces": ("--spaces", "block_mb=64, perm_mb=2", "--spaces perm_mb=2"),
    "gc-stress": ("--gc-stress", "kind=minor, alloc=4", "--gc-stress alloc=4"),
    "gc-check": ("--gc-check", "moves, verify=both", "--gc-check verify=both"),
}

# name -> the compact spelling of the same option, which must be accepted
COMPACT = {
    "gc": ("--gc", "kind=generational,nursery_mb=4"),
    "spaces": ("--spaces", "block_mb=64,perm_mb=2"),
    "gc-stress": ("--gc-stress", "kind=minor,alloc=4"),
    "gc-check": ("--gc-check", "moves,verify=both"),
}


def camel_exe() -> str:
    home = os.environ.get("CAMEL_HOME") or str(REPO_ROOT / "out" / "latest")
    return str(Path(home) / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def run(program: str, option: str, words: str):
    exe = camel_exe()
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(exe).resolve().parents[1]))
    return subprocess.run([exe, option, words, program], capture_output=True, text=True,
                          timeout=120, env=env)


def main(argv) -> int:
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    mode, program, name = argv[1], argv[2], argv[3]
    if mode == "refuse":
        option, words, compact = SPELLINGS[name]
    elif mode == "accept":
        option, words = COMPACT[name]
        compact = ""
    elif mode == "accept-words" and len(argv) >= 5:
        option, words, compact = argv[3], argv[4], ""
    else:
        print(__doc__, file=sys.stderr)
        return 2
    proc = run(program, option, words)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    if mode in ("accept", "accept-words"):
        print(f"camel exit={proc.returncode} (accepted)")
        return 0 if proc.returncode == 0 else 1
    missing = [what for what in (SENTENCE, compact) if what not in proc.stderr]
    print(f"camel exit={proc.returncode} (refused)")
    if proc.returncode != 1:
        print(f"expected exit 1, got {proc.returncode}", file=sys.stderr)
        return 1
    if missing:
        print("stderr does not contain: " + " | ".join(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))