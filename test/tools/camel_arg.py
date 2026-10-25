# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The `camel` binary a checker runs is named on the command line: one rule, in one place.

`--camel <path>` (or `--camel=<path>`) names the install a run measures; with none it is this
tree's `out/latest/bin/camel`. The default is this tree's own build on purpose: `CAMEL_HOME` is the
install location's convention -- what the `camel` binary and an SDK user read to find the runtime
library and the standard library -- and a checker that let it pick the binary would be one run
measuring whichever install the shell happened to export. A checker sets `CAMEL_HOME` for the
children it starts, to the root of the install it measured; it never takes it from the environment.

A `--camel` with no value is a usage error and exits 2; it never falls back to the default -- the
run that was asked to measure one install and measured another is the defect this rule exists
against. The JavaScript half of the same rule is `test/tools/camel-arg.mjs`.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


def default_camel(root) -> str:
    """The install a checker measures with no `--camel`: this tree's `out/latest`."""
    return str(Path(root) / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def camel_argument(argv, root):
    """`(binary, rest)` from `argv`: `--camel <path>` (or `--camel=<path>`) names the binary, and
    `rest` is `argv[1:]` without that pair, in order, so a checker reads its own arguments exactly
    as it did before."""
    named = None
    rest = []
    args = list(argv[1:])
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "--camel":
            if i + 1 >= len(args):
                _refuse()
            named = args[i + 1]
            i += 2
            continue
        if arg.startswith("--camel="):
            named = arg[len("--camel=") :]
            if not named:
                _refuse()
            i += 1
            continue
        rest.append(arg)
        i += 1
    return (str(Path(named).resolve()) if named is not None else default_camel(root), rest)


def camel_from_argv(root):
    """`(binary, rest)` from this process's own arguments: the one line a checker writes."""
    return camel_argument(sys.argv, root)


def _refuse() -> None:
    print("--camel needs the path of the install to measure", file=sys.stderr)
    sys.exit(2)